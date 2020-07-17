/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#include <memory>
#include <thread>
#include <list>
#include <utility>
#include <chrono>
#include <csignal>
#include <iterator>
#include <stdexcept>

#define REQUIRE_CXX11 1

#include "apputil.hpp"  // CreateAddrInet
#include "uriparser.hpp"  // UriParser
#include "socketoptions.hpp"
#include "logsupport.hpp"
#include "testmediabase.hpp"
#include "testmedia.hpp"
#include "netinet_any.h"
#include "threadname.h"
#include "verbose.hpp"

#include <srt.h>
#include <logging.h>

// Make the windows-nonexistent alarm an empty call
#ifdef _WIN32
#define alarm(argument) (void)0
#define signal_alarm(fn) (void)0
#else
#define signal_alarm(fn) signal(SIGALRM, fn)
#endif


volatile bool mpbond_int_state = false;
void OnINT_SetIntState(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    mpbond_int_state = true;
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

    signal(SIGINT, OnINT_SetIntState);
    signal(SIGTERM, OnINT_SetIntState);

    vector<OptionScheme> optargs;

    OptionName
        o_input     ((optargs), "<input-medium> Define input to send over SRT endpoint", "i", "input"),
        o_output    ((optargs), "<output-medium> Define output to send data read from SRT endpoint", "o", "output"),
        o_verbose   ((optargs), "[channel=0|1] Print size of every packet transferred on stdout or specified [channel]", "v",   "verbose"),
        o_loglevel  ((optargs), "<severity=fatal|error|note|warning|debug> Minimum severity for logs", "ll",  "loglevel"),
        o_logfa     ((optargs), "<FA=all> Enabled Functional Areas", "lfa", "logfa"),
        o_help      ((optargs), " This help", "?", "help", "-help")
            ;

    options_t params = ProcessOptions(argv, argc, optargs);

    bool need_help = OptionPresent(params, o_help);

    vector<string> args = params[""];

    string srtspec;

    if (args.empty())
        need_help = true;
    else
    {
        for (size_t i = 0; i < args.size(); ++i)
        {
            UriParser u(args[i], UriParser::EXPECT_HOST);
            if (u.portno() == 0)
            {
                cerr << "ERROR: " << args[i] << " expected host:port or :port syntax.\n";
                return 1;
            }
        }
    }

    if (need_help)
    {
        cerr << "Usage:\n";
        cerr << "    " << argv[0] << " <SRT listeners...> [-i INPUT] [-o OUTPUT]\n";
        cerr << "*** (Position of [options] is unrestricted.)\n";
        cerr << "*** (<variadic...> option parameters can be only terminated by a next option.)\n";
        cerr << "where:\n";
        cerr << "   - <SRT listeners...>: a list of host:port specs for SRT listener\n";
        cerr << "   - INPUT or OUTPUT: at least one of that kind must be specified\n";
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

    bool skip_flushing = false; // non-configurable for now

    bool mode_output = OptionPresent(params, o_output);

    string loglevel = Option<OutString>(params, "error", "ll", "loglevel");
    srt_logging::LogLevel::type lev = SrtParseLogLevel(loglevel);
    srt::setloglevel(lev);
    srt::addlogfa(SRT_LOGFA_APP);

    // Check verbose option before extracting the argument so that Verb()s
    // can be displayed also when they report something about option parsing.
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


    if (OptionPresent(params, o_input) == OptionPresent(params, o_output))
    {
        cerr << "One of -i and -o options must be specified (not both)\n";
        return 1;
    }


    // Create listeners according to the parameters
    vector<SRTSOCKET> listeners;

    Verb() << "LISTENERS [ " << VerbNoEOL;

    for (size_t i = 0; i < args.size(); ++i)
    {
        UriParser u(args[i], UriParser::EXPECT_HOST);
        sockaddr_any sa = CreateAddr(u.host(), u.portno());

        SRTSOCKET s = srt_create_socket();

        //SRT_GROUPCONNTYPE gcon = SRTGC_GROUPONLY;
        int gcon = 1;
        srt_setsockflag(s, SRTO_GROUPCONNECT, &gcon, sizeof gcon);

        srt_bind(s, sa.get(), sizeof sa);
        srt_listen(s, 5);

        listeners.push_back(s);
        Verb() << u.host() << ":" << u.portno() << " " << VerbNoEOL;
    }

    Verb() << "] accept...";

    SRTSOCKET conngrp = srt_accept_bond(listeners.data(), listeners.size(), -1);
    if (conngrp == SRT_INVALID_SOCK)
    {
        cerr << "ERROR: srt_accept_bond: " << srt_getlasterror_str() << endl;
        return 1;
    }

    auto s = new SrtSource;
    unique_ptr<Source> src;
    unique_ptr<Target> tar;

    try
    {
        // Now create input or output
        if (mode_output)
        {
            string outspec = Option<OutString>(params, o_output);
            Verb() << "SRT -> " << outspec;
            tar = Target::Create(outspec);

            s->Acquire(conngrp);
            src.reset(s);
        }
        else
        {
            string inspec = Option<OutString>(params, o_input);
            Verb() << "SRT <- " << inspec;
            src = Source::Create(inspec);

            auto s = new SrtTarget;
            s->Acquire(conngrp);
            tar.reset(s);
        }
    }
    catch (...)
    {
        return 2;
    }

    size_t chunk = SRT_LIVE_MAX_PLSIZE;

    // Now run the loop
    try
    {
        for (;;)
        {
            Verb() << " << ... " << VerbNoEOL;
            const bytevector& data = src->Read(chunk);
            Verb() << " << " << data.size() << "  ->  " << VerbNoEOL;
            if ( data.empty() && src->End() )
            {
                Verb() << "EOS";
                break;
            }
            tar->Write(data);

            if ( tar->Broken() )
            {
                Verb() << " OUTPUT broken";
                break;
            }

            Verb() << "sent";

            if ( mpbond_int_state )
            {
                Verror() << "\n (interrupted on request)";
                break;
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
        if (::mpbond_int_state)
        {
            Verror() << "Exit on interrupt.";
            // Do nothing.
        }
        else
        {
            Verror() << "STD EXCEPTION: " << x.what();
        }

        return 255;
    } catch (...) {
        Verror() << "UNKNOWN type of EXCEPTION";
        return 1;
    }

    return 0;
}




