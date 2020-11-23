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
// the way how it's invoked: srt-live-transmit <source> <target> (plus options).
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
#include <list>


#include "apputil.hpp"  // CreateAddr
#include "uriparser.hpp"  // UriParser
#include "socketoptions.hpp"
#include "logsupport.hpp"
#include "transmitmedia.hpp"
#include "verbose.hpp"

// NOTE: This is without "haisrt/" because it uses an internal path
// to the library. Application using the "installed" library should
// use <srt/srt.h>
#include <srt.h>
#include <udt.h> // This TEMPORARILY contains extra C++-only SRT API.
#include <logging.h>

using namespace std;



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
    Verb() << "\n-------- REQUESTED INTERRUPT!\n";
    int_state = true;
}

void OnAlarm_Interrupt(int)
{
    Verb() << "\n---------- INTERRUPT ON TIMEOUT!\n";

    int_state = false; // JIC
    timer_state = true;

    if ((false))
    {
        throw AlarmExit("Watchdog bites hangup");
    }
}

extern "C" void TestLogHandler(void* opaque, int level, const char* file, int line, const char* area, const char* message);



struct LiveTransmitConfig
{
    int timeout = 0;
    int timeout_mode = 0;
    int chunk_size = -1;
    bool quiet = false;
    srt_logging::LogLevel::type loglevel = srt_logging::LogLevel::error;
    set<srt_logging::LogFA> logfas;
    bool log_internal;
    string logfile;
    int bw_report = 0;
    bool srctime = false;
    size_t buffering = 10;
    int stats_report = 0;
    string stats_out;
    SrtStatsPrintFormat stats_pf = SRTSTATS_PROFMAT_2COLS;
    bool auto_reconnect = true;
    bool full_stats = false;

    string source;
    string target;
};


void PrintOptionHelp(const OptionName& opt_names, const string &value, const string &desc)
{
    cerr << "\t";
    int i = 0;
    for (auto opt : opt_names.names)
    {
        if (i++) cerr << ", ";
        cerr << "-" << opt;
    }

    if (!value.empty())
        cerr << ":"  << value;
    cerr << "\t- " << desc << "\n";
}


int parse_args(LiveTransmitConfig &cfg, int argc, char** argv)
{
    const OptionName
        o_timeout       = { "t", "to", "timeout" },
        o_timeout_mode  = { "tm", "timeout-mode" },
        o_autorecon     = { "a", "auto", "autoreconnect" },
        o_chunk         = { "c", "chunk" },
        o_bwreport      = { "r", "bwreport", "report", "bandwidth-report", "bitrate-report" },
        o_srctime       = {"st", "srctime", "sourcetime"},
        o_buffering     = {"buffering"},
        o_statsrep      = { "s", "stats", "stats-report-frequency" },
        o_statsout      = { "statsout" },
        o_statspf       = { "pf", "statspf" },
        o_statsfull     = { "f", "fullstats" },
        o_loglevel      = { "ll", "loglevel" },
        o_logfa         = { "lfa", "logfa" },
        o_log_internal  = { "loginternal"},
        o_logfile       = { "logfile" },
        o_quiet         = { "q", "quiet" },
        o_verbose       = { "v", "verbose" },
        o_help          = { "h", "help" },
        o_version       = { "version" };

    const vector<OptionScheme> optargs = {
        { o_timeout,      OptionScheme::ARG_ONE },
        { o_timeout_mode, OptionScheme::ARG_ONE },
        { o_autorecon,    OptionScheme::ARG_ONE },
        { o_chunk,        OptionScheme::ARG_ONE },
        { o_bwreport,     OptionScheme::ARG_ONE },
        { o_srctime,      OptionScheme::ARG_ONE },
        { o_buffering,    OptionScheme::ARG_ONE },
        { o_statsrep,     OptionScheme::ARG_ONE },
        { o_statsout,     OptionScheme::ARG_ONE },
        { o_statspf,      OptionScheme::ARG_ONE },
        { o_statsfull,    OptionScheme::ARG_NONE },
        { o_loglevel,     OptionScheme::ARG_ONE },
        { o_logfa,        OptionScheme::ARG_ONE },
        { o_log_internal, OptionScheme::ARG_NONE },
        { o_logfile,      OptionScheme::ARG_ONE },
        { o_quiet,        OptionScheme::ARG_NONE },
        { o_verbose,      OptionScheme::ARG_NONE },
        { o_help,         OptionScheme::ARG_VAR },
        { o_version,      OptionScheme::ARG_NONE }
    };

    options_t params = ProcessOptions(argv, argc, optargs);

          bool print_help    = OptionPresent(params, o_help);
    const bool print_version = OptionPresent(params, o_version);

    if (params[""].size() != 2 && !print_help && !print_version)
    {
        cerr << "ERROR. Invalid syntax. Specify source and target URIs.\n";
        if (params[""].size() > 0)
        {
            cerr << "The following options are passed without a key: ";
            copy(params[""].begin(), params[""].end(), ostream_iterator<string>(cerr, ", "));
            cerr << endl;
        }
        print_help = true; // Enable help to print it further
    }

    if (print_help)
    {
        string helpspec = Option<OutString>(params, o_help);

        if (helpspec == "logging")
        {
            cerr << "Logging options:\n";
            cerr << "    -ll <LEVEL>   - specify minimum log level\n";
            cerr << "    -lfa <area...> - specify functional areas\n";
            cerr << "Where:\n\n";
            cerr << "    <LEVEL>: fatal error note warning debug\n\n";
            cerr << "Turns on logs that are at the given log level or any higher level\n";
            cerr << "(all to the left in the list above from the selected level).\n";
            cerr << "Names from syslog, like alert, crit, emerg, err, info, panic, are also\n";
            cerr << "recognized, but they are aligned to those that lie close in the above hierarchy.\n\n";
            cerr << "    <area...> is a coma-separated list of areas to turn on.\n\n";
            cerr << "The list may include 'all' to turn all FAs on.\n";
            cerr << "Example: `-lfa:sockmgmt,chn-recv` enables only `sockmgmt` and `chn-recv` log FAs.\n";
            cerr << "Default: all are on except haicrypt. NOTE: 'general' FA can't be disabled.\n\n";
            cerr << "List of functional areas:\n";

            map<int, string> revmap;
            for (auto entry: SrtLogFAList())
                revmap[entry.second] = entry.first;

            // Each group on a new line
            int en10 = 0;
            for (auto entry: revmap)
            {
                cerr << " " << entry.second;
                if (entry.first/10 != en10)
                {
                    cerr << endl;
                    en10 = entry.first/10;
                }
            }
            cerr << endl;

            return 1;
        }

        cout << "SRT sample application to transmit live streaming.\n";
        cerr << "Built with SRT Library version: " << SRT_VERSION << endl;
        const uint32_t srtver = srt_getversion();
        const int major = srtver / 0x10000;
        const int minor = (srtver / 0x100) % 0x100;
        const int patch = srtver % 0x100;
        cerr << "SRT Library version: " << major << "." << minor << "." << patch << endl;
        cerr << "Usage: srt-live-transmit [options] <input-uri> <output-uri>\n";
        cerr << "\n";
#ifndef _WIN32
        PrintOptionHelp(o_timeout,   "<timeout=0>", "exit timer in seconds");
        PrintOptionHelp(o_timeout_mode, "<mode=0>", "timeout mode (0 - since app start; 1 - like 0, but cancel on connect");
#endif
        PrintOptionHelp(o_autorecon, "<enabled=yes>", "auto-reconnect mode {yes, no}");
        PrintOptionHelp(o_chunk,     "<chunk=1456>", "max size of data read in one step, that can fit one SRT packet");
        PrintOptionHelp(o_bwreport,  "<every_n_packets=0>", "bandwidth report frequency");
        PrintOptionHelp(o_srctime,   "<enabled=yes>", "Pass packet time from source to SRT output {yes, no}");
        PrintOptionHelp(o_buffering, "<packets=n>", "Buffer up to n incoming packets");
        PrintOptionHelp(o_statsrep,  "<every_n_packets=0>", "frequency of status report");
        PrintOptionHelp(o_statsout,  "<filename>", "output stats to file");
        PrintOptionHelp(o_statspf,   "<format=default>", "stats printing format {json, csv, default}");
        PrintOptionHelp(o_statsfull, "", "full counters in stats-report (prints total statistics)");
        PrintOptionHelp(o_loglevel,  "<level=error>", "log level {fatal,error,info,note,warning}");
        PrintOptionHelp(o_logfa,     "<fas>", "log functional area (see '-h logging' for more info)");
        //PrintOptionHelp(o_log_internal, "", "use internal logger");
        PrintOptionHelp(o_logfile, "<filename="">", "write logs to file");
        PrintOptionHelp(o_quiet, "", "quiet mode (default off)");
        PrintOptionHelp(o_verbose,   "", "verbose mode (default off)");
        cerr << "\n";
        cerr << "\t-h,-help - show this help (use '-h logging' for logging system)\n";
        cerr << "\t-version - print SRT library version\n";
        cerr << "\n";
        cerr << "\t<input-uri>  - URI specifying a medium to read from\n";
        cerr << "\t<output-uri> - URI specifying a medium to write to\n";
        cerr << "URI syntax: SCHEME://HOST:PORT/PATH?PARAM1=VALUE&PARAM2=VALUE...\n";
        cerr << "Supported schemes:\n";
        cerr << "\tsrt: use HOST, PORT, and PARAM for setting socket options\n";
        cerr << "\tudp: use HOST, PORT and PARAM for some UDP specific settings\n";
        cerr << "\tfile: only as file://con for using stdin or stdout\n";

        return 2;
    }

    if (print_version)
    {
        cerr << "SRT Library version: " <<  SRT_VERSION << endl;
        return 2;
    }

    cfg.timeout      = Option<OutNumber>(params, o_timeout);
    cfg.timeout_mode = Option<OutNumber>(params, o_timeout_mode);
    cfg.chunk_size   = Option<OutNumber>(params, "-1", o_chunk);
    cfg.srctime      = Option<OutBool>(params, cfg.srctime, o_srctime);
    const int buffering = Option<OutNumber>(params, "10", o_buffering);
    if (buffering <= 0)
    {
        cerr << "ERROR: Buffering value should be positive. Value provided: " << buffering << "." << endl;
        return 1;
    }
    else
    {
        cfg.buffering = (size_t) buffering;
    }
    cfg.bw_report    = Option<OutNumber>(params, o_bwreport);
    cfg.stats_report = Option<OutNumber>(params, o_statsrep);
    cfg.stats_out    = Option<OutString>(params, o_statsout);
    const string pf  = Option<OutString>(params, "default", o_statspf);
    cfg.stats_pf     = ParsePrintFormat(pf);
    if (cfg.stats_pf == SRTSTATS_PROFMAT_INVALID)
    {
        cfg.stats_pf = SRTSTATS_PROFMAT_2COLS;
        cerr << "ERROR: Unsupported print format: " << pf << " -- fallback to default" << endl;
        return 1;
    }

    cfg.full_stats   = OptionPresent(params, o_statsfull);
    cfg.loglevel     = SrtParseLogLevel(Option<OutString>(params, "error", o_loglevel));
    cfg.logfas       = SrtParseLogFA(Option<OutString>(params, "", o_logfa));
    cfg.log_internal = OptionPresent(params, o_log_internal);
    cfg.logfile      = Option<OutString>(params, o_logfile);
    cfg.quiet        = OptionPresent(params, o_quiet);
    
    if (OptionPresent(params, o_verbose))
        Verbose::on = !cfg.quiet;

    cfg.auto_reconnect = Option<OutBool>(params, true, o_autorecon);

    cfg.source = params[""].at(0);
    cfg.target = params[""].at(1);

    return 0;
}



int main(int argc, char** argv)
{
    srt_startup();
    // This is mainly required on Windows to initialize the network system,
    // for a case when the instance would use UDP. SRT does it on its own, independently.
    if (!SysInitializeNetwork())
        throw std::runtime_error("Can't initialize network!");

    // Symmetrically, this does a cleanup; put into a local destructor to ensure that
    // it's called regardless of how this function returns.
    struct NetworkCleanup
    {
        ~NetworkCleanup()
        {
            srt_cleanup();
            SysCleanupNetwork();
        }
    } cleanupobj;


    LiveTransmitConfig cfg;
    const int parse_ret = parse_args(cfg, argc, argv);
    if (parse_ret != 0)
        return parse_ret == 1 ? EXIT_FAILURE : 0;

    //
    // Set global config variables
    //
    if (cfg.chunk_size > 0)
        transmit_chunk_size = cfg.chunk_size;
    transmit_stats_writer = SrtStatsWriterFactory(cfg.stats_pf);
    transmit_bw_report = cfg.bw_report;
    transmit_stats_report = cfg.stats_report;
    transmit_total_stats = cfg.full_stats;

    //
    // Set SRT log levels and functional areas
    //
    srt_setloglevel(cfg.loglevel);
    if (!cfg.logfas.empty())
    {
        srt_resetlogfa(nullptr, 0);
        for (set<srt_logging::LogFA>::iterator i = cfg.logfas.begin(); i != cfg.logfas.end(); ++i)
            srt_addlogfa(*i);
    }

    //
    // SRT log handler
    //
    std::ofstream logfile_stream; // leave unused if not set
    char NAME[] = "SRTLIB";
    if (cfg.log_internal)
    {
        srt_setlogflags(0
            | SRT_LOGF_DISABLE_TIME
            | SRT_LOGF_DISABLE_SEVERITY
            | SRT_LOGF_DISABLE_THREADNAME
            | SRT_LOGF_DISABLE_EOL
        );
        srt_setloghandler(NAME, TestLogHandler);
    }
    else if (!cfg.logfile.empty())
    {
        logfile_stream.open(cfg.logfile.c_str());
        if (!logfile_stream)
        {
            cerr << "ERROR: Can't open '" << cfg.logfile.c_str() << "' for writing - fallback to cerr\n";
        }
        else
        {
            UDT::setlogstream(logfile_stream);
        }
    }


    //
    // SRT stats output
    //
    std::ofstream logfile_stats; // leave unused if not set
    if (cfg.stats_out != "")
    {
        logfile_stats.open(cfg.stats_out.c_str());
        if (!logfile_stats)
        {
            cerr << "ERROR: Can't open '" << cfg.stats_out << "' for writing stats. Fallback to stdout.\n";
            logfile_stats.close();
        }
    }
    else if (cfg.bw_report != 0 || cfg.stats_report != 0)
    {
        g_stats_are_printed_to_stdout = true;
    }

    ostream &out_stats = logfile_stats.is_open() ? logfile_stats : cout;

#ifdef _WIN32

    if (cfg.timeout != 0)
    {
        cerr << "ERROR: The -timeout option (-t) is not implemented on Windows\n";
        return EXIT_FAILURE;
    }

#else
    if (cfg.timeout > 0)
    {
        signal(SIGALRM, OnAlarm_Interrupt);
        if (!cfg.quiet)
            cerr << "TIMEOUT: will interrupt after " << cfg.timeout << "s\n";
        alarm(cfg.timeout);
    }
#endif
    signal(SIGINT, OnINT_ForceExit);
    signal(SIGTERM, OnINT_ForceExit);


    if (!cfg.quiet)
    {
        cerr << "Media path: '"
            << cfg.source
            << "' --> '"
            << cfg.target
            << "'\n";
    }

    unique_ptr<Source> src;
    bool srcConnected = false;
    unique_ptr<Target> tar;
    bool tarConnected = false;

    int pollid = srt_epoll_create();
    if (pollid < 0)
    {
        cerr << "Can't initialize epoll";
        return 1;
    }

    size_t receivedBytes = 0;
    size_t wroteBytes = 0;
    size_t lostBytes = 0;
    size_t lastReportedtLostBytes = 0;
    std::time_t writeErrorLogTimer(std::time(nullptr));

    try {
        // Now loop until broken
        while (!int_state && !timer_state)
        {
            if (!src.get())
            {
                src = Source::Create(cfg.source);
                if (!src.get())
                {
                    cerr << "Unsupported source type" << endl;
                    return 1;
                }
                int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                switch (src->uri.type())
                {
                case UriParser::SRT:
                    if (srt_epoll_add_usock(pollid,
                        src->GetSRTSocket(), &events))
                    {
                        cerr << "Failed to add SRT source to poll, "
                            << src->GetSRTSocket() << endl;
                        return 1;
                    }
                    break;
                case UriParser::UDP:
                    if (srt_epoll_add_ssock(pollid,
                        src->GetSysSocket(), &events))
                    {
                        cerr << "Failed to add UDP source to poll, "
                            << src->GetSysSocket() << endl;
                        return 1;
                    }
                    break;
                case UriParser::FILE:
                    if (srt_epoll_add_ssock(pollid,
                        src->GetSysSocket(), &events))
                    {
                        cerr << "Failed to add FILE source to poll, "
                            << src->GetSysSocket() << endl;
                        return 1;
                    }
                    break;
                default:
                    break;
                }

                receivedBytes = 0;
            }

            if (!tar.get())
            {
                tar = Target::Create(cfg.target);
                if (!tar.get())
                {
                    cerr << "Unsupported target type" << endl;
                    return 1;
                }

                // IN because we care for state transitions only
                // OUT - to check the connection state changes
                int events = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                switch(tar->uri.type())
                {
                case UriParser::SRT:
                    if (srt_epoll_add_usock(pollid,
                        tar->GetSRTSocket(), &events))
                    {
                        cerr << "Failed to add SRT destination to poll, "
                            << tar->GetSRTSocket() << endl;
                        return 1;
                    }
                    break;
                default:
                    break;
                }

                wroteBytes = 0;
                lostBytes = 0;
                lastReportedtLostBytes = 0;
            }

            int srtrfdslen = 2;
            int srtwfdslen = 2;
            SRTSOCKET srtrwfds[4] = {SRT_INVALID_SOCK, SRT_INVALID_SOCK , SRT_INVALID_SOCK , SRT_INVALID_SOCK };
            int sysrfdslen = 2;
            SYSSOCKET sysrfds[2];
            if (srt_epoll_wait(pollid,
                &srtrwfds[0], &srtrfdslen, &srtrwfds[2], &srtwfdslen,
                100,
                &sysrfds[0], &sysrfdslen, 0, 0) >= 0)
            {
                bool doabort = false;
                for (size_t i = 0; i < sizeof(srtrwfds) / sizeof(SRTSOCKET); i++)
                {
                    SRTSOCKET s = srtrwfds[i];
                    if (s == SRT_INVALID_SOCK)
                        continue;

                    // Remove duplicated sockets
                    for (size_t j = i + 1; j < sizeof(srtrwfds) / sizeof(SRTSOCKET); j++)
                    {
                        const SRTSOCKET next_s = srtrwfds[j];
                        if (next_s == s)
                            srtrwfds[j] = SRT_INVALID_SOCK;
                    }

                    bool issource = false;
                    if (src && src->GetSRTSocket() == s)
                    {
                        issource = true;
                    }
                    else if (tar && tar->GetSRTSocket() != s)
                    {
                        continue;
                    }

                    const char * dirstring = (issource) ? "source" : "target";

                    SRT_SOCKSTATUS status = srt_getsockstate(s);
                    switch (status)
                    {
                    case SRTS_LISTENING:
                    {
                        const bool res = (issource) ?
                            src->AcceptNewClient() : tar->AcceptNewClient();
                        if (!res)
                        {
                            cerr << "Failed to accept SRT connection"
                                << endl;
                            doabort = true;
                            break;
                        }

                        srt_epoll_remove_usock(pollid, s);

                        SRTSOCKET ns = (issource) ?
                            src->GetSRTSocket() : tar->GetSRTSocket();
                        int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                        if (srt_epoll_add_usock(pollid, ns, &events))
                        {
                            cerr << "Failed to add SRT client to poll, "
                                << ns << endl;
                            doabort = true;
                        }
                        else
                        {
                            if (!cfg.quiet)
                            {
                                cerr << "Accepted SRT "
                                    << dirstring
                                    <<  " connection"
                                    << endl;
                            }
#ifndef _WIN32
                            if (cfg.timeout_mode == 1 && cfg.timeout > 0)
                            {
                                if (!cfg.quiet)
                                    cerr << "TIMEOUT: cancel\n";
                                alarm(0);
                            }
#endif
                            if (issource)
                                srcConnected = true;
                            else
                                tarConnected = true;
                        }
                    }
                    break;
                    case SRTS_BROKEN:
                    case SRTS_NONEXIST:
                    case SRTS_CLOSED:
                    {
                        if (issource)
                        {
                            if (srcConnected)
                            {
                                if (!cfg.quiet)
                                {
                                    cerr << "SRT source disconnected"
                                        << endl;
                                }
                                srcConnected = false;
                            }
                        }
                        else if (tarConnected)
                        {
                            if (!cfg.quiet)
                                cerr << "SRT target disconnected" << endl;
                            tarConnected = false;
                        }

                        if(!cfg.auto_reconnect)
                        {
                            doabort = true;
                        }
                        else
                        {
                            // force re-connection
                            srt_epoll_remove_usock(pollid, s);
                            if (issource)
                                src.reset();
                            else
                                tar.reset();

#ifndef _WIN32
                            if (cfg.timeout_mode == 1 && cfg.timeout > 0)
                            {
                                if (!cfg.quiet)
                                    cerr << "TIMEOUT: will interrupt after " << cfg.timeout << "s\n";
                                alarm(cfg.timeout);
                            }
#endif
                        }
                    }
                    break;
                    case SRTS_CONNECTED:
                    {
                        if (issource)
                        {
                            if (!srcConnected)
                            {
                                if (!cfg.quiet)
                                    cerr << "SRT source connected" << endl;
                                srcConnected = true;
                            }
                        }
                        else if (!tarConnected)
                        {
                            if (!cfg.quiet)
                                cerr << "SRT target connected" << endl;
                            tarConnected = true;
                            if (tar->uri.type() == UriParser::SRT)
                            {
                                const int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                                // Disable OUT event polling when connected
                                if (srt_epoll_update_usock(pollid,
                                    tar->GetSRTSocket(), &events))
                                {
                                    cerr << "Failed to add SRT destination to poll, "
                                        << tar->GetSRTSocket() << endl;
                                    return 1;
                                }
                            }

#ifndef _WIN32
                            if (cfg.timeout_mode == 1 && cfg.timeout > 0)
                            {
                                if (!cfg.quiet)
                                    cerr << "TIMEOUT: cancel\n";
                                alarm(0);
                            }
#endif
                        }
                    }

                    default:
                    {
                        // No-Op
                    }
                    break;
                    }
                }

                if (doabort)
                {
                    break;
                }

                // read a few chunks at a time in attempt to deplete
                // read buffers as much as possible on each read event
                // note that this implies live streams and does not
                // work for cached/file sources
                std::list<std::shared_ptr<MediaPacket>> dataqueue;
                if (src.get() && src->IsOpen() && (srtrfdslen || sysrfdslen))
                {
                    while (dataqueue.size() < cfg.buffering)
                    {
                        std::shared_ptr<MediaPacket> pkt(new MediaPacket(transmit_chunk_size));
                        const int res = src->Read(transmit_chunk_size, *pkt, out_stats);

                        if (res == SRT_ERROR && src->uri.type() == UriParser::SRT)
                        {
                            if (srt_getlasterror(NULL) == SRT_EASYNCRCV)
                                break;

                            throw std::runtime_error(
                                string("error: recvmsg: ") + string(srt_getlasterror_str())
                            );
                        }

                        if (res == 0 || pkt->payload.empty())
                        {
                            break;
                        }

                        dataqueue.push_back(pkt);
                        receivedBytes += pkt->payload.size();
                    }
                }

                // if there is no target, let the received data be lost
                while (!dataqueue.empty())
                {
                    std::shared_ptr<MediaPacket> pkt = dataqueue.front();
                    if (!tar.get() || !tar->IsOpen())
                    {
                        lostBytes += pkt->payload.size();
                    }
                    else if (!tar->Write(pkt->payload.data(), pkt->payload.size(), cfg.srctime ? pkt->time : 0, out_stats))
                    {
                        lostBytes += pkt->payload.size();
                    }
                    else
                    {
                        wroteBytes += pkt->payload.size();
                    }

                    dataqueue.pop_front();
                }

                if (!cfg.quiet && (lastReportedtLostBytes != lostBytes))
                {
                    std::time_t now(std::time(nullptr));
                    if (std::difftime(now, writeErrorLogTimer) >= 5.0)
                    {
                        cerr << lostBytes << " bytes lost, "
                            << wroteBytes << " bytes sent, "
                            << receivedBytes << " bytes received"
                            << endl;
                        writeErrorLogTimer = now;
                        lastReportedtLostBytes = lostBytes;
                    }
                }
            }
        }
    }
    catch (std::exception& x)
    {
        cerr << "ERROR: " << x.what() << endl;
        return 255;
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
