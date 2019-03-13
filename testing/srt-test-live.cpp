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

#include "apputil.hpp"  // CreateAddrInet
#include "uriparser.hpp"  // UriParser
#include "socketoptions.hpp"
#include "logsupport.hpp"
#include "testmediabase.hpp"
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

ostream* cverb = &cout;

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
    {
        chunk = SRT_LIVE_DEF_PLSIZE;
    }
    else
    {
        transmit_chunk_size = chunk;
    }

    string verbose_val = Option("no", "v", "verbose");
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

    bool crashonx = Option("no", "k", "crash") != "no";
    string loglevel = Option("error", "loglevel");
    string logfa = Option("general", "logfa");
    string logfile = Option("", "logfile");
    bool internal_log = Option("no", "loginternal") != "no";
    bool skip_flushing = Option("no", "S", "skipflush") != "no";

    // Print format
    string pf = Option("default", "pf", "printformat");
    if (pf == "json")
    {
        transmit_printformat_json = true;
    }
    else if (pf != "default")
    {
        cerr << "ERROR: Unsupported print format: " << pf << endl;
        return 1;
    }


    // Options that require integer conversion
    size_t bandwidth;
    size_t stoptime;

    try
    {
        bandwidth = stoul(Option("0", "b", "bandwidth", "bitrate"));
        transmit_bw_report = stoul(Option("0", "r", "report", "bandwidth-report", "bitrate-report"));
        transmit_stats_report = stoi(Option("0", "s", "stats", "stats-report-frequency"));
        stoptime = stoul(Option("0", "d", "stoptime"));
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
        src = Source::Create(params[0]);
        tar = Target::Create(params[1]);
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

    Verb() << "STARTING TRANSMISSION: '" << params[0] << "' --> '" << params[1] << "'";

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
                Verb() << " OUTPUT broken\n";
                break;
            }

            Verb() << " sent";
            if ( int_state )
            {
                cerr << "\n (interrupted on request)\n";
                break;
            }

            bw.Checkpoint(chunk, transmit_bw_report);

            if (stoptime != 0)
            {
                int elapsed = time(0) - end_time;
                int remain = stoptime - final_delay - elapsed;
                if (remain < 0)
                {
                    cerr << "\n (interrupted on timeout: elapsed " << elapsed << "s) - waiting " << final_delay << "s for cleanup\n";
                    this_thread::sleep_for(chrono::seconds(final_delay));
                    break;
                }
            }
        }

    } catch (Source::ReadEOF&) {
        alarm(0);

        if (!skip_flushing)
        {
            cerr << "(DEBUG) EOF when reading file. Looping until the sending bufer depletes.\n";
            for (;;)
            {
                size_t still = tar->Still();
                if (still == 0)
                {
                    cerr << "(DEBUG) DEPLETED. Done.\n";
                    break;
                }

                cerr << "(DEBUG)... still " << still << " bytes (sleep 1s)\n";
                this_thread::sleep_for(chrono::seconds(1));
            }
        }
    } catch (std::exception& x) { // Catches TransmissionError and AlarmExit

        if (stoptime != 0 && ::timer_state)
        {
            cerr << "Exit on timeout.\n";
        }
        else if (::int_state)
        {
            // Do nothing.
        }
        else
        {
            cerr << "STD EXCEPTION: " << x.what() << endl;
        }

        if (final_delay > 0)
        {
            cerr << "Waiting " << final_delay << "s for possible cleanup...\n";
            this_thread::sleep_for(chrono::seconds(final_delay));
        }
        if (stoptime != 0 && ::timer_state)
            return 0;

        return 255;

    } catch (...) {

        cerr << "UNKNOWN type of EXCEPTION\n";
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

