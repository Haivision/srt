/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

// Medium concretizations

// Just for formality. This file should be used 
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <iterator>
#include <map>
#include <chrono>
#include <thread>
#include <atomic>
#include <srt.h>
#if !defined(_WIN32)
#include <sys/ioctl.h>
#endif

// SRT protected includes
#include "netinet_any.h"
#include "common.h"
#include "api.h"
#include "udt.h"
#include "logging.h"
#include "utilities.h"

#include "apputil.hpp"
#include "socketoptions.hpp"
#include "uriparser.hpp"
#include "testmedia.hpp"
#include "srt_compat.h"
#include "verbose.hpp"

using namespace std;
using namespace srt;

using srt_logging::KmStateStr;
using srt_logging::SockStatusStr;
#if ENABLE_BONDING
using srt_logging::MemberStatusStr;
#endif

std::atomic<bool> transmit_throw_on_interrupt {false};
std::atomic<bool> transmit_int_state {false};
int transmit_bw_report = 0;
unsigned transmit_stats_report = 0;
size_t transmit_chunk_size = SRT_LIVE_DEF_PLSIZE;
bool transmit_printformat_json = false;
srt_listen_callback_fn* transmit_accept_hook_fn = nullptr;
void* transmit_accept_hook_op = nullptr;
bool transmit_use_sourcetime = false;
int transmit_retry_connect = 0;
bool transmit_retry_always = false;

// Do not unblock. Copy this to an app that uses applog and set appropriate name.
//srt_logging::Logger applog(SRT_LOGFA_APP, srt_logger_config, "srt-test");

std::shared_ptr<SrtStatsWriter> transmit_stats_writer;

string DirectionName(SRT_EPOLL_T direction)
{
    string dir_name;
    if (direction & ~SRT_EPOLL_ERR)
    {
        if (direction & SRT_EPOLL_IN)
        {
            dir_name = "source";
        }

        if (direction & SRT_EPOLL_OUT)
        {
            if (!dir_name.empty())
                dir_name = "relay";
            else
                dir_name = "target";
        }

        if (direction & SRT_EPOLL_ERR)
        {
            dir_name += "+error";
        }
    }
    else
    {
        // stupid name for a case of IPE
        dir_name = "stone";
    }

    return dir_name;
}

template<class FileBase> inline
bytevector FileRead(FileBase& ifile, size_t chunk, const string& filename)
{
    bytevector data(chunk);
    ifile.read(data.data(), chunk);
    size_t nread = ifile.gcount();
    if (nread < data.size())
        data.resize(nread);

    if (data.empty())
        throw Source::ReadEOF(filename);
    return data;
}


class FileSource: public virtual Source
{
    ifstream ifile;
    string filename_copy;
public:

    FileSource(const string& path): ifile(path, ios::in | ios::binary), filename_copy(path)
    {
        if (!ifile)
            throw std::runtime_error(path + ": Can't open file for reading");
    }

    MediaPacket Read(size_t chunk) override { return FileRead(ifile, chunk, filename_copy); }

    bool IsOpen() override { return bool(ifile); }
    bool End() override { return ifile.eof(); }
    //~FileSource() { ifile.close(); }
};

class FileTarget: public virtual Target
{
    ofstream ofile;
public:

    FileTarget(const string& path): ofile(path, ios::out | ios::trunc | ios::binary) {}

    void Write(const MediaPacket& data) override
    {
        ofile.write(data.payload.data(), data.payload.size());
#ifdef PLEASE_LOG
        applog.Debug() << "FileTarget::Write: " << data.payload.size() << " written to a file";
#endif
    }

    bool IsOpen() override { return !!ofile; }
    bool Broken() override { return !ofile.good(); }
    //~FileTarget() { ofile.close(); }
    void Close() override
    {
#ifdef PLEASE_LOG
        applog.Debug() << "FileTarget::Close";
#endif
        ofile.close();
    }
};

// Can't base this class on FileSource and FileTarget classes because they use two
// separate fields, which makes it unable to reliably define IsOpen(). This would
// require to use 'fstream' type field in some kind of FileCommon first. Not worth
// a shot.
class FileRelay: public Relay
{
    fstream iofile;
    string filename_copy;
public:

    FileRelay(const string& path):
        iofile(path, ios::in | ios::out | ios::binary), filename_copy(path)
    {
        if (!iofile)
            throw std::runtime_error(path + ": Can't open file for reading");
    }
    MediaPacket Read(size_t chunk) override { return FileRead(iofile, chunk, filename_copy); }

    void Write(const MediaPacket& data) override
    {
        iofile.write(data.payload.data(), data.payload.size());
    }

    bool IsOpen() override { return !!iofile; }
    bool End() override { return iofile.eof(); }
    bool Broken() override { return !iofile.good(); }
    void Close() override { iofile.close(); }
};

template <class Iface> struct File;
template <> struct File<Source> { typedef FileSource type; };
template <> struct File<Target> { typedef FileTarget type; };
template <> struct File<Relay> { typedef FileRelay type; };

template <class Iface>
Iface* CreateFile(const string& name) { return new typename File<Iface>::type (name); }

void SrtCommon::InitParameters(string host, string path, map<string,string> par)
{
    // Application-specific options: mode, blocking, timeout, adapter
    if ( Verbose::on && !par.empty())
    {
        Verb() << "SRT parameters specified:\n";
        for (map<string,string>::iterator i = par.begin(); i != par.end(); ++i)
        {
            Verb() << "\t" << i->first << " = '" << i->second << "'\n";
        }
    }

    if (path != "")
    {
        // Special case handling of an unusual specification.

        if (path.substr(0, 2) != "//")
        {
            Error("Path specification not supported for SRT (use // in front for special cases)");
        }

        path = path.substr(2);

#if ENABLE_BONDING
        if (path == "group")
        {
            // Group specified, check type.
            m_group_type = par["type"];
            if (m_group_type == "")
            {
                Error("With //group, the group 'type' must be specified.");
            }

            vector<string> parts;
            Split(m_group_type, '/', back_inserter(parts));
            if (parts.size() == 0 || parts.size() > 2)
            {
                Error("Invalid specification for 'type' parameter");
            }

            if (parts.size() == 2)
            {
                m_group_type = parts[0];
                m_group_config = parts[1];
            }

            vector<string> nodes;
            Split(par["nodes"], ',', back_inserter(nodes));

            if (nodes.empty())
            {
                Error("With //group, 'nodes' must specify comma-separated host:port specs.");
            }

            int token = 1;

            // Check if correctly specified
            for (string& hostport: nodes)
            {
                if (hostport == "")
                    continue;

                // The attribute string, as it was embedded in another URI,
                // must have had replaced the & character with another ?, so
                // now all ? character, except the first one, must be now
                // restored so that UriParser interprets them correctly.

                size_t atq = hostport.find('?');
                if (atq != string::npos)
                {
                    while (atq+1 < hostport.size())
                    {
                        size_t next = hostport.find('?', atq+1);
                        if (next == string::npos)
                            break;
                        hostport[next] = '&';
                        atq = next;
                    }
                }

                UriParser check(hostport, UriParser::EXPECT_HOST);
                if (check.host() == "" || check.port() == "")
                {
                    Error("With //group, 'nodes' must specify comma-separated host:port specs.");
                }

                if (check.portno() <= 1024)
                {
                    Error("With //group, every node in 'nodes' must have port >1024");
                }

                Connection cc(check.host(), check.portno());
                if (check.parameters().count("weight"))
                {
                    cc.weight = stoi(check.queryValue("weight"));
                }

                if (check.parameters().count("source"))
                {
                    UriParser sourcehp(check.queryValue("source"), UriParser::EXPECT_HOST);
                    cc.source = CreateAddr(sourcehp.host(), sourcehp.portno());
                }

                // Check if there's a key with 'srto.' prefix.

                UriParser::query_it start = check.parameters().lower_bound("srto.");

                SRT_SOCKOPT_CONFIG* config = nullptr;
                bool all_clear = true;
                vector<string> fails;
                map<string, string> options;

                if (start != check.parameters().end())
                {
                    for (; start != check.parameters().end(); ++start)
                    {
                        auto& y = *start;
                        if (y.first.substr(0, 5) != "srto.")
                            break;

                        options[y.first.substr(5)] = y.second;
                    }
                }

                if (!options.empty())
                {
                    config = srt_create_config();

                    for (auto o: srt_options)
                    {
                        if (!options.count(o.name))
                            continue;
                        string value = options.at(o.name);
                        bool ok = o.apply<SocketOption::SRT>(config, value);
                        if ( !ok )
                        {
                            fails.push_back(o.name);
                            all_clear = false;
                        }
                    }

                    if (!all_clear)
                    {
                        srt_delete_config(config);
                        Error("With //group, failed to set options: " + Printable(fails));
                    }

                    cc.options = config;
                }

                cc.token = token++;
                m_group_nodes.push_back(move(cc));
            }

            par.erase("type");
            par.erase("nodes");

            // For a group-connect specification, it's
            // always the caller mode.
            // XXX change it here if maybe rendezvous is also
            // possible in future.
            par["mode"] = "caller";
        }
#endif
    }

    if (par.count("bind"))
    {
        string bindspec = par.at("bind");
        UriParser u (bindspec, UriParser::EXPECT_HOST);
        if ( u.scheme() != ""
                || u.path() != ""
                || !u.parameters().empty()
                || u.portno() == 0)
        {
            Error("Invalid syntax in 'bind' option");
        }

        if (u.host() != "")
            par["adapter"] = u.host();
        par["port"] = u.port();
        par.erase("bind");
    }

    string adapter;
    if (par.count("adapter"))
    {
        adapter = par.at("adapter");
    }

    m_mode = "default";
    if (par.count("mode"))
    {
        m_mode = par.at("mode");
    }
    SocketOption::Mode mode = SrtInterpretMode(m_mode, host, adapter);
    if (mode == SocketOption::FAILURE)
    {
        Error("Invalid mode");
    }

    if (!m_group_nodes.empty() && mode != SocketOption::CALLER)
    {
        Error("Group node specification is only available in caller mode");
    }

    // Fix the mode name after successful interpretation
    m_mode = SocketOption::mode_names[mode];

    par.erase("mode");

    if (par.count("blocking"))
    {
        m_blocking_mode = !false_names.count(par.at("blocking"));
        par.erase("blocking");
    }

    if (par.count("timeout"))
    {
        m_timeout = stoi(par.at("timeout"), 0, 0);
        par.erase("timeout");
    }

    if (par.count("adapter"))
    {
        m_adapter = adapter;
        par.erase("adapter");
    }
    else if (m_mode == "listener")
    {
        // For listener mode, adapter is taken from host,
        // if 'adapter' parameter is not given
        m_adapter = host;
    }

    if (par.count("tsbpd") && false_names.count(par.at("tsbpd")))
    {
        m_tsbpdmode = false;
    }

    if (par.count("port"))
    {
        m_outgoing_port = stoi(par.at("port"), 0, 0);
        par.erase("port");
    }

    // That's kinda clumsy, but it must rely on the defaults.
    // Default mode is live, so check if the file mode was enforced
    if (par.count("transtype") == 0 || par["transtype"] != "file")
    {
        // If the Live chunk size was nondefault, enforce the size.
        if (transmit_chunk_size != SRT_LIVE_DEF_PLSIZE)
        {
            if (transmit_chunk_size > SRT_LIVE_MAX_PLSIZE)
                throw std::runtime_error("Chunk size in live mode exceeds 1456 bytes; this is not supported");

            par["payloadsize"] = Sprint(transmit_chunk_size);
        }
    }

    // Assigning group configuration from a special "groupconfig" attribute.
    // This is the only way how you can set up this configuration at the listener side.
    if (par.count("groupconfig"))
    {
        m_group_config = par.at("groupconfig");
        par.erase("groupconfig");
    }

    // Fix Minversion, if specified as string
    if (par.count("minversion"))
    {
        string v = par["minversion"];
        if (v.find('.') != string::npos)
        {
            int version = srt::SrtParseVersion(v.c_str());
            if (version == 0)
            {
                throw std::runtime_error(Sprint("Value for 'minversion' doesn't specify a valid version: ", v));
            }
            par["minversion"] = Sprint(version);
            Verb() << "\tFIXED: minversion = 0x" << std::hex << std::setfill('0') << std::setw(8) << version << std::dec;
        }
    }

    // Assign the others here.
    m_options = par;
    m_options["mode"] = m_mode;
}

void SrtCommon::PrepareListener(string host, int port, int backlog)
{
    m_bindsock = srt_create_socket();
    if (m_bindsock == SRT_ERROR)
        Error("srt_create_socket");

    int stat = ConfigurePre(m_bindsock);
    if (stat == SRT_ERROR)
        Error("ConfigurePre");

    if (!m_blocking_mode)
    {
        srt_conn_epoll = AddPoller(m_bindsock, SRT_EPOLL_IN);
    }

    auto sa = CreateAddr(host, port);
    Verb() << "Binding a server on " << host << ":" << port << " ...";
    stat = srt_bind(m_bindsock, sa.get(), sizeof sa);
    if (stat == SRT_ERROR)
    {
        srt_close(m_bindsock);
        Error("srt_bind");
    }

    Verb() << " listen... " << VerbNoEOL;
    stat = srt_listen(m_bindsock, backlog);
    if (stat == SRT_ERROR)
    {
        srt_close(m_bindsock);
        Error("srt_listen");
    }

}

void SrtCommon::StealFrom(SrtCommon& src)
{
    // This is used when SrtCommon class designates a listener
    // object that is doing Accept in appropriate direction class.
    // The new object should get the accepted socket.
    m_direction = src.m_direction;
    m_blocking_mode = src.m_blocking_mode;
    m_timeout = src.m_timeout;
    m_tsbpdmode = src.m_tsbpdmode;
    m_options = src.m_options;
    m_bindsock = SRT_INVALID_SOCK; // no listener
    m_sock = src.m_sock;
    src.m_sock = SRT_INVALID_SOCK; // STEALING
}

void SrtCommon::AcceptNewClient()
{
    sockaddr_any scl;

    ::transmit_throw_on_interrupt = true;

    if (!m_blocking_mode)
    {
        Verb() << "[ASYNC] (conn=" << srt_conn_epoll << ")";

        int len = 2;
        SRTSOCKET ready[2];
        while (srt_epoll_wait(srt_conn_epoll, ready, &len, 0, 0, 1000, 0, 0, 0, 0) == -1)
        {
            if (::transmit_int_state)
                Error("srt_epoll_wait for srt_accept: interrupt");

            if (srt_getlasterror(NULL) == SRT_ETIMEOUT)
                continue;
            Error("srt_epoll_wait(srt_conn_epoll)");
        }

        Verb() << "[EPOLL: " << len << " sockets] " << VerbNoEOL;
    }
    Verb() << " accept..." << VerbNoEOL;

    m_sock = srt_accept(m_bindsock, (scl.get()), (&scl.len));
    if (m_sock == SRT_INVALID_SOCK)
    {
        srt_close(m_bindsock);
        m_bindsock = SRT_INVALID_SOCK;
        Error("srt_accept");
    }

#if ENABLE_BONDING
    if (m_sock & SRTGROUP_MASK)
    {
        m_listener_group = true;
        if (m_group_config != "")
        {
            // Don't break the connection basing on this, just ignore.
            Verb() << " (ignoring setting group config: '" << m_group_config << "') " << VerbNoEOL;
        }
        // There might be added a poller, remove it.
        // We need it work different way.

#ifndef SRT_OLD_APP_READER

        if (srt_epoll != -1)
        {
            Verb() << "(Group: erasing epoll " << srt_epoll << ") " << VerbNoEOL;
            srt_epoll_release(srt_epoll);
        }

        // Don't add any sockets, they will have to be added
        // anew every time again.
        srt_epoll = srt_epoll_create();
#endif

        // Group data must have a size of at least 1
        // otherwise the srt_group_data() call will fail
        if (m_group_data.empty())
            m_group_data.resize(1);

        Verb() << " connected(group epoll " << srt_epoll <<").";
    }
    else
#endif
    {
        sockaddr_any peeraddr(AF_INET6);
        string peer = "<?PEER?>";
        if (-1 != srt_getpeername(m_sock, (peeraddr.get()), (&peeraddr.len)))
        {
            peer = peeraddr.str();
        }

        sockaddr_any agentaddr(AF_INET6);
        string agent = "<?AGENT?>";
        if (-1 != srt_getsockname(m_sock, (agentaddr.get()), (&agentaddr.len)))
        {
            agent = agentaddr.str();
        }

        Verb() << " connected [" << agent << "] <-- " << peer;
    }
    ::transmit_throw_on_interrupt = false;

    // ConfigurePre is done on bindsock, so any possible Pre flags
    // are DERIVED by sock. ConfigurePost is done exclusively on sock.
    int stat = ConfigurePost(m_sock);
    if (stat == SRT_ERROR)
        Error("ConfigurePost");
}

static string PrintEpollEvent(int events, int et_events)
{
    static pair<int, const char*> const namemap [] = {
        make_pair(SRT_EPOLL_IN, "R"),
        make_pair(SRT_EPOLL_OUT, "W"),
        make_pair(SRT_EPOLL_ERR, "E"),
        make_pair(SRT_EPOLL_UPDATE, "U")
    };

    ostringstream os;
    int N = (int)Size(namemap);

    for (int i = 0; i < N; ++i)
    {
        if (events & namemap[i].first)
        {
            os << "[";
            if (et_events & namemap[i].first)
                os << "^";
            os << namemap[i].second << "]";
        }
    }

    return os.str();
}

void SrtCommon::Init(string host, int port, string path, map<string,string> par, SRT_EPOLL_OPT dir)
{
    m_direction = dir;
    InitParameters(host, path, par);

    int backlog = 1;
    if (m_mode == "listener" && par.count("groupconnect")
            && true_names.count(par["groupconnect"]))
    {
        backlog = 10;
    }

    Verb() << "Opening SRT " << DirectionName(dir) << " " << m_mode
        << "(" << (m_blocking_mode ? "" : "non-") << "blocking,"
        << " backlog=" << backlog << ") on "
        << host << ":" << port;

    try
    {
        if (m_mode == "caller")
        {
            if (m_group_nodes.empty())
            {
                OpenClient(host, port);
            }
#if ENABLE_BONDING
            else
            {
                OpenGroupClient(); // Source data are in the fields already.
            }
#endif
        }
        else if (m_mode == "listener")
            OpenServer(m_adapter, port, backlog);
        else if (m_mode == "rendezvous")
            OpenRendezvous(m_adapter, host, port);
        else
        {
            throw std::invalid_argument("Invalid 'mode'. Use 'client' or 'server'");
        }
    }
    catch (...)
    {
        // This is an in-constructor-called function, so
        // when the exception is thrown, the destructor won't
        // close the sockets. This intercepts the exception
        // to close them.
        Verb() << "Open FAILED - closing SRT sockets";
        if (m_bindsock != SRT_INVALID_SOCK)
            srt_close(m_bindsock);
        if (m_sock != SRT_INVALID_SOCK)
            srt_close(m_sock);
        m_sock = m_bindsock = SRT_INVALID_SOCK;
        throw;
    }

    int pbkeylen = 0;
    SRT_KM_STATE kmstate, snd_kmstate, rcv_kmstate;
    int len = sizeof (int);
    srt_getsockflag(m_sock, SRTO_PBKEYLEN, &pbkeylen, &len);
    srt_getsockflag(m_sock, SRTO_KMSTATE, &kmstate, &len);
    srt_getsockflag(m_sock, SRTO_SNDKMSTATE, &snd_kmstate, &len);
    srt_getsockflag(m_sock, SRTO_RCVKMSTATE, &rcv_kmstate, &len);

    Verb() << "ENCRYPTION status: " << KmStateStr(kmstate)
        << " (SND:" << KmStateStr(snd_kmstate) << " RCV:" << KmStateStr(rcv_kmstate)
        << ") PBKEYLEN=" << pbkeylen;

    // Display some selected options on the socket.
    if (Verbose::on)
    {
        int64_t bandwidth = 0;
        int latency = 0;
        bool blocking_snd = false, blocking_rcv = false;
        int dropdelay = 0;
        int size_int = sizeof (int), size_int64 = sizeof (int64_t), size_bool = sizeof (bool);
        char packetfilter[100] = "";
        int packetfilter_size = 100;

        srt_getsockflag(m_sock, SRTO_MAXBW, &bandwidth, &size_int64);
        srt_getsockflag(m_sock, SRTO_RCVLATENCY, &latency, &size_int);
        srt_getsockflag(m_sock, SRTO_RCVSYN, &blocking_rcv, &size_bool);
        srt_getsockflag(m_sock, SRTO_SNDSYN, &blocking_snd, &size_bool);
        srt_getsockflag(m_sock, SRTO_SNDDROPDELAY, &dropdelay, &size_int);
        srt_getsockflag(m_sock, SRTO_PACKETFILTER, (packetfilter), (&packetfilter_size));

        Verb() << "OPTIONS: maxbw=" << bandwidth << " rcvlatency=" << latency << boolalpha
            << " blocking{rcv=" << blocking_rcv << " snd=" << blocking_snd
            << "} snddropdelay=" << dropdelay << " packetfilter=" << packetfilter;
    }

    if (!m_blocking_mode)
    {
        // Don't add new epoll if already created as a part
        // of group management: if (srt_epoll == -1)...

        if (m_mode == "caller")
            dir = (dir | SRT_EPOLL_UPDATE);
        Verb() << "NON-BLOCKING MODE - SUB FOR " << PrintEpollEvent(dir, 0);

        srt_epoll = AddPoller(m_sock, dir);
    }
}

int SrtCommon::AddPoller(SRTSOCKET socket, int modes)
{
    int pollid = srt_epoll_create();
    if (pollid == -1)
        throw std::runtime_error("Can't create epoll in nonblocking mode");
    Verb() << "EPOLL: creating eid=" << pollid << " and adding @" << socket
        << " in " << DirectionName(SRT_EPOLL_OPT(modes)) << " mode";
    srt_epoll_add_usock(pollid, socket, &modes);
    return pollid;
}

int SrtCommon::ConfigurePost(SRTSOCKET sock)
{
    bool yes = m_blocking_mode;
    int result = 0;
    if (m_direction & SRT_EPOLL_OUT)
    {
        Verb() << "Setting SND blocking mode: " << boolalpha << yes << " timeout=" << m_timeout;
        result = srt_setsockopt(sock, 0, SRTO_SNDSYN, &yes, sizeof yes);
        if (result == -1)
        {
#ifdef PLEASE_LOG
            extern srt_logging::Logger applog;
            applog.Error() << "ERROR SETTING OPTION: SRTO_SNDSYN";
#endif
            return result;
        }

        if (m_timeout)
            result = srt_setsockopt(sock, 0, SRTO_SNDTIMEO, &m_timeout, sizeof m_timeout);
        if (result == -1)
        {
#ifdef PLEASE_LOG
            extern srt_logging::Logger applog;
            applog.Error() << "ERROR SETTING OPTION: SRTO_SNDTIMEO";
#endif
            return result;
        }
    }

    if (m_direction & SRT_EPOLL_IN)
    {
        Verb() << "Setting RCV blocking mode: " << boolalpha << yes << " timeout=" << m_timeout;
        result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &yes, sizeof yes);
        if (result == -1)
            return result;

        if (m_timeout)
            result = srt_setsockopt(sock, 0, SRTO_RCVTIMEO, &m_timeout, sizeof m_timeout);
        else
        {
            int timeout = 1000;
            result = srt_setsockopt(sock, 0, SRTO_RCVTIMEO, &timeout, sizeof timeout);
        }
        if (result == -1)
            return result;
    }

    // host is only checked for emptiness and depending on that the connection mode is selected.
    // Here we are not exactly interested with that information.
    vector<string> failures;

    SrtConfigurePost(sock, m_options, &failures);


    if (!failures.empty())
    {
        if (Verbose::on)
        {
            Verb() << "WARNING: failed to set options: ";
            copy(failures.begin(), failures.end(), ostream_iterator<string>(*Verbose::cverb, ", "));
            Verb();
        }
    }

    return 0;
}

int SrtCommon::ConfigurePre(SRTSOCKET sock)
{
    int result = 0;

    int no = 0;
    if (!m_tsbpdmode)
    {
        result = srt_setsockopt(sock, 0, SRTO_TSBPDMODE, &no, sizeof no);
        if (result == -1)
            return result;
    }

    // Let's pretend async mode is set this way.
    // This is for asynchronous connect.
    int maybe = m_blocking_mode;
    result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &maybe, sizeof maybe);
    if (result == -1)
        return result;

    // host is only checked for emptiness and depending on that the connection mode is selected.
    // Here we are not exactly interested with that information.
    vector<string> failures;

    // NOTE: here host = "", so the 'connmode' will be returned as LISTENER always,
    // but it doesn't matter here. We don't use 'connmode' for anything else than
    // checking for failures.
    SocketOption::Mode conmode = SrtConfigurePre(sock, "",  m_options, &failures);

    if (conmode == SocketOption::FAILURE)
    {
        if (Verbose::on )
        {
            Verb() << "WARNING: failed to set options: ";
            copy(failures.begin(), failures.end(), ostream_iterator<string>(*Verbose::cverb, ", "));
            Verb();
        }

        return SRT_ERROR;
    }

    return 0;
}

void SrtCommon::SetupAdapter(const string& host, int port)
{
    Verb() << "Binding the caller socket to " << host << ":" << port << " ...";
    auto lsa = CreateAddr(host, port);
    int stat = srt_bind(m_sock, lsa.get(), sizeof lsa);
    if (stat == SRT_ERROR)
        Error("srt_bind");
}

void SrtCommon::OpenClient(string host, int port)
{
    PrepareClient();

    if (m_outgoing_port || m_adapter != "")
    {
        SetupAdapter(m_adapter, m_outgoing_port);
    }

    ConnectClient(host, port);
}

void SrtCommon::PrepareClient()
{
    m_sock = srt_create_socket();
    if (m_sock == SRT_ERROR)
        Error("srt_create_socket");

    int stat = ConfigurePre(m_sock);
    if (stat == SRT_ERROR)
        Error("ConfigurePre");

    if (!m_blocking_mode)
    {
        srt_conn_epoll = AddPoller(m_sock, SRT_EPOLL_CONNECT | SRT_EPOLL_ERR);
    }

}

#if ENABLE_BONDING
void TransmitGroupSocketConnect(void* srtcommon, SRTSOCKET sock, int error, const sockaddr* /*peer*/, int token)
{
    SrtCommon* that = (SrtCommon*)srtcommon;

    if (error == SRT_SUCCESS)
    {
        return; // nothing to do for a successful socket
    }

#ifdef PLEASE_LOG
    applog.Debug("connect callback: error on @", sock, " erc=", error, " token=", token);
#endif

    /* Example: identify by target address
    sockaddr_any peersa = peer;
    sockaddr_any agentsa;
    bool haveso = (srt_getsockname(sock, agentsa.get(), &agentsa.len) != -1);
    */

    for (auto& n: that->m_group_nodes)
    {
        if (n.token != -1 && n.token == token)
        {
            n.error = error;
            n.reason = srt_getrejectreason(sock);
            return;
        }

        /*

        bool isso = haveso && !n.source.empty();
        if (n.target == peersa && (!isso || n.source.equal_address(agentsa)))
        {
            Verb() << " (by target)" << VerbNoEOL;
            n.error = error;
            n.reason = srt_getrejectreason(sock);
            return;
        }
        */
    }

    Verb() << " IPE: LINK NOT FOUND???]";
}

SRT_GROUP_TYPE ResolveGroupType(const string& name)
{
    static struct
    {
        string name;
        SRT_GROUP_TYPE type;
    } table [] {
#define E(n) {#n, SRT_GTYPE_##n}
        E(BROADCAST),
        E(BACKUP)

#undef E
    };

    typedef int charxform(int c);

    string uname;
    transform(name.begin(), name.end(), back_inserter(uname), (charxform*)(&toupper));

    for (auto& x: table)
        if (x.name == uname)
            return x.type;

    return SRT_GTYPE_UNDEFINED;
}

void SrtCommon::OpenGroupClient()
{
    SRT_GROUP_TYPE type = ResolveGroupType(m_group_type);
    if (type == SRT_GTYPE_UNDEFINED)
    {
        Error("With //group, type='" + m_group_type + "' undefined");
    }

    m_sock = srt_create_group(type);
    if (m_sock == -1)
        Error("srt_create_group");

    srt_connect_callback(m_sock, &TransmitGroupSocketConnect, this);

    int stat = -1;
    if (m_group_config != "")
    {
        Verb() << "Ignoring setting group config: '" << m_group_config;
    }

    stat = ConfigurePre(m_sock);

    if ( stat == SRT_ERROR )
        Error("ConfigurePre");

    if (!m_blocking_mode)
    {
        // Note: here the GROUP is added to the poller.
        srt_conn_epoll = AddPoller(m_sock, SRT_EPOLL_CONNECT | SRT_EPOLL_ERR);
    }

    // Don't check this. Should this fail, the above would already.

    // XXX Now do it regardless whether it's blocking or non-blocking
    // mode - reading from group is currently manually from every socket.
    srt_epoll = srt_epoll_create();

    // ConnectClient can't be used here, the code must
    // be more-less repeated. In this case the situation
    // that not all connections can be established is tolerated,
    // the only case of error is when none of the connections
    // can be established.

    bool any_node = false;

    Verb() << "REDUNDANT connections with " << m_group_nodes.size() << " nodes:";

    if (m_group_data.empty())
        m_group_data.resize(1);

    vector<SRT_SOCKGROUPCONFIG> targets;
    int namelen = sizeof (sockaddr_any);

    Verb() << "Connecting to nodes:";
    int i = 1;
    for (Connection& c: m_group_nodes)
    {
        auto sa = CreateAddr(c.host, c.port);
        c.target = sa;
        Verb() << "\t[" << c.token << "] " << c.host << ":" << c.port << VerbNoEOL;
        vector<string> extras;
        if (c.weight)
            extras.push_back(Sprint("weight=", c.weight));

        if (!c.source.empty())
            extras.push_back("source=" + c.source.str());

        if (!extras.empty())
        {
            Verb() << "?" << extras[0] << VerbNoEOL;
            for (size_t i = 1; i < extras.size(); ++i)
                Verb() << "&" << extras[i] << VerbNoEOL;
        }

        Verb();
        ++i;
        const sockaddr* source = c.source.empty() ? nullptr : c.source.get();
        SRT_SOCKGROUPCONFIG gd = srt_prepare_endpoint(source, sa.get(), namelen);
        gd.weight = c.weight;
        gd.config = c.options;

        targets.push_back(gd);
    }

    ::transmit_throw_on_interrupt = true;
    for (;;) // REPEATABLE BLOCK
    {
Connect_Again:
        Verb() << "Waiting for group connection... " << VerbNoEOL;

        int fisock = srt_connect_group(m_sock, targets.data(), int(targets.size()));

        if (fisock == SRT_ERROR)
        {
            // Complete the error information for every member
            ostringstream out;
            set<int> reasons;
            for (Connection& c: m_group_nodes)
            {
                if (c.error != SRT_SUCCESS)
                {
                    out << "[" << c.token << "] " << c.host << ":" << c.port;
                    if (!c.source.empty())
                        out << "[[" << c.source.str() << "]]";
                    out << ": " << srt_strerror(c.error, 0) << ": " << srt_rejectreason_str(c.reason) << endl;
                }
                reasons.insert(c.reason);
            }

            if (transmit_retry_connect && (transmit_retry_always || (reasons.size() == 1 && *reasons.begin() == SRT_REJ_TIMEOUT)))
            {
                if (transmit_retry_connect != -1)
                    --transmit_retry_connect;

                Verb() << "...all links timeout, retrying (" << transmit_retry_connect << ")...";
                continue;
            }

            Error("srt_connect_group, nodes:\n" + out.str());
        }
        else
        {
            Verb() << "[ASYNC] will wait..." << VerbNoEOL;
        }

        break;
    }

    if (m_blocking_mode)
    {
        Verb() << "SUCCESSFUL";
    }
    else
    {
        Verb() << "INITIATED [ASYNC]";
    }

    // Configuration change applied on a group should
    // spread the setting on all sockets.
    ConfigurePost(m_sock);

    for (size_t i = 0; i < targets.size(); ++i)
    {
        // As m_group_nodes is simply transformed into 'targets',
        // one index can be used to index them all. You don't
        // have to check if they have equal addresses because they
        // are equal by definition.
        if (targets[i].id != -1 && targets[i].errorcode == SRT_SUCCESS)
        {
            m_group_nodes[i].socket = targets[i].id;
        }
    }

    // Now check which sockets were successful, only those
    // should be added to epoll.
    size_t size = m_group_data.size();
    stat = srt_group_data(m_sock, m_group_data.data(), &size);
    if (stat == -1 && size > m_group_data.size())
    {
        // Just too small buffer. Resize and continue.
        m_group_data.resize(size);
        stat = srt_group_data(m_sock, m_group_data.data(), &size);
    }

    if (stat == -1)
    {
        Error("srt_group_data");
    }
    m_group_data.resize(size);

    for (size_t i = 0; i < m_group_nodes.size(); ++i)
    {
        SRTSOCKET insock = m_group_nodes[i].socket;
        if (insock == -1)
        {
            Verb() << "TARGET '" << sockaddr_any(targets[i].peeraddr).str() << "' connection failed.";
            continue;
        }

        // Have socket, store it into the group socket array.
        any_node = true;
    }

    if (!any_node)
        Error("All connections failed");

    // Wait for REAL connected state if nonblocking mode, for AT LEAST one node.
    if (!m_blocking_mode)
    {
        Verb() << "[ASYNC] " << VerbNoEOL;

        // SPIN-WAITING version. Don't use it unless you know what you're doing.
        // SpinWaitAsync();

        // Socket readiness for connection is checked by polling on WRITE allowed sockets.
        int len1 = 2, len2 = 2;
        SRTSOCKET ready_conn[2], ready_err[2];
        if (srt_epoll_wait(srt_conn_epoll,
                    ready_err, &len2,
                    ready_conn, &len1,
                    -1, // Wait infinitely
                    NULL, NULL,
                    NULL, NULL) != -1)
        {
            Verb() << "[C]" << VerbNoEOL;
            for (int i = 0; i < len1; ++i)
                Verb() << " " << ready_conn[i] << VerbNoEOL;
            Verb() << "[E]" << VerbNoEOL;
            for (int i = 0; i < len2; ++i)
                Verb() << " " << ready_err[i] << VerbNoEOL;

            Verb() << "";

            // We are waiting for one entity to be ready so it's either
            // in one or the other
            if (find(ready_err, ready_err+len2, m_sock) != ready_err+len2)
            {
                Verb() << "[EPOLL: " << len2 << " entities FAILED]";
                // Complete the error information for every member
                ostringstream out;
                set<int> reasons;
                for (Connection& c: m_group_nodes)
                {
                    if (c.error != SRT_SUCCESS)
                    {
                        out << "[" << c.token << "] " << c.host << ":" << c.port;
                        if (!c.source.empty())
                            out << "[[" << c.source.str() << "]]";
                        out << ": " << srt_strerror(c.error, 0) << ": " << srt_rejectreason_str(c.reason) << endl;
                    }
                    reasons.insert(c.reason);
                }

                if (transmit_retry_connect && (transmit_retry_always || (reasons.size() == 1 && *reasons.begin() == SRT_REJ_TIMEOUT)))
                {
                    if (transmit_retry_connect != -1)
                        --transmit_retry_connect;


                    Verb() << "...all links timeout, retrying NOW (" << transmit_retry_connect << ")...";
                    goto Connect_Again;
                }

                Error("srt_connect_group, nodes:\n" + out.str());
            }
            else if (find(ready_conn, ready_conn+len1, m_sock) != ready_conn+len1)
            {
                Verb() << "[EPOLL: " << len1 << " entities] " << VerbNoEOL;
            }
            else
            {
                Error("Group: SPURIOUS epoll readiness");
            }
        }
        else
        {
            Error("srt_epoll_wait");
        }
    }

    stat = ConfigurePost(m_sock);
    if (stat == -1)
    {
        // This kind of error must reject the whole operation.
        // Usually you'll get this error on the first socket,
        // and doing this on the others would result in the same.
        Error("ConfigurePost");
    }

    ::transmit_throw_on_interrupt = false;

    Verb() << "Group connection report:";
    for (auto& d: m_group_data)
    {
        // id, status, result, peeraddr
        Verb() << "@" << d.id << " <" << SockStatusStr(d.sockstate) << "> (=" << d.result << ") PEER:"
            << sockaddr_any((sockaddr*)&d.peeraddr, sizeof d.peeraddr).str();
    }

    // Prepare group data for monitoring the group status.
    m_group_data.resize(m_group_nodes.size());
}
#endif

/*
   This may be used sometimes for testing, but it's nonportable.
   void SrtCommon::SpinWaitAsync()
   {
   static string udt_status_names [] = {
   "INIT" , "OPENED", "LISTENING", "CONNECTING", "CONNECTED", "BROKEN", "CLOSING", "CLOSED", "NONEXIST"
   };

   for (;;)
   {
   SRT_SOCKSTATUS state = srt_getsockstate(m_sock);
   if (int(state) < SRTS_CONNECTED)
   {
   if (Verbose::on)
   Verb() << state;
   usleep(250000);
   continue;
   }
   else if (int(state) > SRTS_CONNECTED)
   {
   Error("UDT::connect status=" + udt_status_names[state]);
   }

   return;
   }
   }
 */

struct TransmitErrorReason
{
    int error;
    int reason;
};

static std::map<SRTSOCKET, TransmitErrorReason> transmit_error_storage;

static void TransmitConnectCallback(void*, SRTSOCKET socket, int errorcode, const sockaddr* /*peer*/, int /*token*/)
{
    int reason = srt_getrejectreason(socket);
    transmit_error_storage[socket] = TransmitErrorReason { errorcode, reason };
    Verb() << "[Connection error reported on @" << socket << "]";
}

void SrtCommon::ConnectClient(string host, int port)
{
    auto sa = CreateAddr(host, port);
    Verb() << "Connecting to " << host << ":" << port << " ... " << VerbNoEOL;

    if (!m_blocking_mode)
    {
        srt_connect_callback(m_sock, &TransmitConnectCallback, 0);
    }

    int stat = -1;
    for (;;)
    {
        ::transmit_throw_on_interrupt = true;
        stat = srt_connect(m_sock, sa.get(), sizeof sa);
        ::transmit_throw_on_interrupt = false;
        if (stat == SRT_ERROR)
        {
            int reason = srt_getrejectreason(m_sock);
#if PLEASE_LOG
            LOGP(applog.Error, "ERROR reported by srt_connect - closing socket @", m_sock,
                    " reject reason: ", reason, ": ", srt_rejectreason_str(reason));
#endif
            if (transmit_retry_connect && (transmit_retry_always || reason == SRT_REJ_TIMEOUT))
            {
                if (transmit_retry_connect != -1)
                    --transmit_retry_connect;

                Verb() << "...timeout, retrying (" << transmit_retry_connect << ")...";
                continue;
            }

            srt_close(m_sock);
            Error("srt_connect", reason);
        }
        break;
    }

    // Wait for REAL connected state if nonblocking mode
    if (!m_blocking_mode)
    {
        Verb() << "[ASYNC] " << VerbNoEOL;

        // SPIN-WAITING version. Don't use it unless you know what you're doing.
        // SpinWaitAsync();

        // Socket readiness for connection is checked by polling on WRITE allowed sockets.
        int lenc = 2, lene = 2;
        SRTSOCKET ready_connect[2], ready_error[2];
        if (srt_epoll_wait(srt_conn_epoll, ready_error, &lene, ready_connect, &lenc, -1, 0, 0, 0, 0) != -1)
        {
            // We should have just one socket, so check whatever socket
            // is in the transmit_error_storage.
            if (!transmit_error_storage.empty())
            {
                Verb() << "[CALLBACK(error): " << VerbNoEOL;
                int error, reason;
                bool failed = false;
                for (pair<const SRTSOCKET, TransmitErrorReason>& e: transmit_error_storage)
                {
                    Verb() << "{@" << e.first << " error=" << e.second.error
                        << " reason=" << e.second.reason << "} " << VerbNoEOL;
                    error = e.second.error;
                    reason = e.second.reason;
                    if (error != SRT_SUCCESS)
                        failed = true;
                }
                Verb() << "]";
                transmit_error_storage.clear();
                if (failed)
                    Error("srt_connect(async/cb)", reason, error);
            }

            if (lene > 0)
            {
                Verb() << "[EPOLL(error): " << lene << " sockets]";
                int reason = srt_getrejectreason(ready_error[0]);
                Error("srt_connect(async)", reason, SRT_ECONNREJ);
            }
            Verb() << "[EPOLL: " << lenc << " sockets] " << VerbNoEOL;
        }
        else
        {
            transmit_error_storage.clear();
            Error("srt_epoll_wait(srt_conn_epoll)");
        }

        transmit_error_storage.clear();
    }

    Verb() << " connected.";
    stat = ConfigurePost(m_sock);
    if (stat == SRT_ERROR)
        Error("ConfigurePost");
}

void SrtCommon::Error(string src, int reason, int force_result)
{
    int errnov = 0;
    const int result = force_result == 0 ? srt_getlasterror(&errnov) : force_result;
    if (result == SRT_SUCCESS)
    {
        cerr << "\nERROR (app): " << src << endl;
        throw std::runtime_error(src);
    }
    string message = srt_strerror(result, errnov);
    if (result == SRT_ECONNREJ)
    {
        if ( Verbose::on )
            Verb() << "FAILURE\n" << src << ": [" << result << "] "
                << "Connection rejected: [" << int(reason) << "]: "
                << srt_rejectreason_str(reason);
        else
            cerr << "\nERROR #" << result
                << ": Connection rejected: [" << int(reason) << "]: "
                << srt_rejectreason_str(reason);
    }
    else
    {
        if ( Verbose::on )
        Verb() << "FAILURE\n" << src << ": [" << result << "." << errnov << "] " << message;
        else
        cerr << "\nERROR #" << result << "." << errnov << ": " << message << endl;
    }

    throw TransmissionError("error: " + src + ": " + message);
}

void SrtCommon::SetupRendezvous(string adapter, string host, int port)
{
    sockaddr_any target = CreateAddr(host, port);
    if (target.family() == AF_UNSPEC)
    {
        Error("Unable to resolve target host: " + host);
    }

    bool yes = true;
    srt_setsockopt(m_sock, 0, SRTO_RENDEZVOUS, &yes, sizeof yes);

    const int outport = m_outgoing_port ? m_outgoing_port : port;

    // Prefer the same IPv as target host
    auto localsa = CreateAddr(adapter, outport, target.family());
    string showhost = adapter;
    if (showhost == "")
        showhost = "ANY";
    if (target.family() == AF_INET6)
        showhost = "[" + showhost + "]";
    Verb() << "Binding rendezvous: " << showhost << ":" << outport << " ...";
    int stat = srt_bind(m_sock, localsa.get(), localsa.size());
    if (stat == SRT_ERROR)
    {
        srt_close(m_sock);
        Error("srt_bind");
    }
}

void SrtCommon::Close()
{
#if PLEASE_LOG
        extern srt_logging::Logger applog;
        LOGP(applog.Error, "CLOSE requested - closing socket @", m_sock);
#endif
    bool any = false;
    bool yes = true;
    if (m_sock != SRT_INVALID_SOCK)
    {
        Verb() << "SrtCommon: DESTROYING CONNECTION, closing socket (rt%" << m_sock << ")...";
        srt_setsockflag(m_sock, SRTO_SNDSYN, &yes, sizeof yes);
        srt_close(m_sock);
        any = true;
    }

    if (m_bindsock != SRT_INVALID_SOCK)
    {
        Verb() << "SrtCommon: DESTROYING SERVER, closing socket (ls%" << m_bindsock << ")...";
        // Set sndsynchro to the socket to synch-close it.
        srt_setsockflag(m_bindsock, SRTO_SNDSYN, &yes, sizeof yes);
        srt_close(m_bindsock);
        any = true;
    }

    if (any)
        Verb() << "SrtCommon: ... done.";
}

SrtCommon::~SrtCommon()
{
    Close();
}

#if ENABLE_BONDING
void SrtCommon::UpdateGroupStatus(const SRT_SOCKGROUPDATA* grpdata, size_t grpdata_size)
{
    if (!grpdata)
    {
        // This happens when you passed too small array. Treat this as error and stop.
        cerr << "ERROR: broadcast group update reports " << grpdata_size
            << " existing sockets, but app registerred only " << m_group_nodes.size() << endl;
        Error("Too many unpredicted sockets in the group");
    }

    // Clear the active flag in all nodes so that they are reactivated
    // if they are in the group list, REGARDLESS OF THE STATUS. We need to
    // see all connections that are in the nodes, but not in the group,
    // and this one would have to be activated.
    const SRT_SOCKGROUPDATA* gend = grpdata + grpdata_size;
    for (auto& n: m_group_nodes)
    {
        bool active = (find_if(grpdata, gend,
                    [&n] (const SRT_SOCKGROUPDATA& sg) { return sg.id == n.socket; }) != gend);
        if (!active)
            n.socket = SRT_INVALID_SOCK;
    }

    // Note: sockets are not necessarily in the same order. Find
    // the socket by id.
    for (size_t i = 0; i < grpdata_size; ++i)
    {
        const SRT_SOCKGROUPDATA& d = grpdata[i];
        SRTSOCKET id = d.id;

        SRT_SOCKSTATUS status = d.sockstate;
        int result = d.result;
        SRT_MEMBERSTATUS mstatus = d.memberstate;

        if (result != -1 && status == SRTS_CONNECTED)
        {
            // Short report with the state.
            Verb() << "G@" << id << "<" << MemberStatusStr(mstatus) << "> " << VerbNoEOL;
            continue;
        }
        // id, status, result, peeraddr
        Verb() << "\n\tG@" << id << " <" << SockStatusStr(status) << "/" << MemberStatusStr(mstatus) << "> (=" << result << ") PEER:"
            << sockaddr_any((sockaddr*)&d.peeraddr, sizeof d.peeraddr).str() << VerbNoEOL;

        if (status >= SRTS_BROKEN)
        {
            Verb() << "NOTE: socket @" << id << " is pending for destruction, waiting for it.";
        }
    }

    // This was only informative. Now we check all nodes if they
    // are not active

    int i = 1;
    for (auto& n: m_group_nodes)
    {
        if (n.error != SRT_SUCCESS)
        {
            Verb() << "[" << i << "] CONNECTION FAILURE to '" << n.host << ":" << n.port << "': "
                << srt_strerror(n.error, 0) << ":" << srt_rejectreason_str(n.reason);
        }

        // Check which nodes are no longer active and activate them.
        if (n.socket != SRT_INVALID_SOCK)
            continue;

        auto sa = CreateAddr(n.host, n.port);
        Verb() << "[" << i << "] RECONNECTING to node " << n.host << ":" << n.port << " ... " << VerbNoEOL;
        ++i;

        n.error = SRT_SUCCESS;
        n.reason = SRT_REJ_UNKNOWN;

        const sockaddr* source = n.source.empty() ? nullptr : n.source.get();
        SRT_SOCKGROUPCONFIG gd = srt_prepare_endpoint(source, sa.get(), sa.size());
        gd.weight = n.weight;
        gd.config = n.options;
        gd.token = n.token;

        int fisock = srt_connect_group(m_sock, &gd, 1);
        if (fisock == SRT_ERROR)
        {
            // Whatever. Skip the node.
            Verb() << "FAILED: ";
        }
        else
        {
            // Have socket, store it into the group socket array.
            n.socket = gd.id;
        }
    }
}
#endif

SrtSource::SrtSource(string host, int port, std::string path, const map<string,string>& par)
{
    Init(host, port, path, par, SRT_EPOLL_IN);
    ostringstream os;
    os << host << ":" << port;
    hostport_copy = os.str();
}

static void PrintSrtStats(SRTSOCKET sock, bool clr, bool bw, bool stats)
{
    CBytePerfMon perf;
    // clear only if stats report is to be read
    srt_bstats(sock, &perf, clr);

    if (bw)
        cout << transmit_stats_writer->WriteBandwidth(perf.mbpsBandwidth);
    if (stats)
        cout << transmit_stats_writer->WriteStats(sock, perf);
}


#ifdef SRT_OLD_APP_READER

// NOTE: 'output' is expected to be EMPTY here.
bool SrtSource::GroupCheckPacketAhead(bytevector& output)
{
    bool status = false;
    vector<SRTSOCKET> past_ahead;

    // This map no longer maps only ahead links.
    // Here are all links, and whether ahead, it's defined by the sequence.
    for (auto i = m_group_positions.begin(); i != m_group_positions.end(); ++i)
    {
        // i->first: socket ID
        // i->second: ReadPos { sequence, packet }
        // We are not interested with the socket ID because we
        // aren't going to read from it - we have the packet already.
        ReadPos& a = i->second;

        int seqdiff = CSeqNo::seqcmp(a.sequence, m_group_seqno);
        if ( seqdiff == 1)
        {
            // The very next packet. Return it.
            m_group_seqno = a.sequence;
            Verb() << " (SRT group: ahead delivery %" << a.sequence << " from @" << i->first << ")";
            swap(output, a.packet);
            status = true;
        }
        else if (seqdiff < 1 && !a.packet.empty())
        {
            Verb() << " (@" << i->first << " dropping collected ahead %" << a.sequence << ")";
            a.packet.clear();
        }
        // In case when it's >1, keep it in ahead
    }

    return status;
}

static string DisplayEpollResults(const std::set<SRTSOCKET>& sockset, std::string prefix)
{
    typedef set<SRTSOCKET> fset_t;
    ostringstream os;
    os << prefix << " ";
    for (fset_t::const_iterator i = sockset.begin(); i != sockset.end(); ++i)
    {
        os << "@" << *i << " ";
    }

    return os.str();
}

bytevector SrtSource::GroupRead(size_t chunk)
{
    // Read the current group status. m_sock is here the group id.
    bytevector output;

    // Later iteration over it might be less efficient than
    // by vector, but we'll also often try to check a single id
    // if it was ever seen broken, so that it's skipped.
    set<SRTSOCKET> broken;

RETRY_READING:

    size_t size = m_group_data.size();
    int stat = srt_group_data(m_sock, m_group_data.data(), &size);
    if (stat == -1 && size > m_group_data.size())
    {
        // Just too small buffer. Resize and continue.
        m_group_data.resize(size);
        stat = srt_group_data(m_sock, m_group_data.data(), &size);
    }
    else
    {
        // Downsize if needed.
        m_group_data.resize(size);
    }

    if (stat == -1) // Also after the above fix
    {
        Error(UDT::getlasterror(), "FAILURE when reading group data");
    }

    if (size == 0)
    {
        Error("No sockets in the group - disconnected");
    }

    bool connected = false;
    for (auto& d: m_group_data)
    {
        if (d.status == SRTS_CONNECTED)
        {
            connected = true;
            break;
        }
    }
    if (!connected)
    {
        Error("All sockets in the group disconnected");
    }

    if (Verbose::on)
    {
        for (auto& d: m_group_data)
        {
            if (d.status != SRTS_CONNECTED)
                // id, status, result, peeraddr
                Verb() << "@" << d.id << " <" << SockStatusStr(d.status) << "> (=" << d.result << ") PEER:"
                    << sockaddr_any((sockaddr*)&d.peeraddr, sizeof d.peeraddr).str();
        }
    }

    // Check first the ahead packets if you have any to deliver.
    if (m_group_seqno != -1 && !m_group_positions.empty())
    {
        bytevector ahead_packet;

        // This function also updates the group sequence pointer.
        if (GroupCheckPacketAhead(ahead_packet))
            return move(ahead_packet);
    }

    // LINK QUALIFICATION NAMES:
    //
    // HORSE: Correct link, which delivers the very next sequence.
    // Not necessarily this link is currently active.
    //
    // KANGAROO: Got some packets dropped and the sequence number
    // of the packet jumps over the very next sequence and delivers
    // an ahead packet.
    //
    // ELEPHANT: Is not ready to read, while others are, or reading
    // up to the current latest delivery sequence number does not
    // reach this sequence and the link becomes non-readable earlier.

    // The above condition has ruled out one kangaroo and turned it
    // into a horse.

    // Below there's a loop that will try to extract packets. Kangaroos
    // will be among the polled ones because skipping them risks that
    // the elephants will take over the reading. Links already known as
    // elephants will be also polled in an attempt to revitalize the
    // connection that experienced just a short living choking.
    //
    // After polling we attempt to read from every link that reported
    // read-readiness and read at most up to the sequence equal to the
    // current delivery sequence.

    // Links that deliver a packet below that sequence will be retried
    // until they deliver no more packets or deliver the packet of
    // expected sequence. Links that don't have a record in m_group_positions
    // and report readiness will be always read, at least to know what
    // sequence they currently stand on.
    //
    // Links that are already known as kangaroos will be polled, but
    // no reading attempt will be done. If after the reading series
    // it will turn out that we have no more horses, the slowest kangaroo
    // will be "advanced to a horse" (the ahead link with a sequence
    // closest to the current delivery sequence will get its sequence
    // set as current delivered and its recorded ahead packet returned
    // as the read packet).

    // If we find at least one horse, the packet read from that link
    // will be delivered. All other link will be just ensured update
    // up to this sequence number, or at worst all available packets
    // will be read. In this case all kangaroos remain kangaroos,
    // until the current delivery sequence m_group_seqno will be lifted
    // to the sequence recorded for these links in m_group_positions,
    // during the next time ahead check, after which they will become
    // horses.

    Verb() << "E(" << srt_epoll << ") " << VerbNoEOL;

    for (size_t i = 0; i < size; ++i)
    {
        SRT_SOCKGROUPDATA& d = m_group_data[i];
        if (d.status == SRTS_CONNECTING)
        {
            Verb() << "@" << d.id << "<pending> " << VerbNoEOL;
            int modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
            srt_epoll_add_usock(srt_epoll, d.id, &modes);
            continue; // don't read over a failed or pending socket
        }

        if (d.status >= SRTS_BROKEN)
        {
            broken.insert(d.id);
        }

        if (broken.count(d.id))
        {
            Verb() << "@" << d.id << "<broken> " << VerbNoEOL;
            continue;
        }

        if (d.status != SRTS_CONNECTED)
        {
            Verb() << "@" << d.id << "<idle:" << SockStatusStr(d.status) << "> " << VerbNoEOL;
            // Sockets in this state are ignored. We are waiting until it
            // achieves CONNECTING state, then it's added to write.
            continue;
        }

        // Don't skip packets that are ahead because if we have a situation
        // that all links are either "elephants" (do not report read readiness)
        // and "kangaroos" (have already delivered an ahead packet) then
        // omitting kangaroos will result in only elephants to be polled for
        // reading. Elephants, due to the strict timing requirements and
        // ensurance that TSBPD on every link will result in exactly the same
        // delivery time for a packet of given sequence, having an elephant
        // and kangaroo in one cage means that the elephant is simply a broken
        // or half-broken link (the data are not delivered, but it will get
        // repaired soon, enough for SRT to maintain the connection, but it
        // will still drop packets that didn't arrive in time), in both cases
        // it may potentially block the reading for an indefinite time, while
        // simultaneously a kangaroo might be a link that got some packets
        // dropped, but then it's still capable to deliver packets on time.

        // Note also that about the fact that some links turn out to be
        // elephants we'll learn only after we try to poll and read them.

        // Note that d.id might be a socket that was previously being polled
        // on write, when it's attempting to connect, but now it's connected.
        // This will update the socket with the new event set.

        int modes = SRT_EPOLL_IN | SRT_EPOLL_ERR;
        srt_epoll_add_usock(srt_epoll, d.id, &modes);
        Verb() << "@" << d.id << "[READ] " << VerbNoEOL;
    }

    Verb() << "";

    // Here we need to make an additional check.
    // There might be a possibility that all sockets that
    // were added to the reader group, are ahead. At least
    // surely we don't have a situation that any link contains
    // an ahead-read subsequent packet, because GroupCheckPacketAhead
    // already handled that case.
    //
    // What we can have is that every link has:
    // - no known seq position yet (is not registered in the position map yet)
    // - the position equal to the latest delivered sequence
    // - the ahead position

    // Now the situation is that we don't have any packets
    // waiting for delivery so we need to wait for any to report one.
    // XXX We support blocking mode only at the moment.
    // The non-blocking mode would need to simply check the readiness
    // with only immediate report, and read-readiness would have to
    // be done in background.

    SrtPollState sready;

    // Poll on this descriptor until reading is available, indefinitely.
    if (UDT::epoll_swait(srt_epoll, sready, -1) == SRT_ERROR)
    {
        Error(UDT::getlasterror(), "UDT::epoll_swait(srt_epoll, group)");
    }
    if (Verbose::on)
    {
        Verb() << "RDY: {"
            << DisplayEpollResults(sready.rd(), "[R]")
            << DisplayEpollResults(sready.wr(), "[W]")
            << DisplayEpollResults(sready.ex(), "[E]")
            << "} " << VerbNoEOL;

    }

    LOGC(applog.Debug, log << "epoll_swait: "
            << DisplayEpollResults(sready.rd(), "[R]")
            << DisplayEpollResults(sready.wr(), "[W]")
            << DisplayEpollResults(sready.ex(), "[E]"));

    typedef set<SRTSOCKET> fset_t;

    // Handle sockets of pending connection and with errors.
    broken = sready.ex();

    // We don't do anything about sockets that have been configured to
    // poll on writing (that is, pending for connection). What we need
    // is that the epoll_swait call exit on that fact. Probably if this
    // was the only socket reported, no broken and no read-ready, this
    // will later check on output if still empty, if so, repeat the whole
    // function. This write-ready socket will be there already in the
    // connected state and will be added to read-polling.

    // Ok, now we need to have some extra qualifications:
    // 1. If a socket has no registry yet, we read anyway, just
    // to notify the current position. We read ONLY ONE PACKET this time,
    // we'll worry later about adjusting it to the current group sequence
    // position.
    // 2. If a socket is already position ahead, DO NOT read from it, even
    // if it is ready.

    // The state of things whether we were able to extract the very next
    // sequence will be simply defined by the fact that `output` is nonempty.

    int32_t next_seq = m_group_seqno;

    // If this set is empty, it won't roll even once, therefore output
    // will be surely empty. This will be checked then same way as when
    // reading from every socket resulted in error.
    for (fset_t::const_iterator i = sready.rd().begin(); i != sready.rd().end(); ++i)
    {
        // Check if this socket is in aheads
        // If so, don't read from it, wait until the ahead is flushed.

        SRTSOCKET id = *i;
        ReadPos* p = nullptr;
        auto pe = m_group_positions.find(id);
        if (pe != m_group_positions.end())
        {
            p = &pe->second;
            // Possible results of comparison:
            // x < 0: the sequence is in the past, the socket should be adjusted FIRST
            // x = 0: the socket should be ready to get the exactly next packet
            // x = 1: the case is already handled by GroupCheckPacketAhead.
            // x > 1: AHEAD. DO NOT READ.
            int seqdiff = CSeqNo::seqcmp(p->sequence, m_group_seqno);
            if (seqdiff > 1)
            {
                Verb() << "EPOLL: @" << id << " %" << p->sequence << " AHEAD, not reading.";
                continue;
            }
        }


        // Read from this socket stubbornly, until:
        // - reading is no longer possible (AGAIN)
        // - the sequence difference is >= 1

        int fi = 1; // marker for Verb to display flushing
        for (;;)
        {
            bytevector data(chunk);
            SRT_MSGCTRL mctrl = srt_msgctrl_default;
            stat = srt_recvmsg2(id, data.data(), chunk, &mctrl);
            if (stat == SRT_ERROR)
            {
                if (fi == 0)
                {
                    if (Verbose::on)
                    {
                        if (p)
                        {
                            int32_t pktseq = p->sequence;
                            int seqdiff = CSeqNo::seqcmp(p->sequence, m_group_seqno);
                            Verb() << ". %" << pktseq << " " << seqdiff << ")";
                        }
                        else
                        {
                            Verb() << ".)";
                        }
                    }
                    fi = 1;
                }
                int err = srt_getlasterror(0);
                if (err == SRT_EASYNCRCV)
                {
                    // Do not treat this as spurious, just stop reading.
                    break;
                }
                Verb() << "Error @" << id << ": " << srt_getlasterror_str();
                broken.insert(id);
                break;
            }

            // NOTE: checks against m_group_seqno and decisions based on it
            // must NOT be done if m_group_seqno is -1, which means that we
            // are about to deliver the very first packet and we take its
            // sequence number as a good deal.

            // The order must be:
            // - check discrepancy
            // - record the sequence
            // - check ordering.
            // The second one must be done always, but failed discrepancy
            // check should exclude the socket from any further checks.
            // That's why the common check for m_group_seqno != -1 can't
            // embrace everything below.

            // We need to first qualify the sequence, just for a case
            if (m_group_seqno != -1 && abs(m_group_seqno - mctrl.pktseq) > CSeqNo::m_iSeqNoTH)
            {
                // This error should be returned if the link turns out
                // to be the only one, or set to the group data.
                // err = SRT_ESECFAIL;
                if (fi == 0)
                {
                    Verb() << ".)";
                    fi = 1;
                }
                Verb() << "Error @" << id << ": SEQUENCE DISCREPANCY: base=%" << m_group_seqno << " vs pkt=%" << mctrl.pktseq << ", setting ESECFAIL";
                broken.insert(id);
                break;
            }

            // Rewrite it to the state for a case when next reading
            // would not succeed. Do not insert the buffer here because
            // this is only required when the sequence is ahead; for that
            // it will be fixed later.
            if (!p)
            {
                p = &(m_group_positions[id] = ReadPos { mctrl.pktseq, {} });
            }
            else
            {
                p->sequence = mctrl.pktseq;
            }

            if (m_group_seqno != -1)
            {
                // Now we can safely check it.
                int seqdiff = CSeqNo::seqcmp(mctrl.pktseq, m_group_seqno);

                if (seqdiff <= 0)
                {
                    if (fi == 1)
                    {
                        Verb() << "(@" << id << " FLUSH:" << VerbNoEOL;
                        fi = 0;
                    }

                    Verb() << "." << VerbNoEOL;

                    // The sequence is recorded, the packet has to be discarded.
                    // That's all.
                    continue;
                }

                // Finish flush reporting if fallen into here
                if (fi == 0)
                {
                    Verb() << ". %" << mctrl.pktseq << " " << (-seqdiff) << ")";
                    fi = 1;
                }

                // Now we have only two possibilities:
                // seqdiff == 1: The very next sequence, we want to read and return the packet.
                // seqdiff > 1: The packet is ahead - record the ahead packet, but continue with the others.

                if (seqdiff > 1)
                {
                    Verb() << "@" << id << " %" << mctrl.pktseq << " AHEAD";
                    p->packet = move(data);
                    break; // Don't read from that socket anymore.
                }
            }

            // We have seqdiff = 1, or we simply have the very first packet
            // which's sequence is taken as a good deal. Update the sequence
            // and record output.

            if (!output.empty())
            {
                Verb() << "@" << id << " %" << mctrl.pktseq << " REDUNDANT";
                break;
            }


            Verb() << "@" << id << " %" << mctrl.pktseq << " DELIVERING";
            output = move(data);

            // Record, but do not update yet, until all sockets are handled.
            next_seq = mctrl.pktseq;
            break;
        }
    }

    // ready_len is only the length of currently reported
    // ready sockets, NOT NECESSARILY containing all sockets from the group.
    if (broken.size() == size)
    {
        // All broken
        Error("All sockets broken");
    }

    if (Verbose::on && !broken.empty())
    {
        Verb() << "BROKEN: " << Printable(broken) << " - removing";
    }

    // Now remove all broken sockets from aheads, if any.
    // Even if they have already delivered a packet.
    for (SRTSOCKET d: broken)
    {
        m_group_positions.erase(d);
        srt_close(d);
    }

    // May be required to be re-read.
    broken.clear();

    if (!output.empty())
    {
        // We have extracted something, meaning that we have the sequence shift.
        // Update it now and don't do anything else with the sockets.

        // Sanity check
        if (next_seq == -1)
        {
            Error("IPE: next_seq not set after output extracted!");
        }
        m_group_seqno = next_seq;
        return output;
    }

    // Check if we have any sockets left :D

    // Here we surely don't have any more HORSES,
    // only ELEPHANTS and KANGAROOS. Qualify them and
    // attempt to at least take advantage of KANGAROOS.

    // In this position all links are either:
    // - updated to the current position
    // - updated to the newest possible position available
    // - not yet ready for extraction (not present in the group)

    // If we haven't extracted the very next sequence position,
    // it means that we might only have the ahead packets read,
    // that is, the next sequence has been dropped by all links.

    if (!m_group_positions.empty())
    {
        // This might notify both lingering links, which didn't
        // deliver the required sequence yet, and links that have
        // the sequence ahead. Review them, and if you find at
        // least one packet behind, just wait for it to be ready.
        // Use again the waiting function because we don't want
        // the general waiting procedure to skip others.
        set<SRTSOCKET> elephants;

        // const because it's `typename decltype(m_group_positions)::value_type`
        pair<const SRTSOCKET, ReadPos>* slowest_kangaroo = nullptr;

        for (auto& sock_rp: m_group_positions)
        {
            // NOTE that m_group_seqno in this place wasn't updated
            // because we haven't successfully extracted anything.
            int seqdiff = CSeqNo::seqcmp(sock_rp.second.sequence, m_group_seqno);
            if (seqdiff < 0)
            {
                elephants.insert(sock_rp.first);
            }
            // If seqdiff == 0, we have a socket ON TRACK.
            else if (seqdiff > 0)
            {
                if (!slowest_kangaroo)
                {
                    slowest_kangaroo = &sock_rp;
                }
                else
                {
                    // Update to find the slowest kangaroo.
                    int seqdiff = CSeqNo::seqcmp(slowest_kangaroo->second.sequence, sock_rp.second.sequence);
                    if (seqdiff > 0)
                    {
                        slowest_kangaroo = &sock_rp;
                    }
                }
            }
        }

        // Note that if no "slowest_kangaroo" was found, it means
        // that we don't have kangaroos.
        if (slowest_kangaroo)
        {
            // We have a slowest kangaroo. Elephants must be ignored.
            // Best case, they will get revived, worst case they will be
            // soon broken.
            //
            // As we already have the packet delivered by the slowest
            // kangaroo, we can simply return it.

            m_group_seqno = slowest_kangaroo->second.sequence;
            Verb() << "@" << slowest_kangaroo->first << " %" << m_group_seqno << " KANGAROO->HORSE";
            swap(output, slowest_kangaroo->second.packet);
            return output;
        }

        // Here ALL LINKS ARE ELEPHANTS, stating that we still have any.
        if (Verbose::on)
        {
            if (!elephants.empty())
            {
                // If we don't have kangaroos, then simply reattempt to
                // poll all elephants again anyway (at worst they are all
                // broken and we'll learn about it soon).
                Verb() << "ALL LINKS ELEPHANTS. Re-polling.";
            }
            else
            {
                Verb() << "ONLY BROKEN WERE REPORTED. Re-polling.";
            }
        }
        goto RETRY_READING;
    }

    // We have checked so far only links that were ready to poll.
    // Links that are not ready should be re-checked.
    // Links that were not ready at the entrance should be checked
    // separately, and probably here is the best moment to do it.
    // After we make sure that at least one link is ready, we can
    // reattempt to read a packet from it.

    // Ok, so first collect all sockets that are in
    // connecting state, make a poll for connection.
    srt_epoll_clear_usocks(srt_epoll);
    bool have_connectors = false, have_ready = false;
    for (auto& d: m_group_data)
    {
        if (d.status < SRTS_CONNECTED)
        {
            // Not sure anymore if IN or OUT signals the connect-readiness,
            // but no matter. The signal will be cleared once it is used,
            // while it will be always on when there's anything ready to read.
            int modes = SRT_EPOLL_IN | SRT_EPOLL_OUT;
            srt_epoll_add_usock(srt_epoll, d.id, &modes);
            have_connectors = true;
        }
        else if (d.status == SRTS_CONNECTED)
        {
            have_ready = true;
        }
    }

    if (have_ready || have_connectors)
    {
        Verb() << "(still have: " << (have_ready ? "+" : "-") << "ready, "
            << (have_connectors ? "+" : "-") << "conenctors).";
        goto RETRY_READING;
    }

    if (have_ready)
    {
        Verb() << "(connected in the meantime)";
        // Some have connected in the meantime, don't
        // waste time on the pending ones.
        goto RETRY_READING;
    }

    if (have_connectors)
    {
        Verb() << "(waiting for pending connectors to connect)";
        // Wait here for them to be connected.
        vector<SRTSOCKET> sready;
        sready.resize(m_group_data.size());
        int ready_len = m_group_data.size();
        if (srt_epoll_wait(srt_epoll, sready.data(), &ready_len, 0, 0, -1, 0, 0, 0, 0) == SRT_ERROR)
        {
            Error("All sockets in the group disconnected");
        }

        goto RETRY_READING;
    }

    Error("No data extracted");
    return output; // Just a marker - this above function throws an exception
}

#endif

MediaPacket SrtSource::Read(size_t chunk)
{
    static size_t counter = 1;

    bool have_group SRT_ATR_UNUSED = !m_group_nodes.empty();

    bytevector data(chunk);
    // EXPERIMENTAL
#ifdef SRT_OLD_APP_READER
    if (have_group || m_listener_group)
    {
        data = GroupRead(chunk);
    }

    if (have_group)
    {
        // This is to be done for caller mode only
        UpdateGroupStatus(m_group_data.data(), m_group_data.size());
    }
#else

    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    bool ready = true;
    int stat;

    do
    {
#if ENABLE_BONDING
        if (have_group || m_listener_group)
        {
            mctrl.grpdata = m_group_data.data();
            mctrl.grpdata_size = m_group_data.size();
        }
#endif

        if (::transmit_int_state)
            Error("srt_recvmsg2: interrupted");

        ::transmit_throw_on_interrupt = true;
        stat = srt_recvmsg2(m_sock, data.data(), int(chunk), &mctrl);
        ::transmit_throw_on_interrupt = false;
        if (stat != SRT_ERROR)
        {
            ready = true;
        }
        else
        {
            int syserr = 0;
            int err = srt_getlasterror(&syserr);

            if (!m_blocking_mode)
            {
                // EAGAIN for SRT READING
                if (err == SRT_EASYNCRCV)
                {
Epoll_again:
                    Verb() << "AGAIN: - waiting for data by epoll(" << srt_epoll << ")...";
                    // Poll on this descriptor until reading is available, indefinitely.
                    int len = 2;
                    SRT_EPOLL_EVENT sready[2];
                    len = srt_epoll_uwait(srt_epoll, sready, len, -1);
                    if (len != -1)
                    {
                        Verb() << "... epoll reported ready " << len << " sockets";
                        // If the event was SRT_EPOLL_UPDATE, report it, and still wait.

                        bool any_read_ready = false;
                        vector<int> errored;
                        for (int i = 0; i < len; ++i)
                        {
                            if (sready[i].events & SRT_EPOLL_UPDATE)
                            {
                                Verb() << "... [BROKEN CONNECTION reported on @" << sready[i].fd << "]";
                            }

                            if (sready[i].events & SRT_EPOLL_IN)
                                any_read_ready = true;

                            if (sready[i].events & SRT_EPOLL_ERR)
                            {
                                errored.push_back(sready[i].fd);
                            }
                        }

                        if (!any_read_ready)
                        {
                            Verb() << " ... [NOT READ READY - AGAIN (" << errored.size() << " errored: " << Printable(errored) << ")]";
                            goto Epoll_again;
                        }

                        continue;
                    }
                    // If was -1, then passthru.
                }
            }
            else
            {
                // In blocking mode it uses a minimum of 1s timeout,
                // and continues only if interrupt not requested.
                if (!::transmit_int_state && (err == SRT_EASYNCRCV || err == SRT_ETIMEOUT))
                {
                    ready = false;
                    continue;
                }
            }
            Error("srt_recvmsg2");
        }

        if (stat == 0)
        {
            throw ReadEOF(hostport_copy);
        }
#if PLEASE_LOG
        extern srt_logging::Logger applog;
        LOGC(applog.Debug, log << "recv: #" << mctrl.msgno << " %" << mctrl.pktseq << "  "
                << BufferStamp(data.data(), stat) << " BELATED: " << ((srt_time_now()-mctrl.srctime)/1000.0) << "ms");
#endif

        Verb() << "(#" << mctrl.msgno << " %" << mctrl.pktseq << "  " << BufferStamp(data.data(), stat) << ") " << VerbNoEOL;
    }
    while (!ready);

    chunk = size_t(stat);
    if (chunk < data.size())
        data.resize(chunk);

    const bool need_bw_report    = transmit_bw_report    && int(counter % transmit_bw_report) == transmit_bw_report - 1;
    const bool need_stats_report = transmit_stats_report && counter % transmit_stats_report == transmit_stats_report - 1;

#if ENABLE_BONDING
    if (have_group) // Means, group with caller mode
    {
        UpdateGroupStatus(mctrl.grpdata, mctrl.grpdata_size);
        if (transmit_stats_writer && (need_stats_report || need_bw_report))
        {
            PrintSrtStats(m_sock, need_stats_report, need_bw_report, need_stats_report);
            for (size_t i = 0; i < mctrl.grpdata_size; ++i)
                PrintSrtStats(mctrl.grpdata[i].id, need_stats_report, need_bw_report, need_stats_report);
        }
    }
    else
#endif
    {
        if (transmit_stats_writer && (need_stats_report || need_bw_report))
        {
            PrintSrtStats(m_sock, need_stats_report, need_bw_report, need_stats_report);
        }
    }
#endif

    ++counter;

    return MediaPacket(data, mctrl.srctime);
}

SrtTarget::SrtTarget(std::string host, int port, std::string path, const std::map<std::string,std::string>& par)
{
    Init(host, port, path, par, SRT_EPOLL_OUT);
}


int SrtTarget::ConfigurePre(SRTSOCKET sock)
{
    int result = SrtCommon::ConfigurePre(sock);
    if (result == -1)
        return result;

    int yes = 1;
    // This is for the HSv4 compatibility; if both parties are HSv5
    // (min. version 1.2.1), then this setting simply does nothing.
    // In HSv4 this setting is obligatory; otherwise the SRT handshake
    // extension will not be done at all.
    result = srt_setsockopt(sock, 0, SRTO_SENDER, &yes, sizeof yes);
    if (result == -1)
        return result;

    return 0;
}

void SrtTarget::Write(const MediaPacket& data)
{
    static int counter = 1;
    ::transmit_throw_on_interrupt = true;

    // Check first if it's ready to write.
    // If not, wait indefinitely.
    if (!m_blocking_mode)
    {
Epoll_again:
        int len = 2;
        SRT_EPOLL_EVENT sready[2];
        len = srt_epoll_uwait(srt_epoll, sready, len, -1);
        if (len != -1)
        {
            bool any_write_ready = false;
            for (int i = 0; i < len; ++i)
            {
                if (sready[i].events & SRT_EPOLL_UPDATE)
                {
                    Verb() << "... [BROKEN CONNECTION reported on @" << sready[i].fd << "]";
                }

                if (sready[i].events & SRT_EPOLL_OUT)
                    any_write_ready = true;
            }

            if (!any_write_ready)
            {
                Verb() << " ... [NOT WRITE READY - AGAIN]";
                goto Epoll_again;
            }

            // Pass on, write ready.
        }
        else
        {
            Error("srt_epoll_uwait");
        }
    }

    SRT_MSGCTRL mctrl = srt_msgctrl_default;
#if ENABLE_BONDING
    bool have_group = !m_group_nodes.empty();
    if (have_group || m_listener_group)
    {
        mctrl.grpdata = m_group_data.data();
        mctrl.grpdata_size = m_group_data.size();
    }
#endif

    if (transmit_use_sourcetime)
    {
        mctrl.srctime = data.time;
    }

    int stat = srt_sendmsg2(m_sock, data.payload.data(), int(data.payload.size()), &mctrl);

    // For a socket group, the error is reported only
    // if ALL links from the group have failed to perform
    // the operation. If only one did, the result will be
    // visible in the status array.
    if (stat == SRT_ERROR)
        Error("srt_sendmsg");
    ::transmit_throw_on_interrupt = false;

    const bool need_bw_report    = transmit_bw_report    && int(counter % transmit_bw_report) == transmit_bw_report - 1;
    const bool need_stats_report = transmit_stats_report && counter % transmit_stats_report == transmit_stats_report - 1;

#if ENABLE_BONDING
    if (have_group)
    {
        // For listener group this is not necessary. The group information
        // is updated in mctrl.
        UpdateGroupStatus(mctrl.grpdata, mctrl.grpdata_size);
        if (transmit_stats_writer && (need_stats_report || need_bw_report))
        {
            PrintSrtStats(m_sock, need_stats_report, need_bw_report, need_stats_report);
            for (size_t i = 0; i < mctrl.grpdata_size; ++i)
                PrintSrtStats(mctrl.grpdata[i].id, need_stats_report, need_bw_report, need_stats_report);
        }
    }
    else
#endif
    {
        if (transmit_stats_writer && (need_stats_report || need_bw_report))
        {
            PrintSrtStats(m_sock, need_stats_report, need_bw_report, need_stats_report);
        }
    }

    Verb() << "(#" << mctrl.msgno << " %" << mctrl.pktseq << "  " << BufferStamp(data.payload.data(), data.payload.size()) << ") " << VerbNoEOL;

    ++counter;
}

SrtRelay::SrtRelay(std::string host, int port, std::string path, const std::map<std::string,std::string>& par)
{
    Init(host, port, path, par, SRT_EPOLL_IN | SRT_EPOLL_OUT);
}

SrtModel::SrtModel(string host, int port, map<string,string> par)
{
    InitParameters(host, "", par);
    if (m_mode == "caller")
        is_caller = true;
    else if (m_mode == "rendezvous")
        is_rend = true;
    else if (m_mode != "listener")
        throw std::invalid_argument("Wrong 'mode' attribute; expected: caller, listener, rendezvous");

    m_host = host;
    m_port = port;
}

void SrtModel::Establish(std::string& w_name)
{
    // This does connect or accept.
    // When this returned true, the caller should create
    // a new SrtSource or SrtTaget then call StealFrom(*this) on it.

    // If this is a connector and the peer doesn't have a corresponding
    // medium, it should send back a single byte with value 0. This means
    // that agent should stop connecting.

    if (is_rend)
    {
        OpenRendezvous(m_adapter, m_host, m_port);
    }
    else if (is_caller)
    {
        // Establish a connection

        PrepareClient();

        if (w_name != "")
        {
            Verb() << "Connect with requesting stream [" << w_name << "]";
            srt::setstreamid(m_sock, w_name);
        }
        else
        {
            Verb() << "NO STREAM ID for SRT connection";
        }

        if (m_outgoing_port || m_adapter != "")
        {
            Verb() << "Setting outgoing port: " << m_outgoing_port << " adapter:" << m_adapter;
            SetupAdapter(m_adapter, m_outgoing_port);
        }

        ConnectClient(m_host, m_port);

        if (m_outgoing_port == 0)
        {
            // Must rely on a randomly selected one. Extract the port
            // so that it will be reused next time.
            sockaddr_any s(AF_INET);
            int namelen = s.size();
            if (srt_getsockname(Socket(), (s.get()), (&namelen)) == SRT_ERROR)
            {
                Error("srt_getsockname");
            }

            m_outgoing_port = s.hport();
            Verb() << "Extracted outgoing port: " << m_outgoing_port;
        }
    }
    else
    {
        // Listener - get a socket by accepting.
        // Check if the listener is already created first
        if (Listener() == SRT_INVALID_SOCK)
        {
            Verb() << "Setting up listener: port=" << m_port << " backlog=5";
            PrepareListener(m_adapter, m_port, 5);
        }

        Verb() << "Accepting a client...";
        AcceptNewClient();
        // This rewrites m_sock with a new SRT socket ("accepted" socket)
        w_name = UDT::getstreamid(m_sock);
        Verb() << "... GOT CLIENT for stream [" << w_name << "]";
    }
}


template <class Iface> struct Srt;
template <> struct Srt<Source> { typedef SrtSource type; };
template <> struct Srt<Target> { typedef SrtTarget type; };
template <> struct Srt<Relay> { typedef SrtRelay type; };

template <class Iface>
Iface* CreateSrt(const string& host, int port, std::string path, const map<string,string>& par)
{ return new typename Srt<Iface>::type (host, port, path, par); }

MediaPacket ConsoleRead(size_t chunk)
{
    bytevector data(chunk);
    bool st = cin.read(data.data(), chunk).good();
    chunk = cin.gcount();
    if (chunk == 0 && !st)
        return bytevector();

    int64_t stime = 0;
    if (transmit_use_sourcetime)
        stime = srt_time_now();

    if (chunk < data.size())
        data.resize(chunk);
    if (data.empty())
        throw Source::ReadEOF("CONSOLE device");

    return MediaPacket(data, stime);
}

class ConsoleSource: public virtual Source
{
public:

    ConsoleSource()
    {
    }

    MediaPacket Read(size_t chunk) override
    {
        return ConsoleRead(chunk);
    }

    bool IsOpen() override { return cin.good(); }
    bool End() override { return cin.eof(); }
};

class ConsoleTarget: public virtual Target
{
public:

    ConsoleTarget()
    {
    }

    void Write(const MediaPacket& data) override
    {
        cout.write(data.payload.data(), data.payload.size());
    }

    bool IsOpen() override { return cout.good(); }
    bool Broken() override { return cout.eof(); }
};

class ConsoleRelay: public Relay, public ConsoleSource, public ConsoleTarget
{
public:
    ConsoleRelay() = default;

    bool IsOpen() override { return cin.good() && cout.good(); }
};

template <class Iface> struct Console;
template <> struct Console<Source> { typedef ConsoleSource type; };
template <> struct Console<Target> { typedef ConsoleTarget type; };
template <> struct Console<Relay> { typedef ConsoleRelay type; };

template <class Iface>
Iface* CreateConsole() { return new typename Console<Iface>::type (); }


// More options can be added in future.
SocketOption udp_options [] {
    { "iptos", IPPROTO_IP, IP_TOS, SocketOption::PRE, SocketOption::INT, nullptr },
    // IP_TTL and IP_MULTICAST_TTL are handled separately by a common option, "ttl".
    { "mcloop", IPPROTO_IP, IP_MULTICAST_LOOP, SocketOption::PRE, SocketOption::INT, nullptr }
};

static inline bool IsMulticast(in_addr adr)
{
    unsigned char* abytes = (unsigned char*)&adr.s_addr;
    unsigned char c = abytes[0];
    return c >= 224 && c <= 239;
}

void UdpCommon::Setup(string host, int port, map<string,string> attr)
{
    m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_sock == -1)
        Error(SysError(), "UdpCommon::Setup: socket");

    int yes = 1;
    ::setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);

    sadr = CreateAddr(host, port);

    bool is_multicast = false;
    if (sadr.family() == AF_INET)
    {
        if (attr.count("multicast"))
        {
            if (!IsMulticast(sadr.sin.sin_addr))
            {
                throw std::runtime_error("UdpCommon: requested multicast for a non-multicast-type IP address");
            }
            is_multicast = true;
        }
        else if (IsMulticast(sadr.sin.sin_addr))
        {
            is_multicast = true;
        }

        if (is_multicast)
        {
            ip_mreq mreq;
            sockaddr_any maddr (AF_INET);
            int opt_name;
            void* mreq_arg_ptr;
            socklen_t mreq_arg_size;

            adapter = attr.count("adapter") ? attr.at("adapter") : string();
            if (adapter == "")
            {
                Verb() << "Multicast: home address: INADDR_ANY:" << port;
                maddr.sin.sin_family = AF_INET;
                maddr.sin.sin_addr.s_addr = htonl(INADDR_ANY);
                maddr.sin.sin_port = htons(port); // necessary for temporary use
            }
            else
            {
                Verb() << "Multicast: home address: " << adapter << ":" << port;
                maddr = CreateAddr(adapter, port);
            }

            if (attr.count("source"))
            {
#ifdef IP_ADD_SOURCE_MEMBERSHIP
                ip_mreq_source mreq_ssm;
                /* this is an ssm.  we need to use the right struct and opt */
                opt_name = IP_ADD_SOURCE_MEMBERSHIP;
                mreq_ssm.imr_multiaddr.s_addr = sadr.sin.sin_addr.s_addr;
                mreq_ssm.imr_interface.s_addr = maddr.sin.sin_addr.s_addr;
                inet_pton(AF_INET, attr.at("source").c_str(), &mreq_ssm.imr_sourceaddr);
                mreq_arg_size = sizeof(mreq_ssm);
                mreq_arg_ptr = &mreq_ssm;
#else
                throw std::runtime_error("UdpCommon: source-filter multicast not supported by OS");
#endif
            }
            else
            {
                opt_name = IP_ADD_MEMBERSHIP;
                mreq.imr_multiaddr.s_addr = sadr.sin.sin_addr.s_addr;
                mreq.imr_interface.s_addr = maddr.sin.sin_addr.s_addr;
                mreq_arg_size = sizeof(mreq);
                mreq_arg_ptr = &mreq;
            }

#ifdef _WIN32
            const char* mreq_arg = (const char*)mreq_arg_ptr;
            const auto status_error = SOCKET_ERROR;
#else
            const void* mreq_arg = mreq_arg_ptr;
            const auto status_error = -1;
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
            // On Windows it somehow doesn't work when bind()
            // is called with multicast address. Write the address
            // that designates the network device here.
            // Also, sets port sharing when working with multicast
            sadr = maddr;
            int reuse = 1;
            int shareAddrRes = setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
            if (shareAddrRes == status_error)
            {
                throw runtime_error("marking socket for shared use failed");
            }
            Verb() << "Multicast(Windows): will bind to home address";
#else
            Verb() << "Multicast(POSIX): will bind to IGMP address: " << host;
#endif
            int res = setsockopt(m_sock, IPPROTO_IP, opt_name, mreq_arg, mreq_arg_size);

            if (res == status_error)
            {
                Error(errno, "adding to multicast membership failed");
            }

            attr.erase("multicast");
            attr.erase("adapter");
        }
    }

    // The "ttl" options is handled separately, it maps to both IP_TTL
    // and IP_MULTICAST_TTL so that TTL setting works for both uni- and multicast.
    if (attr.count("ttl"))
    {
        int ttl = stoi(attr.at("ttl"));
        int res = setsockopt(m_sock, IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof ttl);
        if (res == -1)
            Verb() << "WARNING: failed to set 'ttl' (IP_TTL) to " << ttl;
        res = setsockopt(m_sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof ttl);
        if (res == -1)
            Verb() << "WARNING: failed to set 'ttl' (IP_MULTICAST_TTL) to " << ttl;

        attr.erase("ttl");
    }

    m_options = attr;

    for (auto o: udp_options)
    {
        // Ignore "binding" - for UDP there are no post options.
        if (m_options.count(o.name))
        {
            string value = m_options.at(o.name);
            bool ok = o.apply<SocketOption::SYSTEM>(m_sock, value);
            if (!ok)
                Verb() << "WARNING: failed to set '" << o.name << "' to " << value;
        }
    }
}

void UdpCommon::Error(int err, string src)
{
    char buf[512];
    string message = SysStrError(err, buf, 512u);

    if (Verbose::on)
        Verb() << "FAILURE\n" << src << ": [" << err << "] " << message;
    else
        cerr << "\nERROR #" << err << ": " << message << endl;

    throw TransmissionError("error: " + src + ": " + message);
}

UdpCommon::~UdpCommon()
{
#ifdef _WIN32
    if (m_sock != -1)
    {
        shutdown(m_sock, SD_BOTH);
        closesocket(m_sock);
        m_sock = -1;
    }
#else
    close(m_sock);
#endif
}

UdpSource::UdpSource(string host, int port, const map<string,string>& attr)
{
    Setup(host, port, attr);
    int stat = ::bind(m_sock, sadr.get(), sadr.size());
    if (stat == -1)
        Error(SysError(), "Binding address for UDP");
    eof = false;
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (::setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*) &tv, sizeof(tv)) < 0)
        Error(SysError(), "Setting timeout for UDP");
}

MediaPacket UdpSource::Read(size_t chunk)
{
    bytevector data(chunk);
    sockaddr_any sa(sadr.family());
    int64_t srctime = 0;
AGAIN:
    int stat = recvfrom(m_sock, data.data(), (int) chunk, 0, sa.get(), &sa.syslen());
    int err = SysError();
    if (transmit_use_sourcetime)
    {
        srctime = srt_time_now();
    }
    if (stat == -1)
    {
        if (!::transmit_int_state && err == SysAGAIN)
            goto AGAIN;

        Error(SysError(), "UDP Read/recvfrom");
    }

    if (stat < 1)
    {
        eof = true;
        return bytevector();
    }

    chunk = size_t(stat);
    if (chunk < data.size())
        data.resize(chunk);

    return MediaPacket(data, srctime);
}

UdpTarget::UdpTarget(string host, int port, const map<string,string>& attr)
{
    Setup(host, port, attr);
    if (adapter != "")
    {
        auto maddr = CreateAddr(adapter, 0);
        in_addr addr = maddr.sin.sin_addr;

        int res = setsockopt(m_sock, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&addr), sizeof(addr));
        if (res == -1)
        {
            Error(SysError(), "setsockopt/IP_MULTICAST_IF: " + adapter);
        }
    }
}

void UdpTarget::Write(const MediaPacket& data)
{
    int stat = sendto(m_sock, data.payload.data(), int(data.payload.size()), 0, (sockaddr*)&sadr, int(sizeof sadr));
    if (stat == -1)
        Error(SysError(), "UDP Write/sendto");
}


template <class Iface> struct Udp;
template <> struct Udp<Source> { typedef UdpSource type; };
template <> struct Udp<Target> { typedef UdpTarget type; };
template <> struct Udp<Relay> { typedef UdpRelay type; };

template <class Iface>
Iface* CreateUdp(const string& host, int port, const map<string,string>& par) { return new typename Udp<Iface>::type (host, port, par); }

template<class Base>
inline bool IsOutput() { return false; }

template<>
inline bool IsOutput<Target>() { return true; }

template <class Base>
extern unique_ptr<Base> CreateMedium(const string& uri)
{
    unique_ptr<Base> ptr;

    UriParser u(uri);

    int iport = 0;
    switch ( u.type() )
    {
    default:
        break; // do nothing, return nullptr
    case UriParser::FILE:
        if (u.host() == "con" || u.host() == "console")
        {
            if ( IsOutput<Base>() && (
                        (Verbose::on && Verbose::cverb == &cout)
                        || transmit_bw_report || transmit_stats_report) )
            {
                cerr << "ERROR: file://con with -v or -r or -s would result in mixing the data and text info.\n";
                cerr << "ERROR: HINT: you can stream through a FIFO (named pipe)\n";
                throw invalid_argument("incorrect parameter combination");
            }
            ptr.reset( CreateConsole<Base>() );
        }
        else
            ptr.reset( CreateFile<Base>(u.path()));
        break;

    case UriParser::SRT:
        ptr.reset( CreateSrt<Base>(u.host(), u.portno(), u.path(), u.parameters()) );
        break;


    case UriParser::UDP:
        iport = atoi(u.port().c_str());
        if (iport < 1024)
        {
            cerr << "Port value invalid: " << iport << " - must be >=1024\n";
            throw invalid_argument("Invalid port number");
        }
        ptr.reset( CreateUdp<Base>(u.host(), iport, u.parameters()) );
        break;
    }

    if (ptr)
        ptr->uri = move(u);
    return ptr;
}


std::unique_ptr<Source> Source::Create(const std::string& url)
{
    return CreateMedium<Source>(url);
}

std::unique_ptr<Target> Target::Create(const std::string& url)
{
    return CreateMedium<Target>(url);
}
