/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

// Just for formality. This file should be used 
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <iterator>
#include <map>
#include <srt.h>
#if !defined(_WIN32)
#include <sys/ioctl.h>
#else
#include <fcntl.h> 
#include <io.h>
#endif

#include "netinet_any.h"
#include "apputil.hpp"
#include "socketoptions.hpp"
#include "uriparser.hpp"
#include "transmitmedia.hpp"
#include "srt_compat.h"
#include "verbose.hpp"

using namespace std;

bool g_stats_are_printed_to_stdout = false;
bool transmit_total_stats = false;
unsigned long transmit_bw_report = 0;
unsigned long transmit_stats_report = 0;
unsigned long transmit_chunk_size = SRT_LIVE_DEF_PLSIZE;

 PrintFormat printformat = PRINT_FORMAT_2COLS;
 bool first_line_printed = false;


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

    bool Read(size_t chunk, bytevector& data, ostream &SRT_ATR_UNUSED = cout) override
    {
        if (data.size() < chunk)
            data.resize(chunk);

        ifile.read(data.data(), chunk);
        size_t nread = ifile.gcount();
        if ( nread < data.size() )
            data.resize(nread);

        if ( data.empty() )
        {
            return false;
        }

        return true;
    }

    bool IsOpen() override { return bool(ifile); }
    bool End() override { return ifile.eof(); }
};

class FileTarget: public Target
{
    ofstream ofile;
public:

    FileTarget(const string& path): ofile(path, ios::out | ios::trunc | ios::binary) {}

    bool Write(const bytevector& data) override
    {
        ofile.write(data.data(), data.size());
        return !(ofile.bad());
    }

    int Write(const char* data, size_t size, ostream &SRT_ATR_UNUSED = cout) override
    {
        ofile.write(data, size);
        return !(ofile.bad()) ? size : 0;
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


static void PrintSrtStats(int sid, const CBytePerfMon& mon, ostream &out)
{
    std::ostringstream output;

    if (printformat == PRINT_FORMAT_JSON)
    {
        output << "{";
        output << "\"sid\":" << sid << ",";
        output << "\"time\":" << mon.msTimeStamp << ",";
        output << "\"window\":{";
        output << "\"flow\":" << mon.pktFlowWindow << ",";
        output << "\"congestion\":" << mon.pktCongestionWindow << ",";    
        output << "\"flight\":" << mon.pktFlightSize;    
        output << "},";
        output << "\"link\":{";
        output << "\"rtt\":" << mon.msRTT << ",";
        output << "\"bandwidth\":" << mon.mbpsBandwidth << ",";
        output << "\"maxBandwidth\":" << mon.mbpsMaxBW;
        output << "},";
        output << "\"send\":{";
        output << "\"packets\":" << mon.pktSent << ",";
        output << "\"packetsLost\":" << mon.pktSndLoss << ",";
        output << "\"packetsDropped\":" << mon.pktSndDrop << ",";
        output << "\"packetsRetransmitted\":" << mon.pktRetrans << ",";        
        output << "\"bytes\":" << mon.byteSent << ",";
        output << "\"bytesDropped\":" << mon.byteSndDrop << ",";
        output << "\"mbitRate\":" << mon.mbpsSendRate;
        output << "},";
        output << "\"recv\": {";
        output << "\"packets\":" << mon.pktRecv << ",";
        output << "\"packetsLost\":" << mon.pktRcvLoss << ",";
        output << "\"packetsDropped\":" << mon.pktRcvDrop << ",";
        output << "\"packetsRetransmitted\":" << mon.pktRcvRetrans << ",";
        output << "\"packetsBelated\":" << mon.pktRcvBelated << ",";
        output << "\"bytes\":" << mon.byteRecv << ",";
        output << "\"bytesLost\":" << mon.byteRcvLoss << ",";
        output << "\"bytesDropped\":" << mon.byteRcvDrop << ",";
        output << "\"mbitRate\":" << mon.mbpsRecvRate;
        output << "}";
        output << "}" << endl;
    }
    else if (printformat == PRINT_FORMAT_CSV)
    {
        if (!first_line_printed)
        {
            output << "Time,SocketID,pktFlowWindow,pktCongestionWindow,pktFlightSize,";
            output << "msRTT,mbpsBandwidth,mbpsMaxBW,pktSent,pktSndLoss,pktSndDrop,";
            output << "pktRetrans,byteSent,byteSndDrop,mbpsSendRate,usPktSndPeriod,";
            output << "pktRecv,pktRcvLoss,pktRcvDrop,pktRcvRetrans,pktRcvBelated,";
            output << "byteRecv,byteRcvLoss,byteRcvDrop,mbpsRecvRate,RCVLATENCYms";
            output << endl;
            first_line_printed = true;
        }
        int rcv_latency = 0;
        int int_len = sizeof rcv_latency;
        srt_getsockopt(sid, 0, SRTO_RCVLATENCY, &rcv_latency, &int_len);

        output << mon.msTimeStamp << ",";
        output << sid << ",";
        output << mon.pktFlowWindow << ",";
        output << mon.pktCongestionWindow << ",";
        output << mon.pktFlightSize << ",";
        output << mon.msRTT << ",";
        output << mon.mbpsBandwidth << ",";
        output << mon.mbpsMaxBW << ",";
        output << mon.pktSent << ",";
        output << mon.pktSndLoss << ",";
        output << mon.pktSndDrop << ",";
        output << mon.pktRetrans << ",";
        output << mon.byteSent << ",";
        output << mon.byteSndDrop << ",";
        output << mon.mbpsSendRate << ",";
        output << mon.usPktSndPeriod << ",";
        output << mon.pktRecv << ",";
        output << mon.pktRcvLoss << ",";
        output << mon.pktRcvDrop << ",";
        output << mon.pktRcvRetrans << ",";
        output << mon.pktRcvBelated << ",";
        output << mon.byteRecv << ",";
        output << mon.byteRcvLoss << ",";
        output << mon.byteRcvDrop << ",";
        output << mon.mbpsRecvRate << ",";
        output << rcv_latency;
        output << endl;
    }
    else
    {
        output << "======= SRT STATS: sid=" << sid << endl;
        output << "PACKETS     SENT: " << setw(11) << mon.pktSent            << "  RECEIVED:   " << setw(11) << mon.pktRecv              << endl;
        output << "LOST PKT    SENT: " << setw(11) << mon.pktSndLoss         << "  RECEIVED:   " << setw(11) << mon.pktRcvLoss           << endl;
        output << "REXMIT      SENT: " << setw(11) << mon.pktRetrans         << "  RECEIVED:   " << setw(11) << mon.pktRcvRetrans        << endl;
        output << "DROP PKT    SENT: " << setw(11) << mon.pktSndDrop         << "  RECEIVED:   " << setw(11) << mon.pktRcvDrop           << endl;
        output << "RATE     SENDING: " << setw(11) << mon.mbpsSendRate       << "  RECEIVING:  " << setw(11) << mon.mbpsRecvRate         << endl;
        output << "BELATED RECEIVED: " << setw(11) << mon.pktRcvBelated      << "  AVG TIME:   " << setw(11) << mon.pktRcvAvgBelatedTime << endl;
        output << "REORDER DISTANCE: " << setw(11) << mon.pktReorderDistance << endl;
        output << "WINDOW      FLOW: " << setw(11) << mon.pktFlowWindow      << "  CONGESTION: " << setw(11) << mon.pktCongestionWindow  << "  FLIGHT: " << setw(11) << mon.pktFlightSize << endl;
        output << "LINK         RTT: " << setw(9)  << mon.msRTT            << "ms  BANDWIDTH:  " << setw(7)  << mon.mbpsBandwidth    << "Mb/s " << endl;
        output << "BUFFERLEFT:  SND: " << setw(11) << mon.byteAvailSndBuf    << "  RCV:        " << setw(11) << mon.byteAvailRcvBuf      << endl;
    }

    out << output.str() << std::flush;
}

static void PrintSrtBandwidth(double mbpsBandwidth)
{
    std::ostringstream output;

    if (printformat == PRINT_FORMAT_JSON) {
        output << "{\"bandwidth\":" << mbpsBandwidth << '}' << endl;
    } else {
        output << "+++/+++SRT BANDWIDTH: " << mbpsBandwidth << endl;
    }

    cerr << output.str() << std::flush;
}


void SrtCommon::InitParameters(string host, map<string,string> par)
{
    // Application-specific options: mode, blocking, timeout, adapter
    if (Verbose::on && !par.empty())
    {
        Verb() << "SRT parameters specified:\n";
        for (map<string,string>::iterator i = par.begin(); i != par.end(); ++i)
        {
            cerr << "\t" << i->first << " = '" << i->second << "'\n";
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

    sockaddr_in sa = CreateAddrInet(host, port);
    sockaddr* psa = (sockaddr*)&sa;
    Verb() << "Binding a server on " << host << ":" << port << " ...";

    stat = srt_bind(m_bindsock, psa, sizeof sa);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_bindsock);
        Error(UDT::getlasterror(), "srt_bind");
    }

    Verb() << " listen...";

    stat = srt_listen(m_bindsock, backlog);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_bindsock);
        Error(UDT::getlasterror(), "srt_listen");
    }
}

void SrtCommon::StealFrom(SrtCommon& src)
{
    // This is used when SrtCommon class designates a listener
    // object that is doing Accept in appropriate direction class.
    // The new object should get the accepted socket.
    m_output_direction = src.m_output_direction;
    m_timeout = src.m_timeout;
    m_tsbpdmode = src.m_tsbpdmode;
    m_options = src.m_options;
    m_bindsock = SRT_INVALID_SOCK; // no listener
    m_sock = src.m_sock;
    src.m_sock = SRT_INVALID_SOCK; // STEALING
}

bool SrtCommon::AcceptNewClient()
{
    sockaddr_in scl;
    int sclen = sizeof scl;

    Verb() << " accept... ";

    m_sock = srt_accept(m_bindsock, (sockaddr*)&scl, &sclen);
    if ( m_sock == SRT_INVALID_SOCK )
    {
        srt_close(m_bindsock);
        m_bindsock = SRT_INVALID_SOCK;
        Error(UDT::getlasterror(), "srt_accept");
    }

    // we do one client connection at a time,
    // so close the listener.
    srt_close(m_bindsock);
    m_bindsock = SRT_INVALID_SOCK;

    Verb() << " connected.";

    // ConfigurePre is done on bindsock, so any possible Pre flags
    // are DERIVED by sock. ConfigurePost is done exclusively on sock.
    int stat = ConfigurePost(m_sock);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "ConfigurePost");

    return true;
}

void SrtCommon::Init(string host, int port, map<string,string> par, bool dir_output)
{
    m_output_direction = dir_output;
    InitParameters(host, par);

    Verb() << "Opening SRT " << (dir_output ? "target" : "source") << " " << m_mode
        << " on " << host << ":" << port;

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

int SrtCommon::ConfigurePost(SRTSOCKET sock)
{
    bool no = false;
    int result = 0;
    if ( m_output_direction )
    {
        result = srt_setsockopt(sock, 0, SRTO_SNDSYN, &no, sizeof no);
        if ( result == -1 )
            return result;

        if ( m_timeout )
            return srt_setsockopt(sock, 0, SRTO_SNDTIMEO, &m_timeout, sizeof m_timeout);
    }
    else
    {
        result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &no, sizeof no);
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
            if ( !ok )
                Verb() << "WARNING: failed to set '" << o.name << "' (post, "
                    << (m_output_direction? "target":"source") << ") to "
                    << value;
            else
                Verb() << "NOTE: SRT/post::" << o.name << "=" << value;
        }
    }

    return 0;
}

int SrtCommon::ConfigurePre(SRTSOCKET sock)
{
    int result = 0;

    bool no = false;
    if ( !m_tsbpdmode )
    {
        result = srt_setsockopt(sock, 0, SRTO_TSBPDMODE, &no, sizeof no);
        if ( result == -1 )
            return result;
    }

    result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &no, sizeof no);
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
        if ( Verbose::on )
        {
            cerr << "WARNING: failed to set options: ";
            copy(failures.begin(), failures.end(), ostream_iterator<string>(cerr, ", "));
            cerr << endl;
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
}


void SrtCommon::ConnectClient(string host, int port)
{

    sockaddr_in sa = CreateAddrInet(host, port);
    sockaddr* psa = (sockaddr*)&sa;

    Verb() << "Connecting to " << host << ":" << port;

    int stat = srt_connect(m_sock, psa, sizeof sa);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_sock);
        Error(UDT::getlasterror(), "UDT::connect");
    }

    stat = ConfigurePost(m_sock);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "ConfigurePost");
}

void SrtCommon::Error(UDT::ERRORINFO& udtError, string src)
{
    int udtResult = udtError.getErrorCode();
    string message = udtError.getErrorMessage();
    Verb() << "\nERROR #" << udtResult << ": " << message;

    udtError.clear();
    throw TransmissionError("error: " + src + ": " + message);
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

    sockaddr_in localsa = CreateAddrInet(adapter, port);
    sockaddr* plsa = (sockaddr*)&localsa;

    Verb() << "Binding a server on " << adapter << ":" << port;

    stat = srt_bind(m_sock, plsa, sizeof localsa);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_sock);
        Error(UDT::getlasterror(), "srt_bind");
    }

    sockaddr_in sa = CreateAddrInet(host, port);
    sockaddr* psa = (sockaddr*)&sa;
    Verb() << "Connecting to " << host << ":" << port;

    stat = srt_connect(m_sock, psa, sizeof sa);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_sock);
        Error(UDT::getlasterror(), "srt_connect");
    }

    stat = ConfigurePost(m_sock);
    if ( stat == SRT_ERROR )
        Error(UDT::getlasterror(), "ConfigurePost");
}

void SrtCommon::Close()
{
    Verb() << "SrtCommon: DESTROYING CONNECTION, closing sockets (rt%" << m_sock << " ls%" << m_bindsock << ")...";

    if ( m_sock != SRT_INVALID_SOCK )
    {
        srt_close(m_sock);
        m_sock = SRT_INVALID_SOCK;
    }

    if ( m_bindsock != SRT_INVALID_SOCK )
    {
        srt_close(m_bindsock);
        m_bindsock = SRT_INVALID_SOCK ;
    }

    Verb() << "SrtCommon: ... done.";
}

SrtCommon::~SrtCommon()
{
    Close();
}

SrtSource::SrtSource(string host, int port, const map<string,string>& par)
{
    Init(host, port, par, false);

    ostringstream os;
    os << host << ":" << port;
    hostport_copy = os.str();
}

bool SrtSource::Read(size_t chunk, bytevector& data, ostream &out_stats)
{
    static unsigned long counter = 1;

    if (data.size() < chunk)
        data.resize(chunk);

    bool ready = true;
    int stat;
    do
    {
        stat = srt_recvmsg(m_sock, data.data(), chunk);
        if ( stat == SRT_ERROR )
        {
            // EAGAIN for SRT READING
            if ( srt_getlasterror(NULL) == SRT_EASYNCRCV )
            {
                data.clear();
                return false;
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

    const bool need_bw_report = transmit_bw_report && (counter % transmit_bw_report) == transmit_bw_report - 1;
    const bool need_stats_report = transmit_stats_report && (counter % transmit_stats_report) == transmit_stats_report - 1;

    if (need_bw_report || need_stats_report)
    {
        CBytePerfMon perf;
        srt_bstats(m_sock, &perf, need_stats_report && !transmit_total_stats);
        if (need_bw_report)
        {
            PrintSrtBandwidth(perf.mbpsBandwidth);
        }
        if (need_stats_report)
        {
            PrintSrtStats(m_sock, perf, out_stats);
        }
    }

    ++counter;

    return true;
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

int SrtTarget::Write(const char* data, size_t size, ostream &out_stats)
{
    static unsigned long counter = 1;

    int stat = srt_sendmsg2(m_sock, data, size, nullptr);
    if (stat == SRT_ERROR)
    {
        return stat;
    }

    const bool need_bw_report = transmit_bw_report && (counter % transmit_bw_report) == transmit_bw_report - 1;
    const bool need_stats_report = transmit_stats_report && (counter % transmit_stats_report) == transmit_stats_report - 1;

    if (need_bw_report || need_stats_report)
    {
        CBytePerfMon perf;
        srt_bstats(m_sock, &perf, need_stats_report && !transmit_total_stats);
        if (need_bw_report)
        {
            PrintSrtBandwidth(perf.mbpsBandwidth);
        }
        if (need_stats_report)
        {
            PrintSrtStats(m_sock, perf, out_stats);
        }
    }

    ++counter;

    return stat;
}

bool SrtTarget::Write(const bytevector& data) 
{
    return -1 != Write(data.data(), data.size());
}

SrtModel::SrtModel(string host, int port, map<string,string> par)
{
    InitParameters(host, par);
    if (m_mode == "caller")
        is_caller = true;
    else if (m_mode != "listener")
        throw std::invalid_argument("Only caller and listener modes supported");

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

    if (is_caller)
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

template <class Iface>
Iface* CreateSrt(const string& host, int port, const map<string,string>& par) { return new typename Srt<Iface>::type (host, port, par); }

class ConsoleSource: public Source
{
public:

    ConsoleSource()
    {
#ifdef _WIN32
        // The default stdin mode on windows is text.
        // We have to set it to the binary mode
        _setmode(_fileno(stdin), _O_BINARY);
#endif
    }

    bool Read(size_t chunk, bytevector& data, ostream &SRT_ATR_UNUSED = cout) override
    {
        if (data.size() < chunk)
            data.resize(chunk);

        bool st = cin.read(data.data(), chunk).good();
        chunk = cin.gcount();
        if ( chunk == 0 && !st )
        {
            data.clear();
            return false;
        }

        if ( chunk < data.size() )
            data.resize(chunk);
        if ( data.empty() )
            return false;

        return true;
    }

    bool IsOpen() override { return cin.good(); }
    bool End() override { return cin.eof(); }
    int GetSysSocket() { return 0; };
};

class ConsoleTarget: public Target
{
public:

    ConsoleTarget()
    {
#ifdef _WIN32
        // The default stdout mode on windows is text.
        // We have to set it to the binary mode
        _setmode(_fileno(stdout), _O_BINARY);
#endif
    }

    int Write(const char* data, size_t len, ostream &SRT_ATR_UNUSED = cout) override
    {
        cout.write(data, len);
        return len;
    }

    bool Write(const bytevector& data) override
    {
        return 0 != Write(data.data(), data.size());
    }

    bool IsOpen() override { return cout.good(); }
    bool Broken() override { return cout.eof(); }
    int GetSysSocket() { return 0; };
};

template <class Iface> struct Console;
template <> struct Console<Source> { typedef ConsoleSource type; };
template <> struct Console<Target> { typedef ConsoleTarget type; };

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

        // set non-blocking mode
#if defined(_WIN32)
        unsigned long ulyes = 1;
        if (ioctlsocket(m_sock, FIONBIO, &ulyes) == SOCKET_ERROR)
#else
        if (ioctl(m_sock, FIONBIO, (const char *)&yes) < 0)
#endif
        {
            Error(SysError(), "UdpCommon::Setup: ioctl FIONBIO");
        }

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
            ip_mreq_source mreq_ssm;
            ip_mreq mreq;
            sockaddr_in maddr;
            int opt_name;
            void* mreq_arg_ptr;
            socklen_t mreq_arg_size;

            adapter = attr.count("adapter") ? attr.at("adapter") : string();
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


class UdpSource: public Source, public UdpCommon
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

    bool Read(size_t chunk, bytevector& data, ostream &SRT_ATR_UNUSED = cout) override
    {
        if (data.size() < chunk)
            data.resize(chunk);

        sockaddr_in sa;
        socklen_t si = sizeof(sockaddr_in);
        int stat = recvfrom(m_sock, data.data(), chunk, 0, (sockaddr*)&sa, &si);
        if ( stat < 1 )
        {
            if (SysError() != EWOULDBLOCK)
                eof = true;
            data.clear();
            return false;
        }

        chunk = size_t(stat);
        if ( chunk < data.size() )
            data.resize(chunk);

        return true;
    }

    bool IsOpen() override { return m_sock != -1; }
    bool End() override { return eof; }

    int GetSysSocket() { return m_sock; };
};

class UdpTarget: public Target, public UdpCommon
{
public:
    UdpTarget(string host, int port, const map<string,string>& attr )
    {
        Setup(host, port, attr);
    }

    int Write(const char* data, size_t len, ostream &SRT_ATR_UNUSED = cout) override
    {
        int stat = sendto(m_sock, data, len, 0, (sockaddr*)&sadr, sizeof sadr);
        if ( stat == -1 )
        {
            if ((false))
                Error(SysError(), "UDP Write/sendto");
            return stat;
        }
        return stat;
    }

    bool Write(const bytevector& data) override
    {
        return -1 != Write(data.data(), data.size());
    }

    bool IsOpen() override { return m_sock != -1; }
    bool Broken() override { return false; }

    int GetSysSocket() { return m_sock; };
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
    default:
        break; // do nothing, return nullptr
    case UriParser::FILE:
        if (u.host() == "con" || u.host() == "console")
        {
            if (IsOutput<Base>() && (
                (Verbose::on && Verbose::cverb == &cout)
                || g_stats_are_printed_to_stdout))
            {
                cerr << "ERROR: file://con with -v or -r or -s would result in mixing the data and text info.\n";
                cerr << "ERROR: HINT: you can stream through a FIFO (named pipe)\n";
                throw invalid_argument("incorrect parameter combination");
            }
            ptr.reset(CreateConsole<Base>());
        }
// Disable regular file support for the moment
#if 0
        else
            ptr.reset( CreateFile<Base>(u.path()));
#endif
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

    if (ptr.get())
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
