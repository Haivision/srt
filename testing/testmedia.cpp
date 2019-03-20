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

#include "netinet_any.h"
#include "apputil.hpp"
#include "socketoptions.hpp"
#include "uriparser.hpp"
#include "testmedia.hpp"
#include "srt_compat.h"
#include "verbose.hpp"

using namespace std;

std::ostream* transmit_cverb = nullptr;
volatile bool transmit_throw_on_interrupt = false;
int transmit_bw_report = 0;
unsigned transmit_stats_report = 0;
size_t transmit_chunk_size = SRT_LIVE_DEF_PLSIZE;


string DirectionName(SRT_EPOLL_OPT direction)
{
    string dir_name;
    if (direction)
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
    if ( nread < data.size() )
        data.resize(nread);

    if ( data.empty() )
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
        if ( !ifile )
            throw std::runtime_error(path + ": Can't open file for reading");
    }

    bytevector Read(size_t chunk) override { return FileRead(ifile, chunk, filename_copy); }

    bool IsOpen() override { return bool(ifile); }
    bool End() override { return ifile.eof(); }
    //~FileSource() { ifile.close(); }
};

class FileTarget: public virtual Target
{
    ofstream ofile;
public:

    FileTarget(const string& path): ofile(path, ios::out | ios::trunc | ios::binary) {}

    void Write(const bytevector& data) override
    {
        ofile.write(data.data(), data.size());
    }

    bool IsOpen() override { return !!ofile; }
    bool Broken() override { return !ofile.good(); }
    //~FileTarget() { ofile.close(); }
    void Close() override { ofile.close(); }
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
        if ( !iofile )
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


template <class PerfMonType>
void PrintSrtStats(int sid, const PerfMonType& mon)
{
    Verb() << "======= SRT STATS: sid=" << sid;
    Verb() << "PACKETS SENT: " << mon.pktSent << " RECEIVED: " << mon.pktRecv;
    Verb() << "LOST PKT SENT: " << mon.pktSndLoss << " RECEIVED: " << mon.pktRcvLoss;
    Verb() << "REXMIT SENT: " << mon.pktRetrans << " RECEIVED: " << mon.pktRcvRetrans;
    Verb() << "RATE SENDING: " << mon.mbpsSendRate << " RECEIVING: " << mon.mbpsRecvRate;
    Verb() << "BELATED RECEIVED: " << mon.pktRcvBelated << " AVG TIME: " << mon.pktRcvAvgBelatedTime;
    Verb() << "REORDER DISTANCE: " << mon.pktReorderDistance;
    Verb() << "WINDOW: FLOW: " << mon.pktFlowWindow << " CONGESTION: " << mon.pktCongestionWindow << " FLIGHT: " << mon.pktFlightSize;
    Verb() << "RTT: " << mon.msRTT << "ms  BANDWIDTH: " << mon.mbpsBandwidth << "Mb/s\n";
    Verb() << "BUFFERLEFT: SND: " << mon.byteAvailSndBuf << " RCV: " << mon.byteAvailRcvBuf;
}


void SrtCommon::InitParameters(string host, map<string,string> par)
{
    // Application-specific options: mode, blocking, timeout, adapter
    if ( Verbose::on )
    {
        Verb() << "Parameters:\n";
        for (map<string,string>::iterator i = par.begin(); i != par.end(); ++i)
        {
            Verb() << "\t" << i->first << " = '" << i->second << "'\n";
        }
    }

    m_mode = "default";
    if ( par.count("mode") )
        m_mode = par.at("mode");

    if ( m_mode == "default" )
    {
        // Use the following convention:
        // 1. Server for source, Client for target
        // 2. If host is empty, then always server.
        if ( host == "" )
            m_mode = "listener";
        //else if ( !dir_output )
        //m_mode = "server";
        else
            m_mode = "caller";
    }

    if ( m_mode == "client" )
        m_mode = "caller";
    else if ( m_mode == "server" )
        m_mode = "listener";

    par.erase("mode");

    if ( par.count("blocking") )
    {
        m_blocking_mode = !false_names.count(par.at("blocking"));
        par.erase("blocking");
    }

    if ( par.count("timeout") )
    {
        m_timeout = stoi(par.at("timeout"), 0, 0);
        par.erase("timeout");
    }

    if ( par.count("adapter") )
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

    if ( par.count("tsbpd") && false_names.count(par.at("tsbpd")) )
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
}

void SrtCommon::PrepareListener(string host, int port, int backlog)
{
    m_bindsock = srt_socket(AF_INET, SOCK_DGRAM, 0);
    if ( m_bindsock == SRT_ERROR )
        Error(UDT::getlasterror(), "srt_socket");

    int stat = ConfigurePre(m_bindsock);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "ConfigurePre");

    if ( !m_blocking_mode )
    {
        srt_conn_epoll = AddPoller(m_bindsock, SRT_EPOLL_OUT);
    }

    sockaddr_in sa = CreateAddrInet(host, port);
    sockaddr* psa = (sockaddr*)&sa;
    Verb() << "Binding a server on " << host << ":" << port << " ...";
    stat = srt_bind(m_bindsock, psa, sizeof sa);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_bindsock);
        Error(UDT::getlasterror(), "srt_bind");
    }

    Verb() << " listen... " << VerbNoEOL;
    stat = srt_listen(m_bindsock, backlog);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_bindsock);
        Error(UDT::getlasterror(), "srt_listen");
    }

    Verb() << " accept... " << VerbNoEOL;
    ::transmit_throw_on_interrupt = true;

    if ( !m_blocking_mode )
    {
        Verb() << "[ASYNC] ";

        int len = 2;
        SRTSOCKET ready[2];
        if ( srt_epoll_wait(srt_conn_epoll, 0, 0, ready, &len, -1, 0, 0, 0, 0) == -1 )
            Error(UDT::getlasterror(), "srt_epoll_wait");

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
    if ( m_sock == SRT_INVALID_SOCK )
    {
        srt_close(m_bindsock);
        m_bindsock = SRT_INVALID_SOCK;
        Error(UDT::getlasterror(), "srt_accept");
    }

    Verb() << " connected.";
    ::transmit_throw_on_interrupt = false;

    // ConfigurePre is done on bindsock, so any possible Pre flags
    // are DERIVED by sock. ConfigurePost is done exclusively on sock.
    int stat = ConfigurePost(m_sock);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "ConfigurePost");
}

void SrtCommon::Init(string host, int port, map<string,string> par, SRT_EPOLL_OPT dir)
{
    m_direction = dir;
    InitParameters(host, par);

    Verb() << "Opening SRT " << DirectionName(dir) << " " << m_mode
        << "(" << (m_blocking_mode ? "" : "non-") << "blocking)"
        << " on " << host << ":" << port;

    try
    {
        if ( m_mode == "caller" )
            OpenClient(host, port);
        else if ( m_mode == "listener" )
            OpenServer(m_adapter, port);
        else if ( m_mode == "rendezvous" )
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

    if ( !m_blocking_mode )
    {
        srt_epoll = AddPoller(m_sock, dir);
    }
}

int SrtCommon::AddPoller(SRTSOCKET socket, int modes)
{
    int pollid = srt_epoll_create();
    if ( pollid == -1 )
        throw std::runtime_error("Can't create epoll in nonblocking mode");
    srt_epoll_add_usock(pollid, socket, &modes);
    return pollid;
}

int SrtCommon::ConfigurePost(SRTSOCKET sock)
{
    bool yes = m_blocking_mode;
    int result = 0;
    if ( m_direction & SRT_EPOLL_OUT )
    {
        result = srt_setsockopt(sock, 0, SRTO_SNDSYN, &yes, sizeof yes);
        if ( result == -1 )
            return result;

        if ( m_timeout )
            return srt_setsockopt(sock, 0, SRTO_SNDTIMEO, &m_timeout, sizeof m_timeout);
    }

    if ( m_direction & SRT_EPOLL_IN )
    {
        result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &yes, sizeof yes);
        if ( result == -1 )
            return result;

        if ( m_timeout )
            return srt_setsockopt(sock, 0, SRTO_RCVTIMEO, &m_timeout, sizeof m_timeout);
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
    if ( !m_tsbpdmode )
    {
        result = srt_setsockopt(sock, 0, SRTO_TSBPDMODE, &no, sizeof no);
        if ( result == -1 )
            return result;
    }

    // Let's pretend async mode is set this way.
    // This is for asynchronous connect.
    int maybe = m_blocking_mode;
    result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &maybe, sizeof maybe);
    if ( result == -1 )
        return result;

    // host is only checked for emptiness and depending on that the connection mode is selected.
    // Here we are not exactly interested with that information.
    vector<string> failures;

    // NOTE: here host = "", so the 'connmode' will be returned as LISTENER always,
    // but it doesn't matter here. We don't use 'connmode' for anything else than
    // checking for failures.
    SocketOption::Mode conmode = SrtConfigurePre(sock, "",  m_options, &failures);

    if ( conmode == SocketOption::FAILURE )
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
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "srt_bind");
}

void SrtCommon::OpenClient(string host, int port)
{
    PrepareClient();

    if ( m_outgoing_port )
    {
        SetupAdapter("", m_outgoing_port);
    }

    ConnectClient(host, port);
}

void SrtCommon::PrepareClient()
{
    m_sock = srt_socket(AF_INET, SOCK_DGRAM, 0);
    if ( m_sock == SRT_ERROR )
        Error(UDT::getlasterror(), "srt_socket");

    int stat = ConfigurePre(m_sock);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "ConfigurePre");

    if ( !m_blocking_mode )
    {
        srt_conn_epoll = AddPoller(m_sock, SRT_EPOLL_OUT);
    }

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
   if ( int(state) < SRTS_CONNECTED )
   {
   if ( Verbose::on )
   Verb() << state;
   usleep(250000);
   continue;
   }
   else if ( int(state) > SRTS_CONNECTED )
   {
   Error(UDT::getlasterror(), "UDT::connect status=" + udt_status_names[state]);
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
    if ( stat == SRT_ERROR )
    {
        srt_close(m_sock);
        Error(UDT::getlasterror(), "UDT::connect");
    }

    // Wait for REAL connected state if nonblocking mode
    if ( !m_blocking_mode )
    {
        Verb() << "[ASYNC] " << VerbNoEOL;

        // SPIN-WAITING version. Don't use it unless you know what you're doing.
        // SpinWaitAsync();

        // Socket readiness for connection is checked by polling on WRITE allowed sockets.
        int len = 2;
        SRTSOCKET ready[2];
        if ( srt_epoll_wait(srt_conn_epoll, 0, 0, ready, &len, -1, 0, 0, 0, 0) != -1 )
        {
            Verb() << "[EPOLL: " << len << " sockets] " << VerbNoEOL;
        }
        else
        {
            Error(UDT::getlasterror(), "srt_epoll_wait");
        }
    }

    Verb() << " connected.";
    stat = ConfigurePost(m_sock);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "ConfigurePost");
}

void SrtCommon::Error(UDT::ERRORINFO& udtError, string src)
{
    int udtResult = udtError.getErrorCode();
    string message = udtError.getErrorMessage();
    if ( Verbose::on )
        Verb() << "FAILURE\n" << src << ": [" << udtResult << "] " << message;
    else
        cerr << "\nERROR #" << udtResult << ": " << message << endl;

    udtError.clear();
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
    if ( stat == SRT_ERROR )
    {
        srt_close(m_sock);
        Error(UDT::getlasterror(), "srt_bind");
    }
}

void SrtCommon::Close()
{
    bool any = false;
    bool yes = true;
    if ( m_sock != SRT_INVALID_SOCK )
    {
        Verb() << "SrtCommon: DESTROYING CONNECTION, closing socket (rt%" << m_sock << ")...";
        srt_setsockflag(m_sock, SRTO_SNDSYN, &yes, sizeof yes);
        srt_close(m_sock);
        any = true;
    }

    if ( m_bindsock != SRT_INVALID_SOCK )
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

SrtSource::SrtSource(string host, int port, const map<string,string>& par)
{
    Init(host, port, par, SRT_EPOLL_IN);
    ostringstream os;
    os << host << ":" << port;
    hostport_copy = os.str();
}

bytevector SrtSource::Read(size_t chunk)
{
    static size_t counter = 1;

    bytevector data(chunk);
    bool ready = true;
    int stat;
    do
    {
        ::transmit_throw_on_interrupt = true;
        stat = srt_recvmsg(m_sock, data.data(), chunk);
        ::transmit_throw_on_interrupt = false;
        if ( stat == SRT_ERROR )
        {
            if ( !m_blocking_mode )
            {
                // EAGAIN for SRT READING
                if ( srt_getlasterror(NULL) == SRT_EASYNCRCV )
                {
                    Verb() << "AGAIN: - waiting for data by epoll...";
                    // Poll on this descriptor until reading is available, indefinitely.
                    int len = 2;
                    SRTSOCKET sready[2];
                    if ( srt_epoll_wait(srt_epoll, sready, &len, 0, 0, -1, 0, 0, 0, 0) != -1 )
                    {
                        if ( Verbose::on )
                        {
                            Verb() << "... epoll reported ready " << len << " sockets";
                        }
                        continue;
                    }
                    // If was -1, then passthru.
                }
            }
            Error(UDT::getlasterror(), "recvmsg");
        }

        if ( stat == 0 )
        {
            throw ReadEOF(hostport_copy);
        }
    }
    while (!ready);

    chunk = size_t(stat);
    if ( chunk < data.size() )
        data.resize(chunk);

    const bool need_bw_report    = transmit_bw_report    && int(counter % transmit_bw_report) == transmit_bw_report - 1;
    const bool need_stats_report = transmit_stats_report && counter % transmit_stats_report == transmit_stats_report - 1;

    CBytePerfMon perf;
    if (need_stats_report || need_bw_report)
    {
        // clear only if stats report is to be read
        srt_bstats(m_sock, &perf, need_stats_report /* clear */);

        if (need_bw_report)
        {
            Verb() << "+++/+++SRT BANDWIDTH: " << perf.mbpsBandwidth;
        }

        if (need_stats_report)
        {
            PrintSrtStats(m_sock, perf);
        }
    }

    ++counter;

    return data;
}

SrtTarget::SrtTarget(std::string host, int port, const std::map<std::string,std::string>& par)
{
    Init(host, port, par, SRT_EPOLL_OUT);
}


int SrtTarget::ConfigurePre(SRTSOCKET sock)
{
    int result = SrtCommon::ConfigurePre(sock);
    if ( result == -1 )
        return result;

    int yes = 1;
    // This is for the HSv4 compatibility; if both parties are HSv5
    // (min. version 1.2.1), then this setting simply does nothing.
    // In HSv4 this setting is obligatory; otherwise the SRT handshake
    // extension will not be done at all.
    result = srt_setsockopt(sock, 0, SRTO_SENDER, &yes, sizeof yes);
    if ( result == -1 )
        return result;

    return 0;
}

void SrtTarget::Write(const bytevector& data)
{
    ::transmit_throw_on_interrupt = true;

    // Check first if it's ready to write.
    // If not, wait indefinitely.
    if ( !m_blocking_mode )
    {
        int ready[2];
        int len = 2;
        if ( srt_epoll_wait(srt_epoll, 0, 0, ready, &len, -1, 0, 0, 0, 0) == SRT_ERROR )
            Error(UDT::getlasterror(), "srt_epoll_wait");
    }

    int stat = srt_sendmsg2(m_sock, data.data(), data.size(), nullptr);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "srt_sendmsg");
    ::transmit_throw_on_interrupt = false;
}

SrtRelay::SrtRelay(std::string host, int port, const std::map<std::string,std::string>& par)
{
    Init(host, port, par, SRT_EPOLL_IN | SRT_EPOLL_OUT);
}

SrtModel::SrtModel(string host, int port, map<string,string> par)
{
    InitParameters(host, par); // This fixes also 'm_mode' parameter
    if (m_mode == "caller")
        is_caller = true;
    else if (m_mode == "rendezvous")
        is_rend = true;
    else if (m_mode != "listener")
        throw std::invalid_argument("Wrong 'mode' attribute; expected: caller, listener, rendezvous");

    m_host = host;
    m_port = port;
}

void SrtModel::Establish(ref_t<std::string> name)
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

        if (name.get() != "")
        {
            Verb() << "Connect with requesting stream [" << name.get() << "]";
            UDT::setstreamid(m_sock, *name);
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
            if ( srt_getsockname(Socket(), &s, &namelen) == SRT_ERROR )
            {
                Error(UDT::getlasterror(), "srt_getsockname");
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
        *name = UDT::getstreamid(m_sock);
        Verb() << "... GOT CLIENT for stream [" << name.get() << "]";
    }
}


template <class Iface> struct Srt;
template <> struct Srt<Source> { typedef SrtSource type; };
template <> struct Srt<Target> { typedef SrtTarget type; };
template <> struct Srt<Relay> { typedef SrtRelay type; };

template <class Iface>
Iface* CreateSrt(const string& host, int port, const map<string,string>& par) { return new typename Srt<Iface>::type (host, port, par); }

bytevector ConsoleRead(size_t chunk)
{
    bytevector data(chunk);
    bool st = cin.read(data.data(), chunk).good();
    chunk = cin.gcount();
    if ( chunk == 0 && !st )
        return bytevector();

    if ( chunk < data.size() )
        data.resize(chunk);
    if ( data.empty() )
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

        if ( attr.count("multicast") )
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
            adapter = attr.count("adapter") ? attr.at("adapter") : string();
            sockaddr_in maddr;
            if ( adapter == "" )
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

            ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = sadr.sin_addr.s_addr;
            mreq.imr_interface.s_addr = maddr.sin_addr.s_addr;
#ifdef _WIN32
            const char* mreq_arg = (const char*)&mreq;
            const auto status_error = SOCKET_ERROR;
#else
            const void* mreq_arg = &mreq;
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
            int res = setsockopt(m_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, mreq_arg, sizeof(mreq));

            if ( res == status_error )
            {
                throw runtime_error("adding to multicast membership failed");
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
            if ( m_options.count(o.name) )
            {
                string value = m_options.at(o.name);
                bool ok = o.apply<SocketOption::SYSTEM>(m_sock, value);
                if ( !ok )
                    Verb() << "WARNING: failed to set '" << o.name << "' to " << value;
            }
        }
    }

    void Error(int err, string src)
    {
        char buf[512];
        string message = SysStrError(err, buf, 512u);

        if ( Verbose::on )
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
        if ( stat == -1 )
            Error(SysError(), "Binding address for UDP");
        eof = false;
    }

    bytevector Read(size_t chunk) override
    {
        bytevector data(chunk);
        sockaddr_in sa;
        socklen_t si = sizeof(sockaddr_in);
        int stat = recvfrom(m_sock, data.data(), chunk, 0, (sockaddr*)&sa, &si);
        if ( stat == -1 )
            Error(SysError(), "UDP Read/recvfrom");

        if ( stat < 1 )
        {
            eof = true;
            return bytevector();
        }

        chunk = size_t(stat);
        if ( chunk < data.size() )
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
    }

    void Write(const bytevector& data) override
    {
        int stat = sendto(m_sock, data.data(), data.size(), 0, (sockaddr*)&sadr, sizeof sadr);
        if ( stat == -1 )
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
        if ( u.host() == "con" || u.host() == "console" )
        {
            if ( IsOutput<Base>() && (
                        (Verbose::on && transmit_cverb == &cout)
                        || transmit_bw_report) )
            {
                cerr << "ERROR: file://con with -v or -r would result in mixing the data and text info.\n";
                cerr << "ERROR: HINT: you can stream through a FIFO (named pipe)\n";
                throw invalid_argument("incorrect parameter combination");
            }
            ptr.reset( CreateConsole<Base>() );
        }
        else
            ptr.reset( CreateFile<Base>(u.path()));
        break;

    case UriParser::SRT:
        iport = atoi(u.port().c_str());
        if ( iport <= 1024 )
        {
            cerr << "Port value invalid: " << iport << " - must be >1024\n";
            throw invalid_argument("Invalid port number");
        }
        ptr.reset( CreateSrt<Base>(u.host(), iport, u.parameters()) );
        break;


    case UriParser::UDP:
        iport = atoi(u.port().c_str());
        if ( iport <= 1024 )
        {
            cerr << "Port value invalid: " << iport << " - must be >1024\n";
            throw invalid_argument("Invalid port number");
        }
        ptr.reset( CreateUdp<Base>(u.host(), iport, u.parameters()) );
        break;

    }

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
