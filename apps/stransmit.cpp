/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

// NOTE: This application uses C++11.

// This program uses quite a simple architecture, which is mainly related to
// the way how it's invoked: stransmit <source> <target> (plus options).
//
// The media for <source> and <target> are filled by abstract classes
// named Source and Target respectively. Most important virtuals to
// be filled by the derived classes are Source::Read and Target::Write.
//
// For SRT please take a look at the SrtCommon class first. This contains
// everything that is needed for creating an SRT medium, that is, making
// a connection as listener, as caller, and as rendezvous. The listener
// and caller modes are built upon the same philosophy as those for
// BSD/POSIX socket API (bind/listen/accept or connect).
//
// The instance class is selected per details in the URI (usually scheme)
// and then this URI is used to configure the medium object. Medium-specific
// options are specified in the URI: SCHEME://HOST:PORT?opt1=val1&opt2=val2 etc.
//
// Options for connection are set by ConfigurePre and ConfigurePost.
// This is a philosophy that exists also in BSD/POSIX sockets, just not
// officially mentioned:
// - The "PRE" options must be set prior to connecting and can't be altered
//   on a connected socket, however if set on a listening socket, they are
//   derived by accept-ed socket. 
// - The "POST" options can be altered any time on a connected socket.
//   They MAY have also some meaning when set prior to connecting; such
//   option is SRTO_RCVSYN, which makes connect/accept call asynchronous.
//   Because of that this option is treated special way in this app.
//
// See 'srt_options' global variable for a list of all options.

// MSVS likes to complain about lots of standard C functions being unsafe.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <cctype>
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
#include <chrono>
#include <thread>

#include "../common/appcommon.hpp"  // CreateAddrInet
#include "../common/uriparser.hpp"  // UriParser
#include "../common/socketoptions.hpp"

// NOTE: This is without "haisrt/" because it uses an internal path
// to the library. Application using the "installed" library should
// use <haisrt/srt.h>
#include <srt.h>
#include <logging.h>

// FEATURES when undefined or == 2, sets developer mode.
// When FEATURES == 1, it enforces user mode.
// In user mode SRT output is disabled.
#if defined(FEATURES) && FEATURES != 0
    #if FEATURES == 2
        #define DEVELOPER_MODE 1
        #warning USING DEVELOPER MODE
    #else
        #define DEVELOPER_MODE 0
        #warning USING USER MODE
    #endif
#else
#define DEVELOPER_MODE 1
#endif

// The length of the SRT payload used in srt_recvmsg call.
// So far, this function must be used and up to this length of payload.
const size_t DEFAULT_CHUNK = 1316;

using namespace std;

typedef std::vector<char> bytevector;

// This is based on codes taken from <sys/syslog.h>
// This is POSIX standard, so it's not going to change.
// Haivision standard only adds one more severity below
// DEBUG named DEBUG_TRACE to satisfy all possible needs.

map<string, int> srt_level_names
{
    { "alert", LOG_ALERT },
    { "crit", LOG_CRIT },
    { "debug", LOG_DEBUG },
    { "emerg", LOG_EMERG },
    { "err", LOG_ERR },
    { "error", LOG_ERR },		/* DEPRECATED */
    { "fatal", LOG_CRIT }, // XXX Added for SRT
    { "info", LOG_INFO },
    // { "none", INTERNAL_NOPRI },		/* INTERNAL */
    { "notice", LOG_NOTICE },
    { "note", LOG_NOTICE }, // XXX Added for SRT
    { "panic", LOG_EMERG },		/* DEPRECATED */
    { "warn", LOG_WARNING },		/* DEPRECATED */
    { "warning", LOG_WARNING },
    //{ "", -1 }
};



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

logging::LogLevel::type ParseLogLevel(string level)
{
    using namespace logging;

    if ( level.empty() )
        return LogLevel::fatal;

    if ( isdigit(level[0]) )
    {
        long lev = strtol(level.c_str(), 0, 10);
        if ( lev >= SRT_LOG_LEVEL_MIN && lev <= SRT_LOG_LEVEL_MAX )
            return LogLevel::type(lev);

        cerr << "ERROR: Invalid loglevel number: " << level << " - fallback to FATAL\n";
        return LogLevel::fatal;
    }

    int (*ToLower)(int) = &std::tolower; // manual overload resolution
    transform(level.begin(), level.end(), level.begin(), ToLower);

    auto i = srt_level_names.find(level);
    if ( i == srt_level_names.end() )
    {
        cerr << "ERROR: Invalid loglevel spec: " << level << " - fallback to FATAL\n";
        return LogLevel::fatal;
    }

    return LogLevel::type(i->second);
}

set<logging::LogFA> ParseLogFA(string fa)
{
    using namespace logging;

    set<LogFA> fas;

    // The split algo won't work on empty string.
    if ( fa == "" )
        return fas;

    static string names [] = { "general", "bstats", "control", "data", "tsbpd", "rexmit" };
    size_t names_s = sizeof (names)/sizeof (names[0]);

    if ( fa == "all" )
    {
        // Skip "general", it's always on
        fas.insert(SRT_LOGFA_BSTATS);
        fas.insert(SRT_LOGFA_CONTROL);
        fas.insert(SRT_LOGFA_DATA);
        fas.insert(SRT_LOGFA_TSBPD);
        fas.insert(SRT_LOGFA_REXMIT);
        return fas;
    }

    int (*ToLower)(int) = &std::tolower;
    transform(fa.begin(), fa.end(), fa.begin(), ToLower);

    vector<string> xfas;
    size_t pos = 0, ppos = 0;
    for (;;)
    {
        if ( fa[pos] != ',' )
        {
            ++pos;
            if ( pos < fa.size() )
                continue;
        }
        size_t n = pos - ppos;
        if ( n != 0 )
            xfas.push_back(fa.substr(ppos, n));
        ++pos;
        if ( pos >= fa.size() )
            break;
        ppos = pos;
    }

    for (size_t i = 0; i < xfas.size(); ++i)
    {
        fa = xfas[i];
        string* names_p = find(names, names + names_s, fa);
        if ( names_p == names + names_s )
        {
            cerr << "ERROR: Invalid log functional area spec: '" << fa << "' - skipping\n";
            continue;
        }

        size_t nfa = names_p - names;

        if ( nfa != 0 )
            fas.insert(nfa);
    }

    return fas;
}


template <class Base>
unique_ptr<Base> CreateMedium(const string& uri);

class Source
{
public:
    virtual bytevector Read(size_t chunk) = 0;
    virtual bool IsOpen() = 0;
    virtual bool End() = 0;
    static unique_ptr<Source> Create(const string& url)
    {
        return CreateMedium<Source>(url);
    }
    virtual ~Source() {}
};

class Target
{
public:
    virtual void Write(const bytevector& portion) = 0;
    virtual bool IsOpen() = 0;
    virtual bool Broken() = 0;
    static unique_ptr<Target> Create(const string& url)
    {
        return CreateMedium<Target>(url);
    }
    virtual ~Target() {}
};



map<string,string> g_options;

string Option(string deflt="") { return deflt; }

template <class... Args>
string Option(string deflt, string key, Args... further_keys)
{
    map<string, string>::iterator i = g_options.find(key);
    if ( i == g_options.end() )
        return Option(deflt, further_keys...);
    return i->second;
}

volatile bool int_state = false;
volatile bool throw_on_interrupt = false;
bool transmit_verbose = false;
ostream* cverb = &cout;
bool bidirectional = false;
unsigned srt_maxlossttl = 0;
unsigned stats_report_freq = 0;

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

struct BandwidthGuard
{
    typedef std::chrono::steady_clock::time_point time_point;
    size_t conf_bw;
    time_point start_time, prev_time;
    size_t report_count = 0;
    double average_bw = 0;
    size_t transfer_size = 0;

    BandwidthGuard(size_t band): conf_bw(band), start_time(std::chrono::steady_clock::now()), prev_time(start_time) {}

    void Checkpoint(size_t size, size_t toreport )
    {
        using namespace std::chrono;

        time_point eop = steady_clock::now();
        auto dur = duration_cast<microseconds>(eop - start_time);
        //auto this_dur = duration_cast<microseconds>(eop - prev_time);

        transfer_size += size;
        average_bw = transfer_size*1000000.0/dur.count();
        //double this_bw = size*1000000.0/this_dur.count();

        if ( toreport )
        {
            // Show current bandwidth
            ++report_count;
            if ( report_count % toreport == toreport - 1 )
            {
                cout.precision(10);
                int abw = int(average_bw);
                int abw_trunc = abw/1024;
                int abw_frac = abw%1024;
                char bufbw[64];
                sprintf(bufbw, "%d.%03d", abw_trunc, abw_frac);
                cout << "+++/+++SRT TRANSFER: " << transfer_size << "B "
                    "DURATION: "  << duration_cast<milliseconds>(dur).count() << "ms SPEED: " << bufbw << "kB/s\n";
            }
        }

        prev_time = eop;

        if ( transfer_size > SIZE_MAX/2 )
        {
            transfer_size -= SIZE_MAX/2;
            start_time = eop;
        }

        if ( conf_bw == 0 )
            return; // don't guard anything

        // Calculate expected duration for the given size of bytes (in [ms])
        double expdur_ms = double(transfer_size)/conf_bw*1000;

        auto expdur = milliseconds(size_t(expdur_ms));
        // Now compare which is more

        if ( dur >= expdur ) // too slow, but there's nothing we can do. Exit now.
            return;

        std::this_thread::sleep_for(expdur-dur);
    }
};

int bw_report = 0;

extern "C" void TestLogHandler(void* opaque, int level, const char* file, int line, const char* area, const char* message);

int main( int argc, char** argv )
{
    // This is mainly required on Windows to initialize the network system,
    // for a case when the instance would use UDP. SRT does it on its own, independently.
    if ( !SysInitializeNetwork() )
        throw std::runtime_error("Can't initialize network!");

    // Symmetrically, this does a cleanup; put into a local destructor to ensure that
    // it's called regardless of how this function returns.
    struct NetworkCleanup
    {
        ~NetworkCleanup()
        {
            SysCleanupNetwork();
        }
    } cleanupobj;

    vector<string> args;
    copy(argv+1, argv+argc, back_inserter(args));

    // Check options
    vector<string> params;

    for (string a: args)
    {
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
        cerr << "Usage: " << argv[0] << " [options] <input-uri> <output-uri>\n";
        cerr << "\t-t:<timeout=0> - connection timeout\n";
        cerr << "\t-c:<chunk=1316> - max size of data read in one step\n";
        cerr << "\t-b:<bandwidth> - set SRT bandwidth\n";
        cerr << "\t-r:<report-frequency=0> - bandwidth report frequency\n";
        cerr << "\t-s:<stats-report-freq=0> - frequency of status report\n";
        cerr << "\t-k - crash on error (aka developer mode)\n";
        cerr << "\t-v - verbose mode (prints also size of every data packet passed)\n";
        return 1;
    }

    int timeout = stoi(Option("30", "t", "to", "timeout"), 0, 0);
    size_t chunk = stoul(Option("0", "c", "chunk"), 0, 0);
    if ( chunk == 0 )
        chunk = DEFAULT_CHUNK;
    size_t bandwidth = stoul(Option("0", "b", "bandwidth", "bitrate"), 0, 0);
    bw_report = stoul(Option("0", "r", "report", "bandwidth-report", "bitrate-report"), 0, 0);
    transmit_verbose = Option("no", "v", "verbose") != "no";
    bool crashonx = Option("no", "k", "crash") != "no";
    bidirectional = Option("no", "2", "rw", "bidirectional") != "no";

    string loglevel = Option("error", "loglevel");
    string logfa = Option("general", "logfa");
    string logfile = Option("", "logfile");
    srt_maxlossttl = stoi(Option("0", "ttl", "max-loss-delay"));
    stats_report_freq = stoi(Option("0", "s", "stats", "stats-report-frequency"), 0, 0);

    bool internal_log = Option("no", "loginternal") != "no";

    std::ofstream logfile_stream; // leave unused if not set

    srt_setloglevel(ParseLogLevel(loglevel));
    set<logging::LogFA> fas = ParseLogFA(logfa);
    for (set<logging::LogFA>::iterator i = fas.begin(); i != fas.end(); ++i)
        srt_addlogfa(*i);

    char NAME[] = "SRTLIB";
    if ( internal_log )
    {
        srt_setlogflags( 0
                | SRT_LOGF_DISABLE_TIME
                | SRT_LOGF_DISABLE_SEVERITY
                | SRT_LOGF_DISABLE_THREADNAME
                | SRT_LOGF_DISABLE_EOL
                );
        srt_setloghandler(NAME, TestLogHandler);
    }
    else if ( logfile != "" )
    {
        logfile_stream.open(logfile.c_str());
        if ( !logfile_stream )
        {
            cerr << "ERROR: Can't open '" << logfile << "' for writing - fallback to cerr\n";
        }
        else
        {
            UDT::setlogstream(logfile_stream);
        }
    }

#ifdef WIN32
#define alarm(argument) (void)0
#else
    signal(SIGALRM, OnAlarm_Interrupt);
#endif
    signal(SIGINT, OnINT_SetIntState);
    signal(SIGTERM, OnINT_SetIntState);

    try
    {
        auto src = Source::Create(params[0]);
        auto tar = Target::Create(params[1]);

        // Now loop until broken
        BandwidthGuard bw(bandwidth);

        if ( transmit_verbose )
        {
            cout << "STARTING TRANSMISSION: '" << params[0] << "' --> '" << params[1] << "'\n";
        }

        extern logging::Logger glog;
        for (;;)
        {
            if ( timeout != -1 )
            {
                alarm(timeout);
            }
            const bytevector& data = src->Read(chunk);
            if ( transmit_verbose )
                cout << " << " << data.size() << "  ->  ";
            if ( data.empty() && src->End() )
            {
                if ( transmit_verbose )
                    cout << "EOS\n";
                break;
            }
            tar->Write(data);
            if ( timeout != -1 )
            {
                alarm(0);
            }
            if ( tar->Broken() )
            {
                if ( transmit_verbose )
                    cout << " OUTPUT broken\n";
                break;
            }
            if ( transmit_verbose )
                cout << " sent\n";
            if ( int_state )
            {
                cerr << "\n (interrupted on request)\n";
                break;
            }

            bw.Checkpoint(chunk, bw_report);
        }
        alarm(0);

    } catch (...) {
        if ( crashonx )
            throw;

        return 1;
    }
    return 0;
}

// Class utilities

string udt_status_names [] = {
"INIT" , "OPENED", "LISTENING", "CONNECTING", "CONNECTED", "BROKEN", "CLOSING", "CLOSED", "NONEXIST"
};

// Medium concretizations

class FileSource: public Source
{
    ifstream ifile;
public:

    FileSource(const string& path): ifile(path, ios::in | ios::binary) {}

    bytevector Read(size_t chunk) override
    {
        bytevector data(chunk);
        ifile.read(data.data(), chunk);
        size_t nread = size_t(ifile.gcount());
        if ( nread < data.size() )
            data.resize(nread);
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
};

template <class Iface> struct File;
template <> struct File<Source> { typedef FileSource type; };
template <> struct File<Target> { typedef FileTarget type; };

template <class Iface>
Iface* CreateFile(const string& name) { return new typename File<Iface>::type (name); }




class SrtCommon
{
    int srt_conn_epoll = -1;
protected:

    bool m_output_direction = false; //< Defines which of SND or RCV option variant should be used, also to set SRT_SENDER for output
    bool m_blocking_mode = true; //< enforces using SRTO_SNDSYN or SRTO_RCVSYN, depending on @a m_output_direction
    int m_timeout = 0; //< enforces using SRTO_SNDTIMEO or SRTO_RCVTIMEO, depending on @a m_output_direction
    bool m_tsbpdmode = true;
    map<string, string> m_options; // All other options, as provided in the URI
    SRTSOCKET m_sock = SRT_INVALID_SOCK;
    SRTSOCKET m_bindsock = SRT_INVALID_SOCK;
    bool IsUsable() { SRT_SOCKSTATUS st = srt_getsockstate(m_sock); return st > SRTS_INIT && st < SRTS_BROKEN; }
    bool IsBroken() { return srt_getsockstate(m_sock) > SRTS_CONNECTED; }

    void Init(string host, int port, map<string,string> par, bool dir_output)
    {
        m_output_direction = dir_output;

        // Application-specific options: mode, blocking, timeout, adapter
        if ( transmit_verbose )
        {
            cout << "Parameters:\n";
            for (map<string,string>::iterator i = par.begin(); i != par.end(); ++i)
            {
                cout << "\t" << i->first << " = '" << i->second << "'\n";
            }
        }

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
            m_blocking_mode = !false_names.count(par.at("blocking"));
            par.erase("blocking");
        }

        if ( par.count("timeout") )
        {
            m_timeout = stoi(par.at("timeout"), 0, 0);
            par.erase("timeout");
        }

        string adapter = ""; // needed for rendezvous only
        if ( par.count("adapter") )
        {
            adapter = par.at("adapter");
            par.erase("adapter");
        }

        if ( par.count("tsbpd") && false_names.count(par.at("tsbpd")) )
        {
            m_tsbpdmode = false;
        }

        // Assign the others here.
        m_options = par;

        if ( transmit_verbose )
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

    int AddPoller(SRTSOCKET socket, int modes)
    {
        int pollid = srt_epoll_create();
        if ( pollid == -1 )
            throw std::runtime_error("Can't create epoll in nonblocking mode");
        srt_epoll_add_usock(pollid, socket, &modes);
        return pollid;
    }

    virtual int ConfigurePost(SRTSOCKET sock)
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

    virtual int ConfigurePre(SRTSOCKET sock)
    {
        int result = 0;

        int no = 0;
        if ( !m_tsbpdmode )
        {
            result = srt_setsockopt(sock, 0, SRTO_TSBPDMODE, &no, sizeof no);
            if ( result == -1 )
                return result;
        }

        if ( ::srt_maxlossttl != 0 )
        {
            result = srt_setsockopt(sock, 0, SRTO_LOSSMAXTTL, &srt_maxlossttl, sizeof srt_maxlossttl);
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

    void OpenClient(string host, int port)
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
            Error(UDT::getlasterror(), "UDT::connect");
        }

        if ( !m_blocking_mode )
        {
            if ( transmit_verbose )
                cout << "[ASYNC] " << flush;

            /* SPIN-WAITING version. Don't use it unless you know what you're doing.
               
            for (;;)
            {
                int state = UDT::getsockstate(m_sock);
                if ( state < CONNECTED )
                {
                    if ( verbose )
                        cout << state << flush;
                    usleep(250000);
                    continue;
                }
                else if ( state > CONNECTED )
                {
                    Error(UDT::getlasterror(), "UDT::connect status=" + udt_status_names[state]);
                }

                stat = 0; // fake that connect() returned 0
                break;
            }
            */

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

    void Error(UDT::ERRORINFO& udtError, string src)
    {
        int udtResult = udtError.getErrorCode();
        if ( transmit_verbose )
        cout << "FAILURE\n" << src << ": [" << udtResult << "] " << udtError.getErrorMessage() << endl;
        udtError.clear();
        throw std::invalid_argument("error in " + src);
    }

    void OpenServer(string host, int port)
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
        stat = srt_listen(m_bindsock, 1);
        if ( stat == SRT_ERROR )
        {
            srt_close(m_bindsock);
            Error(UDT::getlasterror(), "srt_listen");
        }

        sockaddr_in scl;
        int sclen = sizeof scl;
        if ( transmit_verbose )
        {
            cout << " accept... ";
            cout.flush();
        }
        ::throw_on_interrupt = true;

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

        m_sock = srt_accept(m_bindsock, (sockaddr*)&scl, &sclen);
        if ( m_sock == SRT_INVALID_SOCK )
        {
            srt_close(m_bindsock);
            Error(UDT::getlasterror(), "srt_accept");
        }

        if ( transmit_verbose )
            cout << " connected.\n";
        ::throw_on_interrupt = false;

        // ConfigurePre is done on bindsock, so any possible Pre flags
        // are DERIVED by sock. ConfigurePost is done exclusively on sock.
        stat = ConfigurePost(m_sock);
        if ( stat == SRT_ERROR )
            Error(UDT::getlasterror(), "ConfigurePost");
    }

    void OpenRendezvous(string adapter, string host, int port)
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
            srt_conn_epoll = AddPoller(m_bindsock, SRT_EPOLL_OUT);
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

        if ( transmit_verbose )
            cout << " connected.\n";

        stat = ConfigurePost(m_sock);
        if ( stat == SRT_ERROR )
            Error(UDT::getlasterror(), "ConfigurePost");
    }

    ~SrtCommon()
    {
        if ( transmit_verbose )
            cout << "SrtCommon: DESTROYING CONNECTION, closing sockets\n";
        if ( m_sock != UDT::INVALID_SOCK )
            srt_close(m_sock);

        if ( m_bindsock != UDT::INVALID_SOCK )
            srt_close(m_bindsock);
    }
};

class SrtSource: public Source, public SrtCommon
{
    int srt_epoll = -1;
public:

    SrtSource(string host, int port, const map<string,string>& par)
    {
        Init(host, port, par, false);

        if ( !m_blocking_mode )
        {
            srt_epoll = AddPoller(m_sock, SRT_EPOLL_IN);
        }
    }

    bytevector Read(size_t chunk) override
    {
        static size_t counter = 1;

        bytevector data(chunk);
        bool ready = true;
        int stat;
        do
        {
            ::throw_on_interrupt = true;
            stat = srt_recvmsg(m_sock, data.data(), chunk);
            ::throw_on_interrupt = false;
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
                return bytevector();
            }

            if ( stat == 0 )
            {
                // Not necessarily eof. Closed connection is reported as error.
                this_thread::sleep_for(chrono::milliseconds(10));
                ready = false;
            }
        }
        while (!ready);

        chunk = size_t(stat);
        if ( chunk < data.size() )
            data.resize(chunk);

        CBytePerfMon perf;
        srt_bstats(m_sock, &perf, false);
        if ( bw_report && int(counter % bw_report) == bw_report - 1 )
        {
            cout << "+++/+++SRT BANDWIDTH: " << perf.mbpsBandwidth << endl;
        }

        if ( stats_report_freq && counter % stats_report_freq == stats_report_freq - 1)
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

    virtual int ConfigurePre(UDTSOCKET sock) override
    {
        int result = SrtCommon::ConfigurePre(sock);
        if ( result == -1 )
            return result;
        // For sending party, the SRT_SENDER flag must be set, otherwise
        // the connection will be pure UDT.
        int yes = 1;

        if ( ::bidirectional )
        {
            result = srt_setsockopt(sock, 0, SRTO_TWOWAYDATA, &yes, sizeof yes);
            if ( result == -1 )
                return result;
        }

        return 0;
    }

    bool IsOpen() override { return IsUsable(); }
    bool End() override { return IsBroken(); }
};

class SrtTarget: public Target, public SrtCommon
{
    int srt_epoll = -1;
public:

    SrtTarget(string host, int port, const map<string,string>& par)
    {
        Init(host, port, par, true);

        if ( !m_blocking_mode )
        {
            srt_epoll = AddPoller(m_sock, SRT_EPOLL_OUT);
        }
    }

    virtual int ConfigurePre(SRTSOCKET sock) override
    {
        int result = SrtCommon::ConfigurePre(sock);
        if ( result == -1 )
            return result;
        // For sending party, the SRT_SENDER flag must be set, otherwise
        // the connection will be pure UDT.
        int yes = 1;

        if ( ::bidirectional )
        {
            result = srt_setsockopt(sock, 0, SRTO_TWOWAYDATA, &yes, sizeof yes);
            if ( result == -1 )
                return result;
        }
        else
        {
            result = srt_setsockopt(sock, 0, SRTO_SENDER, &yes, sizeof yes);
            if ( result == -1 )
                return result;
        }

        return 0;
    }

    void Write(const bytevector& data) override
    {
        ::throw_on_interrupt = true;

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
        ::throw_on_interrupt = false;
    }

    bool IsOpen() override { return IsUsable(); }
    bool Broken() override { return IsBroken(); }

};

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
        chunk = size_t(cin.gcount());
        if ( chunk == 0 && !st )
            return bytevector();

        if ( chunk < data.size() )
            data.resize(chunk);

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
    { "iptos", IPPROTO_IP, IP_TOS, SocketOption::INT, SocketOption::PRE },
    // IP_TTL and IP_MULTICAST_TTL are handled separately by a common option, "ttl".
    { "mcloop", IPPROTO_IP, IP_MULTICAST_LOOP, SocketOption::INT, SocketOption::PRE }
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
        {
            perror("UdpCommon:socket");
            throw std::runtime_error("UdpCommon: failed to create a socket");
        }

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
                maddr.sin_family = AF_INET;
                maddr.sin_addr.s_addr = htonl(INADDR_ANY);
                maddr.sin_port = htons(port); // necessary for temporary use     
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

#if defined(WIN32) || defined(__CYGWIN__)
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
#endif

            int res = setsockopt(m_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, mreq_arg, sizeof(mreq));

            if ( res == status_error )
            {
                throw runtime_error("adding to multicast membership failed");
            }
            attr.erase("multicast");
            attr.erase("adapter");
        }

        // The "ttl" options is handled separately, it maps to either IP_TTL
        // or IP_MULTICAST_TTL, depending on whether the address is sc or mc.
        if (attr.count("ttl"))
        {
            int ttl = stoi(attr.at("ttl"));
            int res = setsockopt(m_sock, IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof ttl);
            if (res == -1)
                cout << "WARNING: failed to set 'ttl' (IP_TTL) to " << ttl << endl;
            res = setsockopt(m_sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof ttl);
            if (res == -1)
                cout << "WARNING: failed to set 'ttl' (IP_MULTICAST_TTL) to " << ttl << endl;

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
unique_ptr<Base> CreateMedium(const string& uri)
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
                        (transmit_verbose && cverb == &cout)
                        || bw_report) )
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
#if !DEVELOPER_MODE
        if ( IsOutput<Base>() )
        {
            cerr << "SRT output not supported\n";
            throw invalid_argument("incorrect output");
        }
#endif
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

    return ptr;
}


void TestLogHandler(void* opaque, int level, const char* file, int line, const char* area, const char* message)
{
    char prefix[100] = "";
    if ( opaque )
        strncpy(prefix, (char*)opaque, 99);
    time_t now;
    time(&now);
    char buf[1024];
    struct tm local = LocalTime(now);
    size_t pos = strftime(buf, 1024, "[%c ", &local);

#ifdef _MSC_VER
    // That's something weird that happens on Microsoft Visual Studio 2013
    // Trying to keep portability, while every version of MSVS is a different plaform.
    // On MSVS 2015 there's already a standard-compliant snprintf, whereas _snprintf
    // is available on backward compatibility and it doesn't work exactly the same way.
#define snprintf _snprintf
#endif
    snprintf(buf+pos, 1024-pos, "%s:%d(%s)]{%d} %s", file, line, area, level, message);

    cerr << buf << endl;
}

