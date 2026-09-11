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

#include <csignal>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <iterator>

#include "hvu_compat.h"
#include "apputil.hpp"
#include "uriparser.hpp"  // UriParser
#include "socketoptions.hpp"
#include "testmedia.hpp" // requires access to SRT-dependent globals
#include "verbose.hpp"
#include "bstow-read.hpp"

#include "ofmt.h"

// NOTE: This is without "srt/" because it uses an internal path
// to the library. Application using the "installed" library should
// use <srt/srt.h>
#include <srt.h>
#include <access_control.h>
#include <logging.h>
#include <logger_fas.h>

using namespace std;
using namespace srt;

hvu::logging::Logger applog("app", srt::logging::logger_config(), true, "srt-stow");

void OnINT_ForceExit(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    transmit_int_state = true;
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

    signal(SIGINT, OnINT_ForceExit);
    signal(SIGTERM, OnINT_ForceExit);

    OptionHandler optargs;

    OptionName
        o_timeout   ((optargs), "<timeout[s]=0> Data transmission timeout", "t",   "to", "timeout" ),
        o_loglevel  ((optargs), "<severity> Minimum severity for logs (see --help logging)", "ll",  "loglevel"),
        o_logfa     ((optargs), "<FA=FA-list...> Enabled Functional Areas (see --help logging)", "lfa", "logfa"),
        o_logfile   ((optargs), "<filepath> File to send logs to", "lf",  "logfile"),
        o_verbose   ((optargs), OptionScheme::ARG_OPT(1), "Print size of every packet transferred on stdout", "v",   "verbose"),
        o_help      ((optargs), "[special=logging] This help", "?",   "help", "-help")
            ;

    OptionStatus ost = optargs.process(argv, argc);

    bool need_help = optargs.exists(o_help);

    vector<string> args = optargs[""];

    if (!ost)
    {
        ofprintl(cerr, "ERROR: -", ost.error_option, ": ", ost.error_code_str());
        need_help = true;
    }

    if (args.size() != 2)
    {
        need_help = true;
    }

    if (need_help)
    {
        ofprintl(cerr, "Usage: ", argv[0], " <BSTOW data URI> <SRT target>");
        for (auto os: optargs.options())
            cout << os.helpitem() << endl;
        return 1;
    }

    // This is set always in the STOW application.
    transmit_use_sourcetime = 1;
    Verbose::on = optargs.exists(o_verbose);
    if (Verbose::on)
    {
        int verbchan = optargs.get(o_verbose);
        if (verbchan)
        {
            if (verbchan == 1)
                Verbose::cverb = &std::cout;
            else if (verbchan == 2)
                Verbose::cverb = &std::cerr;
            else
            {
                ofprintl(cerr, "OPTION: -v requires optionally 1 (stdout) or 2 (stderr)");
                return -1;
            }
        }
    }

    string loglevel = optargs.get(o_loglevel, "error");
    vector<string> logfa = optargs.get(o_logfa);

    srt_setloglevel(hvu::logging::parse_level(loglevel));
    string logfa_on, logfa_off;
    ParseLogFASpec(logfa, (logfa_on), (logfa_off));

    set<int> fasoff = hvu::logging::parse_fa(srt::logging::logger_config(), logfa_off);
    set<string> missing_on;
    set<int> fason = hvu::logging::parse_fa(srt::logging::logger_config(), logfa_on, &missing_on);

    auto fa_del = [fasoff]() {
        for (set<int>::iterator i = fasoff.begin(); i != fasoff.end(); ++i)
            srt_dellogfa(*i);
    };

    auto fa_add = [fason]() {
        for (set<int>::iterator i = fason.begin(); i != fason.end(); ++i)
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

    if (!missing_on.empty())
    {
        cerr << "WARNING: unknown logging FA: ";
        copy(missing_on.begin(), missing_on.end(), ostream_iterator<string>(cerr, " "));
        cerr << endl;
    }

    std::ofstream logfile_stream; // leave unused if not set
    string logfile = optargs.get(o_logfile);
    if (logfile != "")
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

    // In this application you can only have a source file
    // and target SRT.
    // File will be accessed specific way, so don't use FileSource,
    // just directly ifstream.

    ifstream src;
    unique_ptr<SrtTarget> tar;

    int app_result = 0;
    try
    {
        // Ok, now file

        string srcname = args[0];
        if (srcname.find("file://") == 0)
        {
            srcname = srcname.substr(7);
            if (srcname == "con")
            {
                ofprintl(cerr, "Console input (stdin) not supported. Use file path");
                throw TransmissionError("Can't open file");
            }
        }

        bstow::PacketReader r(args[0]);
        UriParser u = args[1];

        if (u.type() != UriParser::SRT)
        {
            ofprintl(cerr, "Target URI: '", args[1], "' not supported - SRT only");
            throw TransmissionError("Wrong target medium");
        }

        tar.reset( new SrtTarget(u.host(), u.portno(), u.path(), u.parameters()) );

        // Default transmitmedia settings
        ::transmit_use_sourcetime = true;

        // XXX  TESTING
        ofprintl(cout, "SOURCE FILE: ", srcname);
        ofprintl(cout, "OUTPUT: ", u.uri());

        if (!r.Prepare(srt_time_now()))
        {
            ofprintl(cerr, "TS file indexing failed");
            throw TransmissionError("Wrong source medium");
        }

        // XXX LOOP, END.
        for (;;)
        {
            Verb("[.", VerbNoEOL);

            // This call will block by sleeping up to the time
            // designated as the timestamp of the next frame.
            Verb(", ... ", VerbNoEOL);
            const MediaPacket data = r.Read();
            Verb(", ", data.payload.size(),
                    " T=", data.time,
                    "  ->  ", VerbNoEOL);
            if (data.payload.empty() && r.End())
            {
                Verb("EOS");
                throw Source::ReadEOF("EOS");
            }

            tar->Write(data);
            Verb(".] ", VerbNoEOL);
            if (tar->Broken())
                break;
            if (::transmit_int_state)
            {
                Verror("\n (interrupted on request)");
                break;
            }
            Verb("sent");
        }
    }
    catch (TransmissionError& e)
    {
        ofprintl(cerr, "ERROR: ", e.what());
        app_result = 1;
    }
    catch (ios::failure&)
    {
        ofprintl(cerr, "Source file '", args[0], "' not found");
        app_result = 1;
    }
    // Do cleanup manually to avoid destructor-based calls prematurely.
    srt_cleanup();

    // Unregister the file if it was used as logging target to prevent
    // accessing a deleted file in logs called in the destructor.
    srt::setlogstream(cerr);

    return app_result;
}


