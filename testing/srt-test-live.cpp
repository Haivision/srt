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
// the way how it's invoked: srt-test-live <source> <target> (plus options).
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
// See 'srt_options' global variable (common/socketoptions.hpp) for a list of
// all options.

// MSVS likes to complain about lots of standard C functions being unsafe.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#define REQUIRE_CXX11 1

#include <cctype>
#include <iostream>
#include <fstream>
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

#include "apputil.hpp"  // CreateAddrInet
#include "uriparser.hpp"  // UriParser
#include "socketoptions.hpp"
#include "logsupport.hpp"
#include "testmedia.hpp"
#include "testmedia.hpp" // requires access to SRT-dependent globals
#include "verbose.hpp"

// NOTE: This is without "haisrt/" because it uses an internal path
// to the library. Application using the "installed" library should
// use <srt/srt.h>
#include <srt.h>
#include <udt.h> // This TEMPORARILY contains extra C++-only SRT API.
#include <logging.h>

using namespace std;

srt_logging::Logger applog(SRT_LOGFA_APP, srt_logger_config, "srt-live");

map<string,string> g_options;

struct ForcedExit: public std::runtime_error
{
    ForcedExit(const std::string& arg):
        std::runtime_error(arg)
    {
    }
};

struct AlarmExit: public std::runtime_error
{
    AlarmExit(const std::string& arg):
        std::runtime_error(arg)
    {
    }
};

volatile bool int_state = false;
volatile bool timer_state = false;
void OnINT_ForceExit(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    int_state = true;
    if ( transmit_throw_on_interrupt )
        throw ForcedExit("Requested exception interrupt");
}

void OnAlarm_Interrupt(int)
{
    cerr << "\n---------- INTERRUPT ON TIMEOUT!\n";
    int_state = false; // JIC
    timer_state = true;
    throw AlarmExit("Watchdog bites hangup");
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

extern "C" void TestLogHandler(void* opaque, int level, const char* file, int line, const char* area, const char* message);

namespace srt_logging
{
    extern Logger glog;
}

extern "C" int SrtUserPasswordHook(void* , SRTSOCKET listener, int hsv, const sockaddr*, const char* streamid)
{
    if (hsv < 5)
    {
        Verb() << "SrtUserPasswordHook: HS version 4 doesn't support extended handshake";
        return -1;
    }

    static const map<string, string> passwd {
        {"admin", "thelocalmanager"},
        {"user", "verylongpassword"}
    };

    // Try the "standard interpretation" with username at key u
    string username;

    static const char stdhdr [] = "#!::";
    uint32_t* pattern = (uint32_t*)stdhdr;

    if (strlen(streamid) > 4 && *(uint32_t*)streamid == *pattern)
    {
        vector<string> items;
        Split(streamid+4, ',', back_inserter(items));
        for (auto& i: items)
        {
            vector<string> kv;
            Split(i, '=', back_inserter(kv));
            if (kv.size() == 2 && kv[0] == "u")
            {
                username = kv[1];
            }
        }
    }
    else
    {
        // By default the whole streamid is username
        username = streamid;
    }

    // This hook sets the password to the just accepted socket
    // depending on the user

    string exp_pw = passwd.at(username);

    srt_setsockflag(listener, SRTO_PASSPHRASE, exp_pw.c_str(), exp_pw.size());

    return 0;
}

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

    vector<OptionScheme> optargs;

    OptionName
        o_timeout   ((optargs), "<timeout[s]=0> Data transmission timeout", "t",   "to", "timeout" ),
        o_chunk     ((optargs), "<chunk=1316> Single reading operation buffer size", "c",   "chunk"),
        o_bandwidth ((optargs), "<bw[ms]=0[unlimited]> Input reading speed limit", "b",   "bandwidth", "bitrate"),
        o_report    ((optargs), "<frequency[1/pkt]=0> Print bandwidth report periodically", "r",   "bandwidth-report", "bitrate-report"),
        o_verbose   ((optargs), " Print size of every packet transferred on stdout", "v",   "verbose"),
        o_crash     ((optargs), " Core-dump when connection got broken by whatever reason (developer mode)", "k",   "crash"),
        o_loglevel  ((optargs), "<severity=fatal|error|note|warning|debug> Minimum severity for logs", "ll",  "loglevel"),
        o_logfa     ((optargs), "<FA=all> Enabled Functional Areas", "lfa", "logfa"),
        o_logfile   ((optargs), "<filepath> File to send logs to", "lf",  "logfile"),
        o_stats     ((optargs), "<freq[npkt]> How often stats should be reported", "s",   "stats", "stats-report-frequency"),
        o_statspf   ((optargs), "<format=default|csv|json> Format for printing statistics", "pf", "statspf", "statspformat"),
        o_logint    ((optargs), " Use internal function for receiving logs (for testing)",        "loginternal"),
        o_skipflush ((optargs), " Do not wait safely 5 seconds at the end to flush buffers", "sf",  "skipflush"),
        o_stoptime  ((optargs), "<time[s]=0[no timeout]> Time after which the application gets interrupted", "d", "stoptime"),
        o_help      ((optargs), " This help", "?",   "help", "-help")
            ;

    options_t params = ProcessOptions(argv, argc, optargs);

    bool need_help = OptionPresent(params, o_help);

    vector<string> args = params[""];

    string source_spec, target_spec;

    if (!need_help)
    {
        // You may still need help.

        if (args.size() < 2)
        {
            cerr << "ERROR: source and target URI must be specified.\n\n";
            need_help = true;
        }
        else
        {
            source_spec = args[0];
            target_spec = args[1];
        }
    }

    if (need_help)
    {
        cerr << "Usage:\n";
        cerr << "     " << argv[0] << " [options] <input> <output>\n";
        cerr << "*** (Position of [options] is unrestricted.)\n";
        cerr << "*** (<variadic...> option parameters can be only terminated by a next option.)\n";
        cerr << "where:\n";
        cerr << "    <input> and <output> is specified by an URI.\n";
        cerr << "SUPPORTED URI SCHEMES:\n";
        cerr << "    srt: use SRT connection\n";
        cerr << "    udp: read from bound UDP socket or send to given address as UDP\n";
        cerr << "    file (default if scheme not specified) specified as:\n";
        cerr << "       - empty host/port and absolute file path in the URI\n";
        cerr << "       - only a filename, also as a relative path\n";
        cerr << "       - file://con ('con' as host): designates stdin or stdout\n";
        cerr << "OPTIONS HELP SYNTAX: -option <parameter[unit]=default[meaning]>:\n";
        for (auto os: optargs)
            cout << OptionHelpItem(*os.pid) << endl;
        return 1;
    }

    int timeout = Option<OutNumber>(params, "30", o_timeout);
    size_t chunk = Option<OutNumber>(params, "0", o_chunk);
    if ( chunk == 0 )
    {
        chunk = SRT_LIVE_DEF_PLSIZE;
    }
    else
    {
        transmit_chunk_size = chunk;
    }
    
    size_t bandwidth = Option<OutNumber>(params, "0", o_bandwidth);
    transmit_bw_report = Option<OutNumber>(params, "0", o_report);
    string verbose_val = Option<OutString>(params, "no", o_verbose);

    int verbch = 1; // default cerr
    if (verbose_val != "no")
    {
        Verbose::on = true;
        try
        {
            verbch = stoi(verbose_val);
        }
        catch (...)
        {
            verbch = 1;
        }
        if (verbch != 1)
        {
            if (verbch != 2)
            {
                cerr << "-v or -v:1 (default) or -v:2 only allowed\n";
                return 1;
            }
            Verbose::cverb = &std::cerr;
        }
        else
        {
            Verbose::cverb = &std::cout;
        }
    }

    bool crashonx = OptionPresent(params, o_crash);

    string loglevel = Option<OutString>(params, "error", o_loglevel);
    string logfa = Option<OutString>(params, "general", o_logfa);
    string logfile = Option<OutString>(params, "", o_logfile);
    transmit_stats_report = Option<OutNumber>(params, "0", o_stats);

    bool internal_log = OptionPresent(params, o_logint);
    bool skip_flushing = OptionPresent(params, o_skipflush);

    string hook = Option<OutString>(params, "", "hook");
    if (hook != "")
    {
        if (hook == "user-password")
        {
            transmit_accept_hook_fn = &SrtUserPasswordHook;
            transmit_accept_hook_op = nullptr;
        }
    }

    SrtStatsPrintFormat statspf = ParsePrintFormat(Option<OutString>(params, "default", o_statspf));
    if (statspf == SRTSTATS_PROFMAT_INVALID)
    {
        cerr << "Invalid stats print format\n";
        return 1;
    }
    transmit_stats_writer = SrtStatsWriterFactory(statspf);

    // Options that require integer conversion
    size_t stoptime = Option<OutNumber>(params, "0", o_stoptime);
    std::ofstream logfile_stream; // leave unused if not set

    srt_setloglevel(SrtParseLogLevel(loglevel));
    set<srt_logging::LogFA> fas = SrtParseLogFA(logfa);
    for (set<srt_logging::LogFA>::iterator i = fas.begin(); i != fas.end(); ++i)
        srt_addlogfa(*i);


    UDT::addlogfa(SRT_LOGFA_APP);

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


#ifdef _WIN32
#define alarm(argument) (void)0

    if (stoptime != 0)
    {
        cerr << "ERROR: The -stoptime option (-d) is not implemented on Windows\n";
        return 1;
    }

#else
    signal(SIGALRM, OnAlarm_Interrupt);
#endif
    signal(SIGINT, OnINT_ForceExit);
    signal(SIGTERM, OnINT_ForceExit);

    time_t start_time { time(0) };
    time_t end_time { -1 };

    if (stoptime != 0)
    {
        if (stoptime < 10)
        {
            cerr << "ERROR: -stoptime (-d) must be at least 10 seconds\n";
            return 1;
        }
        alarm(stoptime);
        cerr << "STOPTIME: will interrupt after " << stoptime << "s\n";
        if (timeout != 30)
        {
            cerr << "WARNING: -timeout (-t) option ignored due to specified -stoptime (-d)\n";
        }
    }

    // XXX This could be also controlled by an option.
    int final_delay = 5;

    // In the beginning, set Alarm 

    unique_ptr<Source> src;
    unique_ptr<Target> tar;

    try
    {
        src = Source::Create(source_spec);
        tar = Target::Create(target_spec);
    }
    catch(std::exception& x)
    {
        if (::int_state)
        {
            // The application was terminated by SIGINT or SIGTERM.
            // Don't print anything, just exit gently like ffmpeg.
            cerr << "Exit on request.\n";
            return 255;
        }

        if (stoptime != 0 && ::timer_state)
        {
            cerr << "Exit on timeout.\n";
            return 0;
        }

        Verb() << "MEDIA CREATION FAILED: " << x.what() << " - exitting.";

        // Don't speak anything when no -v option.
        // (the "requested interrupt" will be printed anyway)
        return 2;
    }
    catch (...)
    {
        cerr << "ERROR: UNKNOWN EXCEPTION\n";
        return 2;
    }

    alarm(0);
    end_time = time(0);

    // Now loop until broken
    BandwidthGuard bw(bandwidth);

    Verb() << "STARTING TRANSMISSION: '" << args[0] << "' --> '" << args[1] << "'";

    // After the time has been spent in the creation
    // (including waiting for connection)
    // rest of the time should be spent for transmission.
    if (stoptime != 0)
    {
        int elapsed = end_time - start_time;
        int remain = stoptime - elapsed;

        if (remain <= final_delay)
        {
            cerr << "NOTE: remained too little time for cleanup: " << remain << "s - exitting\n";
            return 0;
        }

        cerr << "NOTE: stoptime: remaining " << remain << " seconds (setting alarm to " << (remain - final_delay) << "s)\n";
        alarm(remain - final_delay);
    }

    try
    {
        for (;;)
        {
            if (stoptime == 0 && timeout != -1 )
            {
                alarm(timeout);
            }
            Verb() << " << ... " << VerbNoEOL;
            const bytevector& data = src->Read(chunk);
            Verb() << " << " << data.size() << "  ->  " << VerbNoEOL;
            if ( data.empty() && src->End() )
            {
                Verb() << "EOS";
                break;
            }
            tar->Write(data);
            if (stoptime == 0 && timeout != -1 )
            {
                alarm(0);
            }

            if ( tar->Broken() )
            {
                Verb() << " OUTPUT broken";
                break;
            }

            Verb() << "sent";

            if ( int_state )
            {
                Verror() << "\n (interrupted on request)";
                break;
            }

            bw.Checkpoint(chunk, transmit_bw_report);

            if (stoptime != 0)
            {
                int elapsed = time(0) - end_time;
                int remain = stoptime - final_delay - elapsed;
                if (remain < 0)
                {
                    Verror() << "\n (interrupted on timeout: elapsed " << elapsed << "s) - waiting " << final_delay << "s for cleanup";
                    this_thread::sleep_for(chrono::seconds(final_delay));
                    break;
                }
            }
        }

    } catch (Source::ReadEOF&) {
        alarm(0);

        if (!skip_flushing)
        {
            Verror() << "(DEBUG) EOF when reading file. Looping until the sending bufer depletes.\n";
            for (;;)
            {
                size_t still = tar->Still();
                if (still == 0)
                {
                    Verror() << "(DEBUG) DEPLETED. Done.\n";
                    break;
                }

                Verror() << "(DEBUG)... still " << still << " bytes (sleep 1s)\n";
                this_thread::sleep_for(chrono::seconds(1));
            }
        }
    } catch (std::exception& x) { // Catches TransmissionError and AlarmExit

        if (stoptime != 0 && ::timer_state)
        {
            Verror() << "Exit on timeout.";
        }
        else if (::int_state)
        {
            Verror() << "Exit on interrupt.";
            // Do nothing.
        }
        else
        {
            Verror() << "STD EXCEPTION: " << x.what();
        }

        if ( crashonx )
            throw;

        if (final_delay > 0)
        {
            Verror() << "Waiting " << final_delay << "s for possible cleanup...";
            this_thread::sleep_for(chrono::seconds(final_delay));
        }
        if (stoptime != 0 && ::timer_state)
            return 0;

        return 255;

    } catch (...) {

        Verror() << "UNKNOWN type of EXCEPTION";
        if ( crashonx )
            throw;

        return 1;
    }

    return 0;
}

// Class utilities


void TestLogHandler(void* opaque, int level, const char* file, int line, const char* area, const char* message)
{
    char prefix[100] = "";
    if ( opaque )
        strncpy(prefix, (char*)opaque, 99);
    time_t now;
    time(&now);
    char buf[1024];
    struct tm local = SysLocalTime(now);
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

