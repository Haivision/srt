/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

// This is a simplified version of srt-live-transmit, which does not use C++11,
// however its functionality is limited to SRT to UDP only.

#include <iostream>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <cstring>
#include <csignal>

#include <srt.h>

#include "../common/appcommon.hpp"
#include "../common/uriparser.hpp"


using namespace std;

typedef std::vector<char> bytevector;

string true_names_i [] = { "1", "yes", "on", "true" };
string false_names_i [] = { "0", "no", "off", "false" };

set<string> true_names, false_names;

struct InitializeMe
{
    InitializeMe()
    {
        copy(true_names_i, true_names_i+4, inserter(true_names, true_names.begin()));
        copy(false_names_i, false_names_i+4, inserter(false_names, false_names.begin()));
    }

} g_initialize_names;

bool verbose = false;
volatile bool throw_on_interrupt = false;


class UdpCommon
{
protected:
    int m_sock;
    sockaddr_in sadr;
    string adapter;
    map<string, string> m_options;

    UdpCommon():
        m_sock(-1)
    {
    }

    void Setup(string host, int port, map<string,string> attr)
    {
        m_sock = socket(AF_INET, SOCK_DGRAM, 0);
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
#ifdef _WIN32
            int res = setsockopt(m_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq));
            if ( res == SOCKET_ERROR || res == -1 )
            {
                throw runtime_error("adding to multicast membership failed");
            }
#else
            int res = setsockopt(m_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
            if ( res == -1 )
            {
                throw runtime_error("adding to multicast membership failed");
            }
#endif
            attr.erase("multicast");
            attr.erase("adapter");
        }

        m_options = attr;

        /*
        for (auto o: udp_options)
        {
            // Ignore "binding" - for UDP there are no post options.
            if ( m_options.count(o.name) )
            {
                string value = m_options.at(o.name);
                bool ok = o.apply<SocketOption::SYSTEM>(m_sock, value);
                if ( verbose && !ok )
                    cout << "WARNING: failed to set '" << o.name << "' to " << value << endl;
            }
        }
        */
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

struct Target
{

};

class UdpTarget: public Target, public UdpCommon
{
public:
    UdpTarget(string host, int port, const map<string,string>& attr )
    {
        Setup(host, port, attr);
    }

    void Write(const bytevector& data) 
    {
        int stat = sendto(m_sock, data.data(), data.size(), 0, (sockaddr*)&sadr, sizeof sadr);
        if ( stat == -1 )
        {
            perror("write");
            throw runtime_error("Error during write");
        }
    }

    bool IsOpen()  { return m_sock != -1; }
    bool Broken()  { return false; }
};




class SrtCommon
{
protected:

    bool m_output_direction;
    bool m_blocking_mode;
    int m_timeout;
    map<string, string> m_options; // All other options, as provided in the URI
    UDTSOCKET m_sock;
    UDTSOCKET m_bindsock;
    bool IsUsable() { SRT_SOCKSTATUS st = srt_getsockstate(m_sock); return st > SRTS_INIT && st < SRTS_BROKEN; }
    bool IsBroken() { return srt_getsockstate(m_sock) > SRTS_CONNECTED; }

    SrtCommon():
        m_output_direction(false),
        m_blocking_mode(true),
        m_timeout(0),
        m_sock(UDT::INVALID_SOCK),
        m_bindsock(UDT::INVALID_SOCK)
    {
    }

    void Init(string host, int port, map<string,string> par, bool dir_output)
    {
        m_output_direction = dir_output;

        // Application-specific options: mode, blocking, timeout, adapter

        string mode = "default";
        if ( par.count("mode") )
            mode = par.at("mode");

        if ( mode == "default" )
        {
            // Use the following convention:
            // 1. Server for source, Client for target
            // 2. If host is empty, then always server.
            if ( host == "" )
                mode = "server";
            //else if ( !dir_output )
                //mode = "server";
            else
                mode = "client";
        }
        par.erase("mode");

        if ( par.count("blocking") )
        {
            if ( false_names.count(par.at("blocking")) )
            {
                m_blocking_mode = false;
            }
            else
            {
                m_blocking_mode = true;
            }
        }

        par.erase("blocking");

        if ( par.count("timeout") )
        {
            m_timeout = atoi(par.at("timeout").c_str());
            par.erase("timeout");
        }

        string adapter = ""; // needed for rendezvous only
        if ( par.count("adapter") )
        {
            adapter = par.at("adapter");
            par.erase("adapter");
        }

        // Assign the others here.
        m_options = par;

        if ( verbose )
            cout << "Opening SRT " << (dir_output ? "target" : "source") << " " << mode
                << "(" << (m_blocking_mode ? "" : "non-") << "blocking)"
                << " on " << host << ":" << port << endl;

        if ( mode == "client" || mode == "caller" )
            OpenClient(host, port);
        else if ( mode == "server" || mode == "listener" )
            OpenServer(host == "" ? adapter : host, port);
        else if ( mode == "rendezvous" )
            OpenRendezvous(adapter, host, port);
        else
        {
            throw std::invalid_argument("Invalid 'mode'. Use 'client' or 'server'");
        }
    }

    virtual int ConfigurePost(UDTSOCKET sock)
    {
        bool yes = m_blocking_mode;
        int result = 0;
        if ( m_output_direction )
        {
            result = UDT::setsockopt(sock, 0, UDT_SNDSYN, &yes, sizeof yes);
            if ( result == -1 )
                return result;

            if ( m_timeout )
                return UDT::setsockopt(sock, 0, UDT_SNDTIMEO, &m_timeout, sizeof m_timeout);
        }
        else
        {
            result = UDT::setsockopt(sock, 0, UDT_RCVSYN, &yes, sizeof yes);
            if ( result == -1 )
                return result;

            if ( m_timeout )
                return UDT::setsockopt(sock, 0, UDT_RCVTIMEO, &m_timeout, sizeof m_timeout);
        }

        /*
        for (auto o: srt_options)
        {
            if ( o.binding == SocketOption::POST && m_options.count(o.name) )
            {
                string value = m_options.at(o.name);
                bool ok = o.apply<SocketOption::SRT>(sock, value);
                if ( verbose )
                {
                    if ( !ok )
                        cout << "WARNING: failed to set '" << o.name << "' (post, " << (m_output_direction? "target":"source") << ") to " << value << endl;
                    else
                        cout << "NOTE: SRT/post::" << o.name << "=" << value << endl;
                }
            }
        }
        */

        return 0;
    }

    virtual int ConfigurePre(UDTSOCKET sock)
    {
        int result = 0;

        int yes = 1;
        result = UDT::setsockopt(sock, 0, SRT_TSBPDMODE, &yes, sizeof yes);
        if ( result == -1 )
            return result;

        if ( m_options.count("passphrase") )
        {
            if ( verbose )
                cout << "NOTE: using passphrase and 16-bit key\n";

            // Insert default
            if ( m_options.count("pbkeylen") == 0 )
            {
                m_options["pbkeylen"] = m_output_direction ? "16" : "0";
            }
        }

        // Let's pretend async mode is set this way.
        // This is for asynchronous connect.
        yes = m_blocking_mode;
        result = srt_setsockopt(sock, 0, SRTO_RCVSYN, &yes, sizeof yes);
        if ( result == -1 )
            return result;

        if ( m_timeout )
            return srt_setsockopt(sock, 0, SRTO_RCVTIMEO, &m_timeout, sizeof m_timeout);

        if ( verbose )
        {
            cout << "PRE: blocking mode set: " << yes << " timeout " << m_timeout << endl;
        }

        return 0;
    }

    void OpenClient(string host, int port)
    {
        m_sock = UDT::socket(AF_INET, SOCK_DGRAM, 0);
        if ( m_sock == UDT::ERROR )
            Error(UDT::getlasterror(), "UDT::socket");

        int stat = ConfigurePre(m_sock);
        if ( stat == UDT::ERROR )
            Error(UDT::getlasterror(), "ConfigurePre");
        sockaddr_in sa = CreateAddrInet(host, port);
        sockaddr* psa = (sockaddr*)&sa;
        if ( verbose )
        {
            cout << "Connecting to " << host << ":" << port << " ... ";
            cout.flush();
        }
        stat = UDT::connect(m_sock, psa, sizeof sa);
        if ( stat == UDT::ERROR )
        {
            Error(UDT::getlasterror(), "UDT::connect");
        }
        if ( verbose )
            cout << " connected.\n";
        stat = ConfigurePost(m_sock);
        if ( stat == UDT::ERROR )
            Error(UDT::getlasterror(), "ConfigurePost");
    }

    void Error(UDT::ERRORINFO& udtError, string src)
    {
        int udtResult = udtError.getErrorCode();
        if ( verbose )
        cout << "FAILURE\n" << src << ": [" << udtResult << "] " << udtError.getErrorMessage() << endl;
        udtError.clear();
        throw std::invalid_argument("error in " + src);
    }

    void OpenServer(string host, int port)
    {
        m_bindsock = UDT::socket(AF_INET, SOCK_DGRAM, 0);
        if ( m_bindsock == UDT::ERROR )
            Error(UDT::getlasterror(), "UDT::socket");

        int stat = ConfigurePre(m_bindsock);
        if ( stat == UDT::ERROR )
            Error(UDT::getlasterror(), "ConfigurePre");

        sockaddr_in sa = CreateAddrInet(host, port);
        sockaddr* psa = (sockaddr*)&sa;
        if ( verbose )
        {
            cout << "Binding a server on " << host << ":" << port << " ...";
            cout.flush();
        }
        stat = UDT::bind(m_bindsock, psa, sizeof sa);
        if ( stat == UDT::ERROR )
            Error(UDT::getlasterror(), "UDT::bind");
        if ( verbose )
        {
            cout << " listen... ";
            cout.flush();
        }
        stat = UDT::listen(m_bindsock, 1);
        if ( stat == UDT::ERROR )
            Error(UDT::getlasterror(), "UDT::listen");

        sockaddr_in scl;
        int sclen = sizeof scl;
        if ( verbose )
        {
            cout << " accept... ";
            cout.flush();
        }
        ::throw_on_interrupt = true;
        m_sock = UDT::accept(m_bindsock, (sockaddr*)&scl, &sclen);
        if ( m_sock == UDT::INVALID_SOCK )
            Error(UDT::getlasterror(), "UDT::accept");
        if ( verbose )
            cout << " connected.\n";
        ::throw_on_interrupt = false;

        // ConfigurePre is done on bindsock, so any possible Pre flags
        // are DERIVED by sock. ConfigurePost is done exclusively on sock.
        stat = ConfigurePost(m_sock);
        if ( stat == UDT::ERROR )
            Error(UDT::getlasterror(), "ConfigurePost");
    }

    void OpenRendezvous(string adapter, string host, int port)
    {
        m_sock = UDT::socket(AF_INET, SOCK_DGRAM, 0);
        if ( m_sock == UDT::ERROR )
            Error(UDT::getlasterror(), "UDT::socket");

        bool yes = true;
        UDT::setsockopt(m_sock, 0, UDT_RENDEZVOUS, &yes, sizeof yes);

        int stat = ConfigurePre(m_sock);
        if ( stat == UDT::ERROR )
            Error(UDT::getlasterror(), "ConfigurePre");

        sockaddr_in localsa = CreateAddrInet(adapter, port);
        sockaddr* plsa = (sockaddr*)&localsa;
        if ( verbose )
        {
            cout << "Binding a server on " << adapter << ":" << port << " ...";
            cout.flush();
        }
        stat = UDT::bind(m_sock, plsa, sizeof localsa);
        if ( stat == UDT::ERROR )
            Error(UDT::getlasterror(), "UDT::bind");

        sockaddr_in sa = CreateAddrInet(host, port);
        sockaddr* psa = (sockaddr*)&sa;
        if ( verbose )
        {
            cout << "Connecting to " << host << ":" << port << " ... ";
            cout.flush();
        }
        stat = UDT::connect(m_sock, psa, sizeof sa);
        if ( stat == UDT::ERROR )
            Error(UDT::getlasterror(), "UDT::connect");
        if ( verbose )
            cout << " connected.\n";

        stat = ConfigurePost(m_sock);
        if ( stat == UDT::ERROR )
            Error(UDT::getlasterror(), "ConfigurePost");
    }

    ~SrtCommon()
    {
        if ( verbose )
            cout << "SrtCommon: DESTROYING CONNECTION, closing sockets\n";
        if ( m_sock != UDT::INVALID_SOCK )
            UDT::close(m_sock);

        if ( m_bindsock != UDT::INVALID_SOCK )
            UDT::close(m_bindsock);
    }
};

// Just to seal up
struct Source
{

};

class SrtSource: public Source, public SrtCommon
{
    int srt_epoll;
public:

    SrtSource(string host, int port, const map<string,string>& par)
    {
        Init(host, port, par, false);

        if ( !m_blocking_mode )
        {
            srt_epoll = srt_epoll_create();
            if ( srt_epoll == SRT_ERROR )
                throw std::runtime_error("Can't create epoll in nonblocking mode");

            int modes = SRT_EPOLL_IN;
            srt_epoll_add_usock(srt_epoll, m_sock, &modes);
        }
    }

    bytevector Read(size_t chunk) 
    {
        bytevector data(chunk);
        bool ready = true;
        int stat;
        do
        {
            ::throw_on_interrupt = true;
            stat = UDT::recvmsg(m_sock, data.data(), chunk);
            ::throw_on_interrupt = false;
            if ( stat == UDT::ERROR )
            {
                Error(UDT::getlasterror(), "recvmsg");
                return bytevector();
            }

            if ( stat == 0 )
            {
                // Not necessarily eof. Closed connection is reported as error.
                //this_thread::sleep_for(chrono::milliseconds(10));
                usleep(10000);
                ready = false;
            }
        }
        while (!ready);

        chunk = size_t(stat);
        if ( chunk < data.size() )
            data.resize(chunk);

        return data;
    }

    virtual int ConfigurePre(UDTSOCKET sock) 
    {
        int result = SrtCommon::ConfigurePre(sock);
        if ( result == -1 )
            return result;
        // For sending party, the SRT_SENDER flag must be set, otherwise
        // the connection will be pure UDT.
        //int yes = 1;


        return 0;
    }

    bool IsOpen()  { return IsUsable(); }
    bool End()  { return IsBroken(); }
};

volatile bool int_state = false;

void OnINT_SetIntState(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    int_state = true;
    if ( throw_on_interrupt )
        throw std::runtime_error("Requested exception interrupt");
}

void OnAlarm_Interrupt(int)
{
    throw std::runtime_error("Watchdog bites hangup");
}


map<string,string> g_options;

int main( int argc, char** argv )
{
    vector<string> args;
    copy(argv+1, argv+argc, back_inserter(args));

    // Check options
    vector<string> params;

    for (vector<string>::iterator ai = args.begin(); ai != args.end(); ++ai)
    {
        string& a = *ai;

        if ( a[0] == '-' )
        {
            string key = a.substr(1);
            size_t pos = key.find(':');
            if ( pos == string::npos )
                pos = key.find(' ');
            string value = pos == string::npos ? "" : key.substr(pos+1);
            key = key.substr(0, pos);
            g_options[key] = value;
            continue;
        }

        params.push_back(a);
    }

    if ( params.size() != 2 )
    {
        cerr << "Usage: " << argv[0] << " [options] <srt-input-uri> <udp-output-uri>\n";
        return 1;
    }

    signal(SIGINT, OnINT_SetIntState);
    signal(SIGTERM, OnINT_SetIntState);

    UriParser su = params[0];
    UriParser tu = params[1];

    if ( su.scheme() != "srt" || tu.scheme() != "udp" )
    {
        cerr << "Source must be srt://... and target must be udp://...\n";
        return 1;
    }

    if ( su.portno() < 1024 || tu.portno() < 1024 )
    {
        cerr << "Port number must be >= 1024\n";
        return 1;
    }

    if ( g_options.count("v") )
        verbose = 1;

    bool crashonx = false;

    const size_t chunk = 1316;

    try
    {
        SrtSource src (su.host(), su.portno(), su.parameters());
        UdpTarget tar (tu.host(), tu.portno(), tu.parameters());

        // Now loop until broken
        for (;;)
        {
            const bytevector& data = src.Read(chunk);
            if ( verbose )
                cout << " << " << data.size() << "  ->  ";
            if ( data.empty() && src.End() )
            {
                if ( verbose )
                    cout << endl;
                break;
            }
            tar.Write(data);
            if ( tar.Broken() )
            {
                if ( verbose )
                    cout << " broken\n";
                break;
            }
            if ( verbose )
                cout << " sent\n";
            if ( int_state )
            {
                cerr << "\n (interrupted on request)\n";
                break;
            }

        }

    } catch (...) {
        if ( crashonx )
            throw;

        return 1;
    }

    return 0;
}

