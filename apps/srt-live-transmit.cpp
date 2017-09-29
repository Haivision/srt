/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>
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

#define REQUIRE_CXX11 1

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
#include "../common/logsupport.hpp"
#include "../common/transmitbase.hpp"

// NOTE: This is without "haisrt/" because it uses an internal path
// to the library. Application using the "installed" library should
// use <haisrt/srt.h>
#include <srt.h>
#include <logging.h>

// The length of the SRT payload used in srt_recvmsg call.
// So far, this function must be used and up to this length of payload.
const size_t DEFAULT_CHUNK = 1316;

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

volatile bool int_state = false;
void OnINT_SetIntState(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    int_state = true;
    if ( transmit_throw_on_interrupt )
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
    transmit_bw_report = stoul(Option("0", "r", "report", "bandwidth-report", "bitrate-report"), 0, 0);
    transmit_verbose = Option("no", "v", "verbose") != "no";
    bool crashonx = Option("no", "k", "crash") != "no";

    string loglevel = Option("error", "loglevel");
    string logfa = Option("general", "logfa");
    string logfile = Option("", "logfile");
    transmit_stats_report = stoi(Option("0", "s", "stats", "stats-report-frequency"), 0, 0);

    bool internal_log = Option("no", "loginternal") != "no";

    bool skip_flushing = Option("no", "S", "skipflush") != "no";

    std::ofstream logfile_stream; // leave unused if not set

    srt_setloglevel(SrtParseLogLevel(loglevel));
    set<logging::LogFA> fas = SrtParseLogFA(logfa);
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

    auto src = Source::Create(params[0]);
    auto tar = Target::Create(params[1]);

    // Now loop until broken
    BandwidthGuard bw(bandwidth);

    if ( transmit_verbose )
    {
        cout << "STARTING TRANSMISSION: '" << params[0] << "' --> '" << params[1] << "'\n";
    }

    extern logging::Logger glog;
    try
    {
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

            bw.Checkpoint(chunk, transmit_bw_report);
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

                cerr << "(DEBUG)... still " << still << " bytes\n";
                this_thread::sleep_for(chrono::seconds(1));
            }
        }
    } catch (std::exception& x) {

        cerr << "STD EXCEPTION: " << x.what() << endl;
        cerr << "Waiting 5s for possible cleanup...\n";
        this_thread::sleep_for(chrono::seconds(5));

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

