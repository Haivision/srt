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

using srt_logging::SockStatusStr;

volatile bool transmit_throw_on_interrupt = false;
int transmit_bw_report = 0;
unsigned transmit_stats_report = 0;
size_t transmit_chunk_size = SRT_LIVE_DEF_PLSIZE;
bool transmit_printformat_json = false;
srt_listen_callback_fn* transmit_accept_hook_fn = nullptr;
void* transmit_accept_hook_op = nullptr;
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

    bytevector Read(size_t chunk) override { return FileRead(ifile, chunk, filename_copy); }

    bool IsOpen() override { return bool(ifile); }
    bool End() override { return ifile.eof(); }
    //~FileSource() { ifile.close(); }
};

#ifdef PLEASE_LOG
#include "logging.h"
#endif

class FileTarget: public virtual Target
{
    ofstream ofile;
public:

    FileTarget(const string& path): ofile(path, ios::out | ios::trunc | ios::binary) {}

    void Write(const bytevector& data) override
    {
        ofile.write(data.data(), data.size());
#ifdef PLEASE_LOG
        extern srt_logging::Logger applog;
        applog.Debug() << "FileTarget::Write: " << data.size() << " written to a file";
#endif
    }

    bool IsOpen() override { return !!ofile; }
    bool Broken() override { return !ofile.good(); }
    //~FileTarget() { ofile.close(); }
    void Close() override
    {
#ifdef PLEASE_LOG
        extern srt_logging::Logger applog;
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
    bytevector Read(size_t chunk) override { return FileRead(iofile, chunk, filename_copy); }

    void Write(const bytevector& data) override
    {
        iofile.write(data.data(), data.size());
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

        if (path == "group")
        {
            // Group specified, check type.
            m_group_type = par["type"];
            if (m_group_type == "")
            {
                Error("With //group, the group 'type' must be specified.");
            }

            if (m_group_type != "redundancy")
            {
                Error("With //group, only type=redundancy is currently supported");
            }

            vector<string> nodes;
            Split(par["nodes"], ',', back_inserter(nodes));

            if (nodes.empty())
            {
                Error("With //group, 'nodes' must specify comma-separated host:port specs.");
            }

            // Check if correctly specified
            for (string& hostport: nodes)
            {
                if (hostport == "")
                    continue;
                UriParser check(hostport, UriParser::EXPECT_HOST);
                if (check.host() == "" || check.port() == "")
                {
                    Error("With //group, 'nodes' must specify comma-separated host:port specs.");
                }

                if (check.portno() <= 1024)
                {
                    Error("With //group, every node in 'nodes' must have port >1024");
                }

                m_group_nodes.push_back(Connection(check.host(), check.portno()));
            }

            par.erase("type");
            par.erase("nodes");
        }
    }

    m_mode = "default";
    if (par.count("mode"))
        m_mode = par.at("mode");

    if (m_mode == "default")
    {
        // Use the following convention:
        // 1. Server for source, Client for target
        // 2. If host is empty, then always server.
        if (host == "" && m_group_nodes.empty())
            m_mode = "listener";
        //else if (!dir_output)
        //m_mode = "server";
        else
            m_mode = "caller";
    }

    if (m_mode == "client")
        m_mode = "caller";
    else if (m_mode == "server")
        m_mode = "listener";

    if (m_mode == "listener" && !m_group_nodes.empty())
    {
        Error("Multiple nodes (redundant links) only supported in CALLER (client) mode.");
    }

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
        m_adapter = par.at("adapter");
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
        srt_conn_epoll = AddPoller(m_bindsock, SRT_EPOLL_OUT);
    }

    sockaddr_in sa = CreateAddrInet(host, port);
    sockaddr* psa = (sockaddr*)&sa;
    Verb() << "Binding a server on " << host << ":" << port << " ...";
    stat = srt_bind(m_bindsock, psa, sizeof sa);
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

    Verb() << " accept... " << VerbNoEOL;
    ::transmit_throw_on_interrupt = true;

    if (!m_blocking_mode)
    {
        Verb() << "[ASYNC] (conn=" << srt_conn_epoll << ")";

        int len = 2;
        SRTSOCKET ready[2];
        if (srt_epoll_wait(srt_conn_epoll, 0, 0, ready, &len, -1, 0, 0, 0, 0) == -1)
            Error("srt_epoll_wait(srt_conn_epoll)");

        Verb() << "[EPOLL: " << len << " sockets] " << VerbNoEOL;
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
    sockaddr_in scl;
    int sclen = sizeof scl;

    Verb() << " accept..." << VerbNoEOL;

    m_sock = srt_accept(m_bindsock, (sockaddr*)&scl, &sclen);
    if (m_sock == SRT_INVALID_SOCK)
    {
        srt_close(m_bindsock);
        m_bindsock = SRT_INVALID_SOCK;
        Error("srt_accept");
    }

    if (m_sock & SRTGROUP_MASK)
    {
        m_listener_group = true;
        // There might be added a poller, remove it.
        // We need it work different way.

        if (srt_epoll != -1)
        {
            Verb() << "(Group: erasing epoll " << srt_epoll << ") " << VerbNoEOL;
            srt_epoll_release(srt_epoll);
        }

        // Don't add any sockets, they will have to be added
        // anew every time again.
        srt_epoll = srt_epoll_create();

        // Group data must have a size of at least 1
        // otherwise the srt_group_data() call will fail
        if (m_group_data.empty())
            m_group_data.resize(1);

        Verb() << " connected(group epoll " << srt_epoll <<").";
    }
    else
    {
        Verb() << " connected.";
    }
    ::transmit_throw_on_interrupt = false;

    // ConfigurePre is done on bindsock, so any possible Pre flags
    // are DERIVED by sock. ConfigurePost is done exclusively on sock.
    int stat = ConfigurePost(m_sock);
    if (stat == SRT_ERROR)
        Error("ConfigurePost");
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
            else
            {
                OpenGroupClient(); // Source data are in the fields already.
            }
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

    // Bring this declaration temporarily, this is only for testing
    std::string KmStateStr(SRT_KM_STATE state);

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

        srt_getsockflag(m_sock, SRTO_MAXBW, &bandwidth, &size_int64);
        srt_getsockflag(m_sock, SRTO_RCVLATENCY, &latency, &size_int);
        srt_getsockflag(m_sock, SRTO_RCVSYN, &blocking_rcv, &size_bool);
        srt_getsockflag(m_sock, SRTO_SNDSYN, &blocking_snd, &size_bool);
        srt_getsockflag(m_sock, SRTO_SNDDROPDELAY, &dropdelay, &size_int);

        Verb() << "OPTIONS: maxbw=" << bandwidth << " rcvlatency=" << latency << boolalpha
            << " blocking{rcv=" << blocking_rcv << " snd=" << blocking_snd
            << "} snddropdelay=" << dropdelay;
    }

    if (!m_blocking_mode)
    {
        // Don't add new epoll if already created as a part
        // of group management: if (srt_epoll == -1)...
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
    sockaddr_in localsa = CreateAddrInet(host, port);
    sockaddr* psa = (sockaddr*)&localsa;
    int stat = srt_bind(m_sock, psa, sizeof localsa);
    if (stat == SRT_ERROR)
        Error("srt_bind");
}

void SrtCommon::OpenClient(string host, int port)
{
    PrepareClient();

    if (m_outgoing_port)
    {
        SetupAdapter("", m_outgoing_port);
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
        srt_conn_epoll = AddPoller(m_sock, SRT_EPOLL_OUT);
    }

}

void SrtCommon::OpenGroupClient()
{
    SRT_GROUP_TYPE type = SRT_GTYPE_UNDEFINED;

    // Resolve group type.
    if (m_group_type == "redundancy")
        type = SRT_GTYPE_REDUNDANT;
    // else if blah blah blah...
    else
    {
        Error("With //group, type='" + m_group_type + "' undefined");
    }

    m_sock = srt_create_group(type);

    int stat = ConfigurePre(m_sock);

    if ( stat == SRT_ERROR )
        Error("ConfigurePre");

    if ( !m_blocking_mode )
    {
        // Note: here the GROUP is added to the poller.
        srt_conn_epoll = AddPoller(m_sock, SRT_EPOLL_OUT);

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

    vector<SRT_SOCKGROUPDATA> targets;
    int namelen = sizeof (sockaddr_in);

    Verb() << "Connecting to nodes:";
    int i = 1;
    for (Connection& c: m_group_nodes)
    {
        sockaddr_in sa = CreateAddrInet(c.host, c.port);
        sockaddr* psa = (sockaddr*)&sa;
        Verb() << "\t[" << i << "] " << c.host << ":" << c.port << " ... " << VerbNoEOL;
        ++i;
        targets.push_back(srt_prepare_endpoint(psa, namelen));
    }

    int fisock = srt_connect_group(m_sock, 0, namelen, targets.data(), targets.size());
    if (fisock == SRT_ERROR)
    {
        Error("srt_connect_group");
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
        if (targets[i].id != -1 && targets[i].status < SRTS_BROKEN)
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
            Verb() << "TARGET '" << SockaddrToString(targets[i].peeraddr) << "' connection failed.";
            continue;
        }

        /*
        if (!m_blocking_mode)
        {
            // EXPERIMENTAL version. Add all sockets to epoll
            // in the direction used for this medium.
            int modes = m_direction;
            srt_epoll_add_usock(srt_epoll, insock, &modes);
            Verb() << "Added @" << insock << " to epoll (" << srt_epoll << ") in modes: " << modes;
        }
        */

        // Have socket, store it into the group socket array.
        any_node = true;
    }

    stat = ConfigurePost(m_sock);
    if (stat == -1)
    {
        // This kind of error must reject the whole operation.
        // Usually you'll get this error on the first socket,
        // and doing this on the others would result in the same.
        Error("ConfigurePost");
    }


    Verb() << "Group connection report:";
    for (auto& d: m_group_data)
    {
        // id, status, result, peeraddr
        Verb() << "@" << d.id << " <" << SockStatusStr(d.status) << "> (=" << d.result << ") PEER:"
            << SockaddrToString(sockaddr_any((sockaddr*)&d.peeraddr, sizeof d.peeraddr));
    }

    /*

       XXX Temporarily disabled, until the nonblocking mode
       is added to groups.

    // Wait for REAL connected state if nonblocking mode, for AT LEAST one node.
    if ( !m_blocking_mode )
    {
        Verb() << "[ASYNC] " << VerbNoEOL;

        // SPIN-WAITING version. Don't use it unless you know what you're doing.
        // SpinWaitAsync();

        // Socket readiness for connection is checked by polling on WRITE allowed sockets.
        int len = 2;
        SRTSOCKET ready[2];
        if ( srt_epoll_wait(srt_conn_epoll,
                    NULL, NULL,
                    ready, &len,
                    -1, // Wait infinitely
                    NULL, NULL,
                    NULL, NULL) != -1 )
        {
            Verb() << "[EPOLL: " << len << " sockets] " << VerbNoEOL;
        }
        else
        {
            Error("srt_epoll_wait");
        }
    }
    */

    if (!any_node)
    {
        Error("REDUNDANCY: all redundant connections failed");
    }

    // Prepare group data for monitoring the group status.
    m_group_data.resize(m_group_nodes.size());
}

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

void SrtCommon::ConnectClient(string host, int port)
{

    sockaddr_in sa = CreateAddrInet(host, port);
    sockaddr* psa = (sockaddr*)&sa;
    Verb() << "Connecting to " << host << ":" << port << " ... " << VerbNoEOL;
    int stat = srt_connect(m_sock, psa, sizeof sa);
    if (stat == SRT_ERROR)
    {
        SRT_REJECT_REASON reason = srt_getrejectreason(m_sock);
#if PLEASE_LOG
        extern srt_logging::Logger applog;
        LOGP(applog.Error, "ERROR reported by srt_connect - closing socket @", m_sock);
#endif
        srt_close(m_sock);
        Error("srt_connect", reason);
    }

    // Wait for REAL connected state if nonblocking mode
    if (!m_blocking_mode)
    {
        Verb() << "[ASYNC] " << VerbNoEOL;

        // SPIN-WAITING version. Don't use it unless you know what you're doing.
        // SpinWaitAsync();

        // Socket readiness for connection is checked by polling on WRITE allowed sockets.
        int len = 2;
        SRTSOCKET ready[2];
        if (srt_epoll_wait(srt_conn_epoll, 0, 0, ready, &len, -1, 0, 0, 0, 0) != -1)
        {
            Verb() << "[EPOLL: " << len << " sockets] " << VerbNoEOL;
        }
        else
        {
            Error("srt_epoll_wait(srt_conn_epoll)");
        }
    }

    Verb() << " connected.";
    stat = ConfigurePost(m_sock);
    if (stat == SRT_ERROR)
        Error("ConfigurePost");
}

void SrtCommon::Error(string src, SRT_REJECT_REASON reason)
{
    int errnov = 0;
    const int result = srt_getlasterror(&errnov);
    if (result == SRT_SUCCESS)
    {
        cerr << "\nERROR (app): " << src << endl;
        throw std::runtime_error(src);
    }
    string message = srt_getlasterror_str();
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

void SrtCommon::SetupRendezvous(string adapter, int port)
{
    bool yes = true;
    srt_setsockopt(m_sock, 0, SRTO_RENDEZVOUS, &yes, sizeof yes);

    sockaddr_in localsa = CreateAddrInet(adapter, port);
    sockaddr* plsa = (sockaddr*)&localsa;
    Verb() << "Binding a server on " << adapter << ":" << port << " ...";
    int stat = srt_bind(m_sock, plsa, sizeof localsa);
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

void SrtCommon::UpdateGroupStatus(const SRT_SOCKGROUPDATA* grpdata, size_t grpdata_size)
{
    if (!grpdata)
    {
        // This happens when you passed too small array. Treat this as error and stop.
        cerr << "ERROR: redundancy group update reports " << grpdata_size
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

        SRT_SOCKSTATUS status = d.status;
        int result = d.result;

        if (result != -1 && status == SRTS_CONNECTED)
        {
            // Everything's ok. Don't do anything.
            continue;
        }
        // id, status, result, peeraddr
        Verb() << "GROUP SOCKET: @" << id << " <" << SockStatusStr(status) << "> (=" << result << ") PEER:"
            << SockaddrToString(sockaddr_any((sockaddr*)&d.peeraddr, sizeof d.peeraddr));

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
        // Check which nodes are no longer active and activate them.
        if (n.socket != SRT_INVALID_SOCK)
            continue;

        sockaddr_in sa = CreateAddrInet(n.host, n.port);
        sockaddr* psa = (sockaddr*)&sa;
        Verb() << "[" << i << "] RECONNECTING to node " << n.host << ":" << n.port << " ... " << VerbNoEOL;
        ++i;

        int insock = srt_connect(m_sock, psa, sizeof sa);
        if (insock == SRT_ERROR)
        {
            // Whatever. Skip the node.
            Verb() << "FAILED: ";
        }
        else
        {
            // Have socket, store it into the group socket array.
            n.socket = insock;
        }
    }
}

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



bytevector SrtSource::Read(size_t chunk)
{
    static size_t counter = 1;

    bool have_group = !m_group_nodes.empty();

    bytevector data(chunk);

    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    bool ready = true;
    int stat;

    do
    {
        if (have_group)
        {
            mctrl.grpdata = m_group_data.data();
            mctrl.grpdata_size = m_group_data.size();
        }

        ::transmit_throw_on_interrupt = true;
        stat = srt_recvmsg2(m_sock, data.data(), chunk, &mctrl);
        ::transmit_throw_on_interrupt = false;
        if (stat == SRT_ERROR)
        {
            if (!m_blocking_mode)
            {
                // EAGAIN for SRT READING
                if (srt_getlasterror(NULL) == SRT_EASYNCRCV)
                {
                    Verb() << "AGAIN: - waiting for data by epoll(" << srt_epoll << ")...";
                    // Poll on this descriptor until reading is available, indefinitely.
                    int len = 2;
                    SRTSOCKET sready[2];
                    if (srt_epoll_wait(srt_epoll, sready, &len, 0, 0, -1, 0, 0, 0, 0) != -1)
                    {
                        Verb() << "... epoll reported ready " << len << " sockets";
                        continue;
                    }
                    // If was -1, then passthru.
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
                << BufferStamp(data.data(), stat) << " BELATED: " << ((CTimer::getTime()-mctrl.srctime)/1000.0) << "ms");
#endif

        Verb() << "(#" << mctrl.msgno << " %" << mctrl.pktseq << "  " << BufferStamp(data.data(), stat) << ") " << VerbNoEOL;
    }
    while (!ready);

    chunk = size_t(stat);
    if (chunk < data.size())
        data.resize(chunk);

    const bool need_bw_report    = transmit_bw_report    && int(counter % transmit_bw_report) == transmit_bw_report - 1;
    const bool need_stats_report = transmit_stats_report && counter % transmit_stats_report == transmit_stats_report - 1;

    if (have_group) // Means, group with caller mode
    {
        UpdateGroupStatus(mctrl.grpdata, mctrl.grpdata_size);
    }
    else
    {
        if (transmit_stats_report && (need_stats_report || need_bw_report))
        {
            PrintSrtStats(m_sock, need_stats_report, need_bw_report, need_stats_report);
        }
    }

    ++counter;

    return data;
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

void SrtTarget::Write(const bytevector& data)
{
    static int counter = 1;
    ::transmit_throw_on_interrupt = true;

    // Check first if it's ready to write.
    // If not, wait indefinitely.
    if (!m_blocking_mode)
    {
        int ready[2];
        int len = 2;
        if (srt_epoll_wait(srt_epoll, 0, 0, ready, &len, -1, 0, 0, 0, 0) == SRT_ERROR)
            Error("srt_epoll_wait");
    }

    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    bool have_group = !m_group_nodes.empty();
    if (have_group || m_listener_group)
    {
        mctrl.grpdata = m_group_data.data();
        mctrl.grpdata_size = m_group_data.size();
    }

    int stat = srt_sendmsg2(m_sock, data.data(), data.size(), &mctrl);

    // For a socket group, the error is reported only
    // if ALL links from the group have failed to perform
    // the operation. If only one did, the result will be
    // visible in the status array.
    if (stat == SRT_ERROR)
        Error("srt_sendmsg");
    ::transmit_throw_on_interrupt = false;

    if (have_group)
    {
        // For listener group this is not necessary. The group information
        // is updated in mctrl.
        UpdateGroupStatus(mctrl.grpdata, mctrl.grpdata_size);
    }
    else
    {
        const bool need_bw_report    = transmit_bw_report    && int(counter % transmit_bw_report) == transmit_bw_report - 1;
        const bool need_stats_report = transmit_stats_report && counter % transmit_stats_report == transmit_stats_report - 1;

        if (transmit_stats_report && (need_stats_report || need_bw_report))
        {
            PrintSrtStats(m_sock, need_stats_report, need_bw_report, need_stats_report);
        }
    }

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
            UDT::setstreamid(m_sock, w_name);
        }
        else
        {
            Verb() << "NO STREAM ID for SRT connection";
        }

        if (m_outgoing_port)
        {
            Verb() << "Setting outgoing port: " << m_outgoing_port;
            SetupAdapter("", m_outgoing_port);
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

bytevector ConsoleRead(size_t chunk)
{
    bytevector data(chunk);
    bool st = cin.read(data.data(), chunk).good();
    chunk = cin.gcount();
    if (chunk == 0 && !st)
        return bytevector();

    if (chunk < data.size())
        data.resize(chunk);
    if (data.empty())
        throw Source::ReadEOF("CONSOLE device");

    return data;
}

class ConsoleSource: public virtual Source
{
public:

    ConsoleSource()
    {
    }

    bytevector Read(size_t chunk) override
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

    void Write(const bytevector& data) override
    {
        cout.write(data.data(), data.size());
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


class UdpCommon
{
protected:
    int m_sock = -1;
    sockaddr_in sadr;
    string adapter;
    map<string, string> m_options;

    void Setup(string host, int port, map<string,string> attr)
    {
        m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_sock == -1)
            Error(SysError(), "UdpCommon::Setup: socket");

        int yes = 1;
        ::setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);

        sadr = CreateAddrInet(host, port);

        bool is_multicast = false;

        if (attr.count("multicast"))
        {
            if (!IsMulticast(sadr.sin_addr))
            {
                throw std::runtime_error("UdpCommon: requested multicast for a non-multicast-type IP address");
            }
            is_multicast = true;
        }
        else if (IsMulticast(sadr.sin_addr))
        {
            is_multicast = true;
        }

        if (is_multicast)
        {
            ip_mreq_source mreq_ssm;
            ip_mreq mreq;
            sockaddr_in maddr;
            int opt_name;
            void* mreq_arg_ptr;
            socklen_t mreq_arg_size;

            adapter = attr.count("adapter") ? attr.at("adapter") : string();
            if (adapter == "")
            {
                Verb() << "Multicast: home address: INADDR_ANY:" << port;
                maddr.sin_family = AF_INET;
                maddr.sin_addr.s_addr = htonl(INADDR_ANY);
                maddr.sin_port = htons(port); // necessary for temporary use
            }
            else
            {
                Verb() << "Multicast: home address: " << adapter << ":" << port;
                maddr = CreateAddrInet(adapter, port);
            }

            if (attr.count("source"))
            {
                /* this is an ssm.  we need to use the right struct and opt */
                opt_name = IP_ADD_SOURCE_MEMBERSHIP;
                mreq_ssm.imr_multiaddr.s_addr = sadr.sin_addr.s_addr;
                mreq_ssm.imr_interface.s_addr = maddr.sin_addr.s_addr;
                inet_pton(AF_INET, attr.at("source").c_str(), &mreq_ssm.imr_sourceaddr);
                mreq_arg_size = sizeof(mreq_ssm);
                mreq_arg_ptr = &mreq_ssm;
            }
            else
            {
                opt_name = IP_ADD_MEMBERSHIP;
                mreq.imr_multiaddr.s_addr = sadr.sin_addr.s_addr;
                mreq.imr_interface.s_addr = maddr.sin_addr.s_addr;
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

    void Error(int err, string src)
    {
        char buf[512];
        string message = SysStrError(err, buf, 512u);

        if (Verbose::on)
            Verb() << "FAILURE\n" << src << ": [" << err << "] " << message;
        else
            cerr << "\nERROR #" << err << ": " << message << endl;

        throw TransmissionError("error: " + src + ": " + message);
    }

    ~UdpCommon()
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
};


class UdpSource: public virtual Source, public virtual UdpCommon
{
    bool eof = true;
public:

    UdpSource(string host, int port, const map<string,string>& attr)
    {
        Setup(host, port, attr);
        int stat = ::bind(m_sock, (sockaddr*)&sadr, sizeof sadr);
        if (stat == -1)
            Error(SysError(), "Binding address for UDP");
        eof = false;
    }

    bytevector Read(size_t chunk) override
    {
        bytevector data(chunk);
        sockaddr_in sa;
        socklen_t si = sizeof(sockaddr_in);
        int stat = recvfrom(m_sock, data.data(), chunk, 0, (sockaddr*)&sa, &si);
        if (stat == -1)
            Error(SysError(), "UDP Read/recvfrom");

        if (stat < 1)
        {
            eof = true;
            return bytevector();
        }

        chunk = size_t(stat);
        if (chunk < data.size())
            data.resize(chunk);

        return data;
    }

    bool IsOpen() override { return m_sock != -1; }
    bool End() override { return eof; }
};

class UdpTarget: public virtual Target, public virtual UdpCommon
{
public:
    UdpTarget(string host, int port, const map<string,string>& attr )
    {
        Setup(host, port, attr);
        if (adapter != "")
        {
            sockaddr_in maddr = CreateAddrInet(adapter, 0);
            in_addr addr = maddr.sin_addr;

            int res = setsockopt(m_sock, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&addr), sizeof(addr));
            if (res == -1)
            {
                Error(SysError(), "setsockopt/IP_MULTICAST_IF: " + adapter);
            }
        }

    }

    void Write(const bytevector& data) override
    {
        int stat = sendto(m_sock, data.data(), data.size(), 0, (sockaddr*)&sadr, sizeof sadr);
        if (stat == -1)
            Error(SysError(), "UDP Write/sendto");
    }

    bool IsOpen() override { return m_sock != -1; }
    bool Broken() override { return false; }
};

class UdpRelay: public Relay, public UdpSource, public UdpTarget
{
public:
    UdpRelay(string host, int port, const map<string,string>& attr):
        UdpSource(host, port, attr),
        UdpTarget(host, port, attr)
    {
    }

    bool IsOpen() override { return m_sock != -1; }
};

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
