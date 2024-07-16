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

#include "srt_compat.h"
#include "apputil.hpp"
#include "uriparser.hpp"  // UriParser
#include "socketoptions.hpp"
#include "logsupport.hpp"
#include "testmedia.hpp" // requires access to SRT-dependent globals
#include "verbose.hpp"

// NOTE: This is without "srt/" because it uses an internal path
// to the library. Application using the "installed" library should
// use <srt/srt.h>
#include <srt.h>
#include <access_control.h>
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

srt::sync::atomic<bool> timer_state;
void OnINT_ForceExit(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    transmit_int_state = true;
}

std::string g_interrupt_reason;

void OnAlarm_Interrupt(int)
{
    cerr << "\n---------- INTERRUPT ON TIMEOUT: hang on " << g_interrupt_reason << "!\n";
    transmit_int_state = true; // JIC
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

bool CheckMediaSpec(const string& prefix, const vector<string>& spec, string& w_outspec)
{
    // This function prints error messages by itself then returns false.
    // Otherwise nothing is printed and true is returned.
    // r_outspec is for a case when a redundancy specification should be translated.

    if (spec.empty())
    {
        cerr << prefix << ": Specification is empty\n";
        return false;
    }

    if (spec.size() == 1)
    {
        // Then, whatever.
        w_outspec = spec[0];
        return true;
    }

    // We have multiple items specified, check each one
    // it SRT, if so, craft the redundancy URI spec,
    // otherwise reject
    vector<string> adrs;
    map<string,string> uriparam;
    bool first = true;
    bool allow_raw_spec = false;
    for (auto uris: spec)
    {
        UriParser uri(uris, UriParser::EXPECT_HOST);
        if (!allow_raw_spec && uri.type() != UriParser::SRT)
        {
            cerr << ": Multiple media must be all with SRT scheme, or srt://* as first.\n";
            return false;
        }

        if (uri.host() == "*")
        {
            allow_raw_spec = true;
            first = false;
            uriparam = uri.parameters();

            // This does not specify the address, only options and URI.
            continue;
        }

        string aspec = uri.host() + ":" + uri.port();
        if (aspec[0] == ':' || aspec[aspec.size()-1] == ':')
        {
            cerr << "Empty host or port in the address specification: " << uris << endl;
            return false;
        }

        if (allow_raw_spec && !uri.parameters().empty())
        {
            bool cont = false;
            // Extract attributes if any and pass them there.
            for (UriParser::query_it i = uri.parameters().begin();
                    i != uri.parameters().end(); ++i)
            {
                aspec += cont ? "&" : "?";
                cont = false;
                aspec += i->first + "=" + i->second;
            }
        }

        adrs.push_back(aspec);
        if (first)
        {
            uriparam = uri.parameters();
            first = false;
        }
    }

    w_outspec = "srt:////group?";
    if (map_getp(uriparam, "type") == nullptr)
        uriparam["type"] = "redundancy";

    for (auto& name_value: uriparam)
    {
        string name, value; tie(name, value) = name_value;
        w_outspec += name + "=" + value + "&";
    }
    w_outspec += "nodes=";
    for (string& a: adrs)
        w_outspec += a + ",";

    Verb() << "NOTE: " << prefix << " specification set as: " << (w_outspec);

    return true;
}

extern "C" void TestLogHandler(void* opaque, int level, const char* file, int line, const char* area, const char* message);

namespace srt_logging
{
    extern Logger glog;
}

#if ENABLE_BONDING
extern "C" int SrtCheckGroupHook(void* , SRTSOCKET acpsock, int , const sockaddr*, const char* )
{
    static string gtypes[] = {
        "undefined", // SRT_GTYPE_UNDEFINED
        "broadcast",
        "backup",
        "balancing",
        "multicast"
    };

    int type;
    int size = sizeof type;
    srt_getsockflag(acpsock, SRTO_GROUPCONNECT, &type, &size);
    Verb() << "listener: @" << acpsock << " - accepting " << (type ? "GROUP" : "SINGLE") << VerbNoEOL;
    if (type != 0)
    {
        SRT_GROUP_TYPE gt;
        size = sizeof gt;
        if (-1 != srt_getsockflag(acpsock, SRTO_GROUPTYPE, &gt, &size))
        {
            if (gt < Size(gtypes))
                Verb() << " type=" << gtypes[gt] << VerbNoEOL;
            else
                Verb() << " type=" << int(gt) << VerbNoEOL;
        }
    }
    Verb() << " connection";

    return 0;
}
#endif

extern "C" int SrtUserPasswordHook(void* , SRTSOCKET acpsock, int hsv, const sockaddr*, const char* streamid)
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

    srt_setrejectreason(acpsock, SRT_REJX_UNAUTHORIZED);
    string exp_pw = passwd.at(username);

    srt_setsockflag(acpsock, SRTO_PASSPHRASE, exp_pw.c_str(), int(exp_pw.size()));

    return 0;
}

struct RejectData
{
    int code;
    string streaminfo;
} g_reject_data;

extern "C" int SrtRejectByCodeHook(void* op, SRTSOCKET acpsock, int , const sockaddr*, const char* )
{
    RejectData* data = (RejectData*)op;

    srt_setrejectreason(acpsock, data->code);
    srt::setstreamid(acpsock, data->streaminfo);

    return -1;
}

int main( int argc, char** argv )
{
    // This is mainly required on Windows to initialize the network system,
    // for a case when the instance would use UDP. SRT does it on its own, independently.
    if ( !SysInitializeNetwork() )
        throw std::runtime_error("Can't initialize network!");

    srt_startup();

    // Symmetrically, this does a cleanup; put into a local destructor to ensure that
    // it's called regardless of how this function returns.
    struct NetworkCleanup
    {
        ~NetworkCleanup()
        {
            SysCleanupNetwork();
            srt_cleanup();
        }
    } cleanupobj;

    vector<OptionScheme> optargs;

    OptionName
        o_timeout   ((optargs), "<timeout[s]=0> Data transmission timeout", "t",   "to", "timeout" ),
        o_chunk     ((optargs), "<chunk=1316> Single reading operation buffer size", "c",   "chunk"),
        o_bandwidth ((optargs), "<bw[ms]=0[unlimited]> Input reading speed limit", "b",   "bandwidth", "bitrate"),
        o_report    ((optargs), "<frequency[1/pkt]=0> Print bandwidth report periodically", "r",   "bandwidth-report", "bitrate-report"),
        o_verbose   ((optargs), "[channel=0|1|./file] Print size of every packet transferred on stdout or specified [channel]", "v",   "verbose"),
        o_crash     ((optargs), " Core-dump when connection got broken by whatever reason (developer mode)", "k",   "crash"),
        o_loglevel  ((optargs), "<severity> Minimum severity for logs (see --help logging)", "ll",  "loglevel"),
        o_logfa     ((optargs), "<FA=FA-list...> Enabled Functional Areas (see --help logging)", "lfa", "logfa"),
        o_logfile   ((optargs), "<filepath> File to send logs to", "lf",  "logfile"),
        o_stats     ((optargs), "<freq[npkt]> How often stats should be reported", "s",   "stats", "stats-report-frequency"),
        o_statspf   ((optargs), "<format=default|csv|json> Format for printing statistics", "pf", "statspf", "statspformat"),
        o_logint    ((optargs), " Use internal function for receiving logs (for testing)",        "loginternal"),
        o_skipflush ((optargs), " Do not wait safely 5 seconds at the end to flush buffers", "sf",  "skipflush"),
        o_stoptime  ((optargs), "<time[s]=0[no timeout]> Time after which the application gets interrupted", "d", "stoptime"),
        o_hook      ((optargs), "<hookspec> Use listener callback of given specification (internally coded)", "hook"),
#if ENABLE_BONDING
        o_group     ((optargs), "<URIs...> Using multiple SRT connections as redundancy group", "g"),
#endif
        o_stime     ((optargs), " Pass source time explicitly to SRT output", "st", "srctime", "sourcetime"),
        o_retry     ((optargs), "<N=-1,0,+N> Retry connection N times if failed on timeout", "rc", "retry"),
        o_help      ((optargs), "[special=logging] This help", "?",   "help", "-help")
            ;

    options_t params = ProcessOptions(argv, argc, optargs);

    bool need_help = OptionPresent(params, o_help);

    vector<string> args = params[""];

    string source_spec, target_spec;
#if ENABLE_BONDING
    vector<string> groupspec = Option<OutList>(params, vector<string>{}, o_group);
#endif
    vector<string> source_items, target_items;

    if (!need_help)
    {
        // You may still need help.

#if ENABLE_BONDING
        if ( !groupspec.empty() )
        {
            // Check if you have something before -g and after -g.
            if (args.empty())
            {
                // Then all items are sources, but the last one is a single target.
                if (groupspec.size() < 3)
                {
                    cerr << "ERROR: Redundancy group: with nothing preceding -g, use -g <SRC-URI1> <SRC-URI2>... <TAR-URI> (at least 3 args)\n";
                    need_help = true;
                }
                else
                {
                    copy(groupspec.begin(), groupspec.end()-1, back_inserter(source_items));
                    target_items.push_back(*(groupspec.end()-1));
                }
            }
            else
            {
                // Something before g, something after g. This time -g can accept also one argument.
                copy(args.begin(), args.end(), back_inserter(source_items));
                copy(groupspec.begin(), groupspec.end(), back_inserter(target_items));
            }
        }
        else
#endif
        {
            if (args.size() < 2)
            {
                cerr << "ERROR: source and target URI must be specified.\n\n";
                need_help = true;
            }
            else
            {
                source_items.push_back(args[0]);
                target_items.push_back(args[1]);
            }
        }
    }

    // Check verbose option before extracting the argument so that Verb()s
    // can be displayed also when they report something about option parsing.
    string verbose_val = Option<OutString>(params, "no", o_verbose);

    unique_ptr<ofstream> pout_verb;

    int verbch = 1; // default cerr
    if (verbose_val != "no")
    {
        Verbose::on = true;
        if (verbose_val == "")
            verbch = 1;
        else if (verbose_val.substr(0, 2) == "./")
            verbch = 3;
        else try
        {
            verbch = stoi(verbose_val);
        }
        catch (...)
        {
            verbch = 1;
        }

        if (verbch == 1)
        {
            Verbose::cverb = &std::cout;
        }
        else if (verbch == 2)
        {
            Verbose::cverb = &std::cerr;
        }
        else if (verbch == 3)
        {
            pout_verb.reset(new ofstream(verbose_val.substr(2), ios::out | ios::trunc));
            if (!pout_verb->good())
            {
                cerr << "-v: error opening verbose output file: " << verbose_val << endl;
                return 1;
            }
            Verbose::cverb = pout_verb.get();
        }
        else
        {
            cerr << "-v or -v:1 (default) or -v:2 only allowed\n";
            return 1;
        }
    }


    if (!need_help)
    {
        // Redundancy is then simply recognized by the fact that there are
        // multiple specified inputs or outputs, for SRT caller only. Check
        // every URI in advance.
        if (!CheckMediaSpec("INPUT", source_items, (source_spec)))
            need_help = true;
        if (!CheckMediaSpec("OUTPUT", target_items, (target_spec)))
            need_help = true;
    }

    if (need_help)
    {
        string helpspec = Option<OutString>(params, o_help);
        if (helpspec == "logging")
        {
            cerr << "Logging options:\n";
            cerr << "    -ll <LEVEL>   - specify minimum log level\n";
            cerr << "    -lfa <area...> - specify functional areas\n";
            cerr << "Where:\n\n";
            cerr << "    <LEVEL>: fatal error note warning debug\n\n";
            cerr << "This turns on logs that are at the given log name and all on the left.\n";
            cerr << "(Names from syslog, like alert, crit, emerg, err, info, panic, are also\n";
            cerr << "recognized, but they are aligned to those that lie close in hierarchy.)\n\n";
            cerr << "    <area...> is a space-sep list of areas to turn on or ~areas to turn off.\n\n";
            cerr << "The list may include 'all' to turn all on or off, beside those selected.\n";
            cerr << "Example: `-lfa ~all cc` - turns off all FA, except cc\n";
            cerr << "Default: all are on except haicrypt. NOTE: 'general' can't be off.\n\n";
            cerr << "List of functional areas:\n";

            map<int, string> revmap;
            for (auto entry: SrtLogFAList())
                revmap[entry.second] = entry.first;

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

        // Unrecognized helpspec is same as no helpspec, that is, general help.
        cerr << "Usage:\n";
        cerr << "    (1) " << argv[0] << " [options] <input> <output>\n";
        cerr << "    (2) " << argv[0] << " <inputs...> -g <outputs...> [options]\n";
        cerr << "*** (Position of [options] is unrestricted.)\n";
        cerr << "*** (<variadic...> option parameters can be only terminated by a next option.)\n";
        cerr << "where:\n";
        cerr << "    (1) Exactly one input and one output URI spec is required,\n";
        cerr << "    (2) Multiple SRT inputs or output as redundant links are allowed.\n";
        cerr << "        `URI1 URI2 -g URI3` uses 1, 2 input and 3 output\n";
        cerr << "        `-g URI1 URI2 URI3` like above\n";
        cerr << "        `URI1 -g URI2 URI3` uses 1 input and 2, 3 output\n";
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

    transmit_use_sourcetime = OptionPresent(params, o_stime);
    size_t bandwidth = Option<OutNumber>(params, "0", o_bandwidth);
    transmit_bw_report = Option<OutNumber>(params, "0", o_report);
    bool crashonx = OptionPresent(params, o_crash);

    string loglevel = Option<OutString>(params, "error", o_loglevel);
    vector<string> logfa = Option<OutList>(params, o_logfa);
    string logfile = Option<OutString>(params, "", o_logfile);
    transmit_stats_report = Option<OutNumber>(params, "0", o_stats);

    bool internal_log = OptionPresent(params, o_logint);
    bool skip_flushing = OptionPresent(params, o_skipflush);

    string hook = Option<OutString>(params, "", o_hook);
    if (hook != "")
    {
        vector<string> hargs;
        Split(hook, ':', back_inserter(hargs));

        if (hargs[0] == "user-password")
        {
            transmit_accept_hook_fn = &SrtUserPasswordHook;
            transmit_accept_hook_op = nullptr;
        }
        else if (hargs[0] == "reject")
        {
            hargs.resize(3); // make sure 3 elements exist, may be empty
            g_reject_data.code = stoi(hargs[1]);
            g_reject_data.streaminfo = hargs[2];
            transmit_accept_hook_op = (void*)&g_reject_data;
            transmit_accept_hook_fn = &SrtRejectByCodeHook;
        }
#if ENABLE_BONDING
        else if (hargs[0] == "groupcheck")
        {
            transmit_accept_hook_fn = &SrtCheckGroupHook;
            transmit_accept_hook_op = nullptr;
        }
#endif
    }

    string pfextra;
    SrtStatsPrintFormat statspf = ParsePrintFormat(Option<OutString>(params, "default", o_statspf), (pfextra));
    if (statspf == SRTSTATS_PROFMAT_INVALID)
    {
        cerr << "Invalid stats print format\n";
        return 1;
    }
    transmit_stats_writer = SrtStatsWriterFactory(statspf);
    if (pfextra != "")
    {
        vector<string> options;
        Split(pfextra, ',', back_inserter(options));
        for (auto& i: options)
        {
            vector<string> klv;
            Split(i, '=', back_inserter(klv));
            klv.resize(2);
            transmit_stats_writer->Option(klv[0], klv[1]);
        }
    }

    // Options that require integer conversion
    size_t stoptime = Option<OutNumber>(params, "0", o_stoptime);
    std::ofstream logfile_stream; // leave unused if not set

    srt_setloglevel(SrtParseLogLevel(loglevel));
    string logfa_on, logfa_off;
    ParseLogFASpec(logfa, (logfa_on), (logfa_off));

    set<srt_logging::LogFA> fasoff = SrtParseLogFA(logfa_off);
    set<srt_logging::LogFA> fason = SrtParseLogFA(logfa_on);

    auto fa_del = [fasoff]() {
        for (set<srt_logging::LogFA>::iterator i = fasoff.begin(); i != fasoff.end(); ++i)
            srt_dellogfa(*i);
    };

    auto fa_add = [fason]() {
        for (set<srt_logging::LogFA>::iterator i = fason.begin(); i != fason.end(); ++i)
            srt_addlogfa(*i);
    };

    if (logfa_off == "all")
    {
        // If the spec is:
        //     -lfa ~all control app
        // then we first delete all, then enable given ones
        fa_del();
        fa_add();
    }
    else
    {
        // Otherwise we first add all those that have to be added,
        // then delete those unwanted. This embraces both
        //   -lfa control app ~cc
        // and
        //   -lfa all ~cc
        fa_add();
        fa_del();
    }


    srt::addlogfa(SRT_LOGFA_APP);

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
            srt::setlogstream(logfile_stream);
        }
    }

    string retryphrase = Option<OutString>(params, "", o_retry);
    if (retryphrase != "")
    {
        if (retryphrase[retryphrase.size()-1] == 'a')
        {
            transmit_retry_always = true;
            retryphrase = retryphrase.substr(0, retryphrase.size()-1);
        }

        transmit_retry_connect = stoi(retryphrase);
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
        if (::transmit_int_state)
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

        Verb() << "MEDIA CREATION FAILED: " << x.what() << " - exiting.";

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

    if (!src || !tar)
    {
        const string tarstate = tar ? "CREATED" : "FAILED";
        const string srcstate = src ? "CREATED" : "FAILED";

        cerr << "ERROR: not both media created; source:" << srcstate << " target:" << tarstate << endl;
        return 2;
    }

    // Now loop until broken
    BandwidthGuard bw(bandwidth);

    if (transmit_use_sourcetime && src->uri.type() != UriParser::SRT)
    {
        Verb() << "WARNING: -st option is effective only if the target type is SRT";
    }

    Verb() << "STARTING TRANSMISSION: '" << source_spec << "' --> '" << target_spec << "'";

    // After the time has been spent in the creation
    // (including waiting for connection)
    // rest of the time should be spent for transmission.
    if (stoptime != 0)
    {
        int elapsed = end_time - start_time;
        int remain = int(stoptime) - elapsed;

        if (remain <= final_delay)
        {
            cerr << "NOTE: remained too little time for cleanup: " << remain << "s - exiting\n";
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
                Verb() << "[." << VerbNoEOL;
                alarm(timeout);
            }
            else
            {
                alarm(0);
            }
            Verb() << " << ... " << VerbNoEOL;
            g_interrupt_reason = "reading";
            const MediaPacket& data = src->Read(chunk);
            Verb() << " << " << data.payload.size() << "  ->  " << VerbNoEOL;
            if ( data.payload.empty() && src->End() )
            {
                Verb() << "EOS";
                break;
            }
            g_interrupt_reason = "writing";
            tar->Write(data);
            if (stoptime == 0 && timeout != -1 )
            {
                Verb() << ".] " << VerbNoEOL;
                alarm(0);
            }

            if ( tar->Broken() )
            {
                Verb() << " OUTPUT broken";
                break;
            }

            Verb() << "sent";

            if (::transmit_int_state)
            {
                Verror() << "\n (interrupted on request)";
                break;
            }

            bw.Checkpoint(chunk, transmit_bw_report);

            if (stoptime != 0)
            {
                int elapsed = time(0) - end_time;
                int remain = int(stoptime - final_delay - elapsed);
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
        else if (::transmit_int_state)
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
    if ( opaque ) {
#ifdef _MSC_VER
        strncpy_s(prefix, sizeof(prefix), (char*)opaque, _TRUNCATE);
#else
        strncpy(prefix, (char*)opaque, sizeof(prefix) - 1);
        prefix[sizeof(prefix) - 1] = '\0';
#endif
    }
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
