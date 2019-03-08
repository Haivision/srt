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

#include "apputil.hpp"  // CreateAddrInet
#include "uriparser.hpp"  // UriParser
#include "socketoptions.hpp"
#include "logsupport.hpp"
#include "transmitbase.hpp"
#include "verbose.hpp"

// NOTE: This is without "haisrt/" because it uses an internal path
// to the library. Application using the "installed" library should
// use <srt/srt.h>
#include <srt.h>
#include <udt.h> // This TEMPORARILY contains extra C++-only SRT API.
#include <logging.h>

using namespace std;

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
#ifndef _WIN32
        cerr << "\t-t:<timeout=0> - exit timer in seconds\n";
        cerr << "\t-tm:<mode=0> - timeout mode (0 - since app start; 1 - like 0, but cancel on connect)\n";
#endif
        cerr << "\t-c:<chunk=1316> - max size of data read in one step\n";
        cerr << "\t-b:<bandwidth> - set SRT bandwidth\n";
        cerr << "\t-r:<report-frequency=0> - bandwidth report frequency\n";
        cerr << "\t-s:<stats-report-freq=0> - frequency of status report\n";
        cerr << "\t-pf:<format> - printformat (json or default)\n";
        cerr << "\t-statsreport:<filename> - stats report file name (cout for output to cout, or a filename)\n";
        cerr << "\t-f - full counters in stats-report (prints total statistics)\n";
        cerr << "\t-q - quiet mode (default no)\n";
        cerr << "\t-v - verbose mode (default no)\n";
        cerr << "\t-a - auto-reconnect mode, default yes, -a:no to disable\n";
        return 1;
    }

    int timeout = stoi(Option("0", "t", "to", "timeout"), 0, 0);
    int timeout_mode = stoi(Option("0", "tm", "timeout-mode"), 0, 0);
    unsigned long chunk = stoul(Option("0", "c", "chunk"), 0, 0);
    if ( chunk == 0 )
    {
        chunk = SRT_LIVE_DEF_PLSIZE;
    }
    else
    {
        transmit_chunk_size = chunk;
    }

    bool quiet = Option("no", "q", "quiet") != "no";
    Verbose::on = !quiet && Option("no", "v", "verbose") != "no";
    string loglevel = Option("error", "loglevel");
    string logfa = Option("general", "logfa");
    string logfile = Option("", "logfile");
    bool internal_log = Option("no", "loginternal") != "no";
    bool autoreconnect = Option("yes", "a", "auto") != "no";
    transmit_total_stats = Option("no", "f", "fullstats") != "no";

    // Print format
    const string pf = Option("default", "pf", "printformat");
    if (pf == "json")
    {
        printformat = PRINT_FORMAT_JSON;
    }
    if (pf == "csv")
    {
        printformat = PRINT_FORMAT_CSV;
    }
    else if (pf != "default")
    {
        cerr << "ERROR: Unsupported print format: " << pf << endl;
        return 1;
    }

    try
    {
        transmit_bw_report = stoul(Option("0", "r", "report", "bandwidth-report", "bitrate-report"));
        transmit_stats_report = stoul(Option("0", "s", "stats", "stats-report-frequency"));
    }
    catch (std::invalid_argument &)
    {
        cerr << "ERROR: Incorrect integer number specified for an option.\n";
        return 1;
    }

    std::ofstream logfile_stream; // leave unused if not set

    srt_setloglevel(SrtParseLogLevel(loglevel));
    set<srt_logging::LogFA> fas = SrtParseLogFA(logfa);
    for (set<srt_logging::LogFA>::iterator i = fas.begin(); i != fas.end(); ++i)
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

    std::ofstream logfile_stats; // leave unused if not set
    const string statsfile = Option("cout", "statsfile");
    if (statsfile != "" && statsfile != "cout")
    {
        logfile_stats.open(statsfile.c_str());
        if (!logfile_stats)
        {
            cerr << "ERROR: Can't open '" << statsfile << "' for writing stats. Fallback to cout.\n";
            logfile_stats.close();
        }
    }

    ostream &out_stats = logfile_stats.is_open() ? logfile_stats : cout;

#ifdef _WIN32

    if (timeout > 0)
    {
        cerr << "ERROR: The -timeout option (-t) is not implemented on Windows\n";
        return 1;
    }

#else
    if (timeout > 0)
    {
        signal(SIGALRM, OnAlarm_Interrupt);
        if (!quiet)
            cerr << "TIMEOUT: will interrupt after " << timeout << "s\n";
        alarm(timeout);
    }
#endif
    signal(SIGINT, OnINT_ForceExit);
    signal(SIGTERM, OnINT_ForceExit);


    if (!quiet)
    {
        cerr << "Media path: '"
            << params[0]
            << "' --> '"
            << params[1]
            << "'\n";
    }

    unique_ptr<Source> src;
    bool srcConnected = false;
    unique_ptr<Target> tar;
    bool tarConnected = false;

    int pollid = srt_epoll_create();
    if ( pollid < 0 )
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
                src = Source::Create(params[0]);
                if (!src.get())
                {
                    cerr << "Unsupported source type" << endl;
                    return 1;
                }
                int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                switch(src->uri.type())
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
                tar = Target::Create(params[1]);
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
                if ((false))
                {
                    cerr << "Event:"
                        << " srtrfdslen " << srtrfdslen
                        << " sysrfdslen " << sysrfdslen
                        << endl;
                }

                bool doabort = false;
                for (size_t i = 0; i < sizeof(srtrwfds) / sizeof(SRTSOCKET); i++)
                {
                    SRTSOCKET s = srtrwfds[i];
                    if (s == SRT_INVALID_SOCK)
                        continue;

                    bool issource = false;
                    if (src && src->GetSRTSocket() == s)
                    {
                        issource = true;
                    }
                    else if (tar && tar->GetSRTSocket() != s)
                    {
                        continue;
                    }

                    const char * dirstring = (issource)? "source" : "target";

                    SRT_SOCKSTATUS status = srt_getsockstate(s);
                    if ((false) && status != SRTS_CONNECTED)
                    {
                        cerr << dirstring << " status " << status << endl;
                    }
                    switch (status)
                    {
                        case SRTS_LISTENING:
                        {
                            if ((false) && !quiet)
                                cerr << "New SRT client connection" << endl;

                            bool res = (issource) ?
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
                                if (!quiet)
                                {
                                    cerr << "Accepted SRT "
                                        << dirstring
                                        <<  " connection"
                                        << endl;
                                }
#ifndef _WIN32
                                if (timeout_mode == 1 && timeout > 0)
                                {
                                    if (!quiet)
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
                                    if (!quiet)
                                    {
                                        cerr << "SRT source disconnected"
                                            << endl;
                                    }
                                    srcConnected = false;
                                }
                            }
                            else if (tarConnected)
                            {
                                if (!quiet)
                                    cerr << "SRT target disconnected" << endl;
                                tarConnected = false;
                            }

                            if(!autoreconnect)
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
                            }
                        }
                        break;
                        case SRTS_CONNECTED:
                        {
                            if (issource)
                            {
                                if (!srcConnected)
                                {
                                    if (!quiet)
                                        cerr << "SRT source connected" << endl;
                                    srcConnected = true;
                                }
                            }
                            else if (!tarConnected)
                            {
                                if (!quiet)
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
                std::list<std::shared_ptr<bytevector>> dataqueue;
                if (src.get() && (srtrfdslen || sysrfdslen))
                {
                    while (dataqueue.size() < 10)
                    {
                        std::shared_ptr<bytevector> pdata(
                            new bytevector(chunk));
                        if (!src->Read(chunk, *pdata, out_stats) || (*pdata).empty())
                        {
                            break;
                        }
                        dataqueue.push_back(pdata);
                        receivedBytes += (*pdata).size();
                    }
                }

                // if no target, let received data fall to the floor
                while (!dataqueue.empty())
                {
                    std::shared_ptr<bytevector> pdata = dataqueue.front();
                    if (!tar.get() || !tar->IsOpen()) {
                        lostBytes += (*pdata).size();
                    } else if (!tar->Write(pdata->data(), pdata->size(), out_stats)) {
                        lostBytes += (*pdata).size();
                    } else
                        wroteBytes += (*pdata).size();

                    dataqueue.pop_front();
                }

                if (!quiet && (lastReportedtLostBytes != lostBytes))
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

