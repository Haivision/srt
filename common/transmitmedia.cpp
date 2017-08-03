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

#include "appcommon.hpp"
#include "socketoptions.hpp"
#include "uriparser.hpp"
#include "transmitmedia.hpp"

using namespace std;

bool transmit_verbose = false;
std::ostream* transmit_cverb = nullptr;
volatile bool transmit_throw_on_interrupt = false;
int transmit_bw_report = 0;
unsigned transmit_stats_report = 0;

class FileSource: public Source
{
    ifstream ifile;
    string filename_copy;
public:

    FileSource(const string& path): ifile(path, ios::in | ios::binary), filename_copy(path)
    {
        if ( !ifile )
            throw std::runtime_error(path + ": Can't open file for reading");
    }

    bytevector Read(size_t chunk) override
    {
        bytevector data(chunk);
        ifile.read(data.data(), chunk);
        size_t nread = ifile.gcount();
        if ( nread < data.size() )
            data.resize(nread);

        if ( data.empty() )
            throw ReadEOF(filename_copy);
        return data;
    }

    bool IsOpen() override { return bool(ifile); }
    bool End() override { return ifile.eof(); }
    //~FileSource() { ifile.close(); }
};

class FileTarget: public Target
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

template <class Iface> struct File;
template <> struct File<Source> { typedef FileSource type; };
template <> struct File<Target> { typedef FileTarget type; };

template <class Iface>
Iface* CreateFile(const string& name) { return new typename File<Iface>::type (name); }


template <class PerfMonType>
void PrintSrtStats(int sid, const PerfMonType& mon)
{
    cout << "======= SRT STATS: sid=" << sid << endl;
    cout << "PACKETS SENT: " << mon.pktSent << " RECEIVED: " << mon.pktRecv << endl;
    cout << "LOST PKT SENT: " << mon.pktSndLoss << " RECEIVED: " << mon.pktRcvLoss << endl;
    cout << "REXMIT SENT: " << mon.pktRetrans << " RECEIVED: " << mon.pktRcvRetrans << endl;
    cout << "RATE SENDING: " << mon.mbpsSendRate << " RECEIVING: " << mon.mbpsRecvRate << endl;
    cout << "BELATED RECEIVED: " << mon.pktRcvBelated << " AVG TIME: " << mon.pktRcvAvgBelatedTime << endl;
    cout << "REORDER DISTANCE: " << mon.pktReorderDistance << endl;
    cout << "WINDOW: FLOW: " << mon.pktFlowWindow << " CONGESTION: " << mon.pktCongestionWindow << " FLIGHT: " << mon.pktFlightSize << endl;
    cout << "RTT: " << mon.msRTT << "ms  BANDWIDTH: " << mon.mbpsBandwidth << "Mb/s\n";
    cout << "BUFFERLEFT: SND: " << mon.byteAvailSndBuf << " RCV: " << mon.byteAvailRcvBuf << endl;
}


void SrtCommon::InitParameters(string host, map<string,string> par)
{
    // Application-specific options: mode, blocking, timeout, adapter
    if ( transmit_verbose )
    {
        cout << "Parameters:\n";
        for (map<string,string>::iterator i = par.begin(); i != par.end(); ++i)
        {
            cout << "\t" << i->first << " = '" << i->second << "'\n";
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
    if ( transmit_verbose )
    {
        cout << "Binding a server on " << host << ":" << port << " ...";
        cout.flush();
    }
    stat = srt_bind(m_bindsock, psa, sizeof sa);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_bindsock);
        Error(UDT::getlasterror(), "srt_bind");
    }

    if ( transmit_verbose )
    {
        cout << " listen... ";
        cout.flush();
    }
    stat = srt_listen(m_bindsock, backlog);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_bindsock);
        Error(UDT::getlasterror(), "srt_listen");
    }

    if ( transmit_verbose )
    {
        cout << " accept... ";
        cout.flush();
    }
    ::transmit_throw_on_interrupt = true;

    if ( !m_blocking_mode )
    {
        if ( transmit_verbose )
            cout << "[ASYNC] " << flush;

        int len = 2;
        SRTSOCKET ready[2];
        if ( srt_epoll_wait(srt_conn_epoll, 0, 0, ready, &len, -1, 0, 0, 0, 0) == -1 )
            Error(UDT::getlasterror(), "srt_epoll_wait");

        if ( transmit_verbose )
        {
            cout << "[EPOLL: " << len << " sockets] " << flush;
        }
    }
}

void SrtCommon::StealFrom(SrtCommon& src)
{
    // This is used when SrtCommon class designates a listener
    // object that is doing Accept in appropriate direction class.
    // The new object should get the accepted socket.
    m_output_direction = src.m_output_direction;
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
    m_sock = srt_accept(m_bindsock, (sockaddr*)&scl, &sclen);
    if ( m_sock == SRT_INVALID_SOCK )
    {
        srt_close(m_bindsock);
        Error(UDT::getlasterror(), "srt_accept");
    }

    if ( transmit_verbose )
        cout << " connected.\n";
    ::transmit_throw_on_interrupt = false;

    // ConfigurePre is done on bindsock, so any possible Pre flags
    // are DERIVED by sock. ConfigurePost is done exclusively on sock.
    int stat = ConfigurePost(m_sock);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "ConfigurePost");
}

void SrtCommon::Init(string host, int port, map<string,string> par, bool dir_output)
{
    m_output_direction = dir_output;
    InitParameters(host, par);

    if ( transmit_verbose )
        cout << "Opening SRT " << (dir_output ? "target" : "source") << " " << m_mode
            << "(" << (m_blocking_mode ? "" : "non-") << "blocking)"
            << " on " << host << ":" << port << endl;

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
    if ( m_output_direction )
    {
        result = srt_setsockopt(sock, 0, SRTO_SNDSYN, &yes, sizeof yes);
        if ( result == -1 )
            return result;

        if ( m_timeout )
            return srt_setsockopt(sock, 0, SRTO_SNDTIMEO, &m_timeout, sizeof m_timeout);
    }
    else
    {
        result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &yes, sizeof yes);
        if ( result == -1 )
            return result;

        if ( m_timeout )
            return srt_setsockopt(sock, 0, SRTO_RCVTIMEO, &m_timeout, sizeof m_timeout);
    }

    SrtConfigurePost(sock, m_options);

    for (auto o: srt_options)
    {
        if ( o.binding == SocketOption::POST && m_options.count(o.name) )
        {
            string value = m_options.at(o.name);
            bool ok = o.apply<SocketOption::SRT>(sock, value);
            if ( transmit_verbose )
            {
                if ( !ok )
                    cout << "WARNING: failed to set '" << o.name << "' (post, " << (m_output_direction? "target":"source") << ") to " << value << endl;
                else
                    cout << "NOTE: SRT/post::" << o.name << "=" << value << endl;
            }
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

    //if ( m_timeout )
    //    result = srt_setsockopt(sock, 0, SRTO_RCVTIMEO, &m_timeout, sizeof m_timeout);
    //if ( result == -1 )
    //    return result;

    //if ( transmit_verbose )
    //{
    //    cout << "PRE: blocking mode set: " << yes << " timeout " << m_timeout << endl;
    //}

    // host is only checked for emptiness and depending on that the connection mode is selected.
    // Here we are not exactly interested with that information.
    vector<string> failures;
    SocketOption::Mode conmode = SrtConfigurePre(sock, "",  m_options, &failures);

    if ( transmit_verbose && conmode == SocketOption::FAILURE )
    {
        cout << "WARNING: failed to set options: ";
        copy(failures.begin(), failures.end(), ostream_iterator<string>(cout, ", "));
        cout << endl;
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
            if ( transmit_verbose )
                cout << state << flush;
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

void SrtCommon::ConnectClient(string host, int port)
{

    sockaddr_in sa = CreateAddrInet(host, port);
    sockaddr* psa = (sockaddr*)&sa;
    if ( transmit_verbose )
    {
        cout << "Connecting to " << host << ":" << port << " ... ";
        cout.flush();
    }
    int stat = srt_connect(m_sock, psa, sizeof sa);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_sock);
        Error(UDT::getlasterror(), "UDT::connect");
    }

    // Wait for REAL connected state if nonblocking mode
    if ( !m_blocking_mode )
    {
        if ( transmit_verbose )
            cout << "[ASYNC] " << flush;

        // SPIN-WAITING version. Don't use it unless you know what you're doing.
        // SpinWaitAsync();

        // Socket readiness for connection is checked by polling on WRITE allowed sockets.
        int len = 2;
        SRTSOCKET ready[2];
        if ( srt_epoll_wait(srt_conn_epoll, 0, 0, ready, &len, -1, 0, 0, 0, 0) != -1 )
        {
            if ( transmit_verbose )
            {
                cout << "[EPOLL: " << len << " sockets] " << flush;
            }
        }
        else
        {
            Error(UDT::getlasterror(), "srt_epoll_wait");
        }
    }

    if ( transmit_verbose )
        cout << " connected.\n";
    stat = ConfigurePost(m_sock);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "ConfigurePost");
}

void SrtCommon::Error(UDT::ERRORINFO& udtError, string src)
{
    int udtResult = udtError.getErrorCode();
    string message = udtError.getErrorMessage();
    if ( transmit_verbose )
        cout << "FAILURE\n" << src << ": [" << udtResult << "] " << message << endl;
    else
        cerr << "\nERROR #" << udtResult << ": " << message << endl;

    udtError.clear();
    throw std::runtime_error("error in " + src + ": " + message);
}

void SrtCommon::OpenRendezvous(string adapter, string host, int port)
{
    m_sock = srt_socket(AF_INET, SOCK_DGRAM, 0);
    if ( m_sock == SRT_ERROR )
        Error(UDT::getlasterror(), "srt_socket");

    bool yes = true;
    srt_setsockopt(m_sock, 0, SRTO_RENDEZVOUS, &yes, sizeof yes);

    int stat = ConfigurePre(m_sock);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "ConfigurePre");

    if ( !m_blocking_mode )
    {
        srt_conn_epoll = AddPoller(m_sock, SRT_EPOLL_OUT);
    }

    sockaddr_in localsa = CreateAddrInet(adapter, port);
    sockaddr* plsa = (sockaddr*)&localsa;
    if ( transmit_verbose )
    {
        cout << "Binding a server on " << adapter << ":" << port << " ...";
        cout.flush();
    }
    stat = srt_bind(m_sock, plsa, sizeof localsa);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_sock);
        Error(UDT::getlasterror(), "srt_bind");
    }

    sockaddr_in sa = CreateAddrInet(host, port);
    sockaddr* psa = (sockaddr*)&sa;
    if ( transmit_verbose )
    {
        cout << "Connecting to " << host << ":" << port << " ... ";
        cout.flush();
    }
    stat = srt_connect(m_sock, psa, sizeof sa);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_sock);
        Error(UDT::getlasterror(), "srt_connect");
    }

    // Wait for REAL connected state if nonblocking mode
    if ( !m_blocking_mode )
    {
        if ( transmit_verbose )
            cout << "[ASYNC] " << flush;

        // SPIN-WAITING version. Don't use it unless you know what you're doing.
        // SpinWaitAsync();

        // Socket readiness for connection is checked by polling on WRITE allowed sockets.
        int len = 2;
        SRTSOCKET ready[2];
        if ( srt_epoll_wait(srt_conn_epoll, 0, 0, ready, &len, -1, 0, 0, 0, 0) != -1 )
        {
            if ( transmit_verbose )
            {
                cout << "[EPOLL: " << len << " sockets] " << flush;
            }
        }
        else
        {
            Error(UDT::getlasterror(), "srt_epoll_wait");
        }
    }

    if ( transmit_verbose )
        cout << " connected.\n";

    stat = ConfigurePost(m_sock);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "ConfigurePost");
}

void SrtCommon::Close()
{
    if ( transmit_verbose )
        cout << "SrtCommon: DESTROYING CONNECTION, closing sockets (rt%" << m_sock << " ls%" << m_bindsock << ")...\n";

    bool yes = true;
    if ( m_sock != UDT::INVALID_SOCK )
    {
        srt_setsockflag(m_sock, SRTO_SNDSYN, &yes, sizeof yes);
        srt_close(m_sock);
    }

    if ( m_bindsock != UDT::INVALID_SOCK )
    {
        // Set sndsynchro to the socket to synch-close it.
        srt_setsockflag(m_bindsock, SRTO_SNDSYN, &yes, sizeof yes);
        srt_close(m_bindsock);
    }
    if ( transmit_verbose )
        cout << "SrtCommon: ... done.\n";
}

SrtCommon::~SrtCommon()
{
    Close();
}

SrtSource::SrtSource(string host, int port, const map<string,string>& par)
{
    Init(host, port, par, false);

    if ( !m_blocking_mode )
    {
        srt_epoll = AddPoller(m_sock, SRT_EPOLL_IN);
    }

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
                    if ( transmit_verbose )
                    {
                        cout << "AGAIN: - waiting for data by epoll...\n";
                    }
                    // Poll on this descriptor until reading is available, indefinitely.
                    int len = 2;
                    SRTSOCKET ready[2];
                    if ( srt_epoll_wait(srt_epoll, ready, &len, 0, 0, -1, 0, 0, 0, 0) != -1 )
                    {
                        if ( transmit_verbose )
                        {
                            cout << "... epoll reported ready " << len << " sockets\n";
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

    CBytePerfMon perf;
    srt_bstats(m_sock, &perf, true);
    if ( transmit_bw_report && int(counter % transmit_bw_report) == transmit_bw_report - 1 )
    {
        cout << "+++/+++SRT BANDWIDTH: " << perf.mbpsBandwidth << endl;
    }

    if ( transmit_stats_report && counter % transmit_stats_report == transmit_stats_report - 1)
    {
        //CPerfMon pmon;
        //memset(&pmon, 0, sizeof pmon);
        //UDT::perfmon(m_sock, &pmon, false);
        //PrintSrtStats(m_sock, pmon);
        PrintSrtStats(m_sock, perf);
    }

    ++counter;

    return data;
}

int SrtTarget::ConfigurePre(SRTSOCKET sock)
{
    int result = SrtCommon::ConfigurePre(sock);
    if ( result == -1 )
        return result;

    int yes = 1;
    // This is for the HSv4 compatibility, if both parties are HSv5
    // (min. version 1.2.1), then this setting simply does nothing.
    // In HSv4 this setting is obligatory otherwise the SRT handshake
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


template <class Iface> struct Srt;
template <> struct Srt<Source> { typedef SrtSource type; };
template <> struct Srt<Target> { typedef SrtTarget type; };

template <class Iface>
Iface* CreateSrt(const string& host, int port, const map<string,string>& par) { return new typename Srt<Iface>::type (host, port, par); }

class ConsoleSource: public Source
{
public:

    ConsoleSource()
    {
    }

    bytevector Read(size_t chunk) override
    {
        bytevector data(chunk);
		bool st = cin.read(data.data(), chunk).good();
        chunk = cin.gcount();
        if ( chunk == 0 && !st )
            return bytevector();

        if ( chunk < data.size() )
            data.resize(chunk);
        if ( data.empty() )
            throw ReadEOF("CONSOLE device");

        return data;
    }

    bool IsOpen() override { return cin.good(); }
    bool End() override { return cin.eof(); }
};

class ConsoleTarget: public Target
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

template <class Iface> struct Console;
template <> struct Console<Source> { typedef ConsoleSource type; };
template <> struct Console<Target> { typedef ConsoleTarget type; };

template <class Iface>
Iface* CreateConsole() { return new typename Console<Iface>::type (); }


// More options can be added in future.
SocketOption udp_options [] {
    { "ipttl", IPPROTO_IP, IP_TTL, SocketOption::INT, SocketOption::PRE },
    { "iptos", IPPROTO_IP, IP_TOS, SocketOption::INT, SocketOption::PRE },
};

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
        {
            perror("UdpCommon:socket");
            throw std::runtime_error("UdpCommon: failed to create a socket");
        }
        sadr = CreateAddrInet(host, port);

        if ( attr.count("multicast") )
        {
            adapter = attr.count("adapter") ? attr.at("adapter") : string();
            sockaddr_in maddr;
            if ( adapter == "" )
            {
                maddr.sin_addr.s_addr = htonl(INADDR_ANY);
            }
            else
            {
                maddr = CreateAddrInet(adapter, port);
            }

            ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = sadr.sin_addr.s_addr;
            mreq.imr_interface.s_addr = maddr.sin_addr.s_addr;
#ifdef WIN32
            const char* mreq_arg = (const char*)&mreq;
            const auto status_error = SOCKET_ERROR;
#else
            const void* mreq_arg = &mreq;
            const auto status_error = -1;
#endif
            int res = setsockopt(m_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, mreq_arg, sizeof(mreq));

            if ( res == status_error )
            {
                throw runtime_error("adding to multicast membership failed");
            }
            attr.erase("multicast");
            attr.erase("adapter");
        }

        m_options = attr;

        for (auto o: udp_options)
        {
            // Ignore "binding" - for UDP there are no post options.
            if ( m_options.count(o.name) )
            {
                string value = m_options.at(o.name);
                bool ok = o.apply<SocketOption::SYSTEM>(m_sock, value);
                if ( transmit_verbose && !ok )
                    cout << "WARNING: failed to set '" << o.name << "' to " << value << endl;
            }
        }
    }

    ~UdpCommon()
    {
#ifdef WIN32
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


class UdpSource: public Source, public UdpCommon
{
    bool eof = true;
public:

    UdpSource(string host, int port, const map<string,string>& attr)
    {
        Setup(host, port, attr);
        int stat = ::bind(m_sock, (sockaddr*)&sadr, sizeof sadr);
        if ( stat == -1 )
        {
            perror("bind");
            throw runtime_error("bind failed, UDP cannot read");
        }
        eof = false;
    }

    bytevector Read(size_t chunk) override
    {
        bytevector data(chunk);
        sockaddr_in sa;
        socklen_t si = sizeof(sockaddr_in);
        int stat = recvfrom(m_sock, data.data(), chunk, 0, (sockaddr*)&sa, &si);
        if ( stat == -1 || stat == 0 )
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

class UdpTarget: public Target, public UdpCommon
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
        {
            perror("UdpTarget: write");
            throw runtime_error("Error during write");
        }
    }

    bool IsOpen() override { return m_sock != -1; }
    bool Broken() override { return false; }
};

template <class Iface> struct Udp;
template <> struct Udp<Source> { typedef UdpSource type; };
template <> struct Udp<Target> { typedef UdpTarget type; };

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
    default: ; // do nothing, return nullptr
    case UriParser::FILE:
        if ( u.host() == "con" || u.host() == "console" )
        {
            if ( IsOutput<Base>() && (
                        (transmit_verbose && transmit_cverb == &cout)
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


extern std::unique_ptr<Source> Source::Create(const std::string& url)
{
    return CreateMedium<Source>(url);
}

extern std::unique_ptr<Target> Target::Create(const std::string& url)
{
    return CreateMedium<Target>(url);
}
