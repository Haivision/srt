/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/


#include <iostream>
#include <iterator>
#include <vector>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <sys/stat.h>
#include <srt.h>
#include <udt.h>

#include "apputil.hpp"
#include "uriparser.hpp"
#include "logsupport.hpp"
#include "socketoptions.hpp"
#include "verbose.hpp"
#include "testmedia.hpp"
#include "threadname.h"

bool Upload(UriParser& srt, UriParser& file);
bool Download(UriParser& srt, UriParser& file);

using namespace std;

template <class VarType, class ValType>
struct OnReturnSetter
{
    VarType& var;
    ValType value;

    OnReturnSetter(VarType& target, ValType v): var(target), value(v) {}
    ~OnReturnSetter() { var = value; }
};

template <class VarType, class ValType>
OnReturnSetter<VarType, ValType> OnReturnSet(VarType& target, ValType v)
{ return OnReturnSetter<VarType, ValType>(target, v); }

template<class MediumDir>
struct Medium
{
    MediumDir* med = nullptr;
    unique_ptr<MediumDir> pinned_med;
    list<bytevector> buffer;
    mutex buffer_lock;
    thread thr;
    condition_variable ready;
    volatile bool running = false;
    std::exception_ptr xp; // To catch exception thrown by a thread

    virtual void Runner() = 0;

    void RunnerBase()
    {
        try
        {
            Runner();
        }
        catch (...)
        {
            xp = std::current_exception();
        }

        //Verb() << "Medium: " << this << ": thread exit";
        unique_lock<mutex> g(buffer_lock);
        running = false;
        ready.notify_all();
        //Verb() << VerbLock << "Medium: EXIT NOTIFIED";
    }

    void run()
    {
        running = true;
        std::ostringstream tns;
        tns << typeid(*this).name() << ":" << this;
        ThreadName tn(tns.str().c_str());
        thr = thread( [this] { RunnerBase(); } );
    }

    void quit()
    {
        if (thr.joinable())
            thr.join();

        if (xp)
        {
            try {
                std::rethrow_exception(xp);
            } catch (TransmissionError& e) {
                if (Verbose::on)
                    Verb() << "Medium " << this << " exited with Transmission Error:\n\t" << e.what();
                else
                    cerr << "Transmission Error: " << e.what() << endl;
            } catch (...) {
                if (Verbose::on)
                    Verb() << "Medium " << this << " exited with UNKNOWN EXCEPTION:";
                else
                    cerr << "UNKNOWN EXCEPTION on medium\n";
            }
        }
    }

    void Setup(MediumDir* t)
    {
        med = t;
        // Leave pinned_med as 0
    }

    void Setup(unique_ptr<MediumDir>&& medbase)
    {
        pinned_med = move(medbase);
        med = pinned_med.get();
    }

    virtual ~Medium()
    {
        //Verb() << "Medium: " << this << " DESTROYED. Threads quit.";
        quit();
    }
};

struct SourceMedium: Medium<Source>
{
    size_t chunksize = 1316;

    // Source Runner: read payloads and put on the buffer
    void Runner() override
    {
        auto on_return_set = OnReturnSet(running, false);

        Verb() << "Starting SourceMedium: " << this;
        for (;;)
        {
            bytevector input = med->Read(chunksize);
            if (med->End())
            {
                Verb() << "Exitting SourceMedium: " << this;
                return;
            }

            lock_guard<mutex> g(buffer_lock);
            buffer.push_back(input);
            ready.notify_one();
        }
    }

    // External user: call this to get the buffer.
    bytevector Extract()
    {
        unique_lock<mutex> g(buffer_lock);
        for (;;)
        {
            if (!running)
            {
                //Verb() << "SourceMedium " << this << " not running";
                return {};
            }

            if (!buffer.empty())
            {
                bytevector top;
                swap(top, *buffer.begin());
                buffer.pop_front();
                return top;
            }

            // Block until ready
            //Verb() << VerbLock << "Extract: " << this << " wait-->";
            ready.wait_for(g, chrono::seconds(1), [this] { return running && !buffer.empty(); });
            //Verb() << VerbLock << "Extract: " << this << " <-- notified.";
        }
    }
};

struct TargetMedium: Medium<Target>
{
    void Runner() override
    {
        auto on_return_set = OnReturnSet(running, false);
        Verb() << "Starting TargetMedium: " << this;
        for (;;)
        {
            bytevector val;
            {
                unique_lock<mutex> lg(buffer_lock);
                if (!running)
                    return;

                if (buffer.empty())
                {
                    bool gotsomething = ready.wait_for(lg, chrono::seconds(1), [this] { return running && !buffer.empty(); } );
                    if (!running || !med || med->Broken())
                        return;
                    if (!gotsomething) // exit on timeout
                        continue;
                }
                swap(val, *buffer.begin());

                //Verb() << "TargetMedium: EXTRACTING for sending";
                buffer.pop_front();
            }

            // Check before writing
            if (med->Broken())
            {
                running = false;
                return;
            }

            // You get the data to send, send them.
            med->Write(val);
        }
    }


    bool Schedule(const bytevector& data)
    {
        lock_guard<mutex> lg(buffer_lock);
        if (!running)
            return false;

        buffer.push_back(data);
        ready.notify_one();
        return true;
    }

    void Interrupt()
    {
        lock_guard<mutex> lg(buffer_lock);
        running = false;
        ready.notify_one();
    }

    ~TargetMedium()
    {
        //Verb() << "TargetMedium: DESTROYING";
        Interrupt();
        // ~Medium will do quit() additionally, which joins the thread
    }

};

class SrtMainLoop
{
    UriParser m_srtspec;

    // Media used
    unique_ptr<SrtRelay> m_srt_relay;
    SourceMedium m_srt_source;
    SourceMedium m_input_medium;
    list<unique_ptr<TargetMedium>> m_output_media;
    thread m_input_thr;
    std::exception_ptr m_input_xp;
    void InputRunner();
    volatile bool m_input_running = false;

public:
    SrtMainLoop(const string& srt_uri, bool input_echoback, const string& input_spec, const vector<string>& output_spec);

    void run();
    
    ~SrtMainLoop()
    {
        if (m_input_thr.joinable())
            m_input_thr.join();
    }
};




int main( int argc, char** argv )
{
    OptionName
        o_loglevel = { "ll", "loglevel" },
        o_verbose = {"v", "verbose" },
        o_input = {"i", "input"},
        o_output = {"o", "output"},
        o_echo = {"e", "io", "input-echoback"};

    // Options that expect no arguments (ARG_NONE) need not be mentioned.
    vector<OptionScheme> optargs = {
        { o_loglevel, OptionScheme::ARG_ONE },
        { o_input, OptionScheme::ARG_ONE },
        { o_output, OptionScheme::ARG_VAR }
    };
    options_t params = ProcessOptions(argv, argc, optargs);

    /*
    cerr << "OPTIONS (DEBUG)\n";
    for (auto o: params)
    {
        cerr << "[" << o.first << "] ";
        copy(o.second.begin(), o.second.end(), ostream_iterator<string>(cerr, " "));
        cerr << endl;
    }
    */

    vector<string> args = params[""];
    if ( args.size() != 1 )
    {
        cerr << "Usage: " << argv[0] << " <srt-endpoint> [ -i <input> | -e ] [ -o <output> ]\n";
        return 1;
    }

    string loglevel = Option<OutString>(params, "error", o_loglevel);
    logging::LogLevel::type lev = SrtParseLogLevel(loglevel);
    UDT::setloglevel(lev);
    UDT::addlogfa(SRT_LOGFA_APP);

   string verbo = Option<OutString>(params, "no", o_verbose);
   if ( verbo == "" || !false_names.count(verbo) )
   {
       Verbose::on = true;
       int verboch = atoi(verbo.c_str());
       if (verboch <= 0)
       {
           verboch = 1;
       }
       else if (verboch > 2)
       {
           cerr << "ERROR: -v option accepts value 1 (stdout, default) or 2 (stderr)\n";
           return 1;
       }

       if (verboch == 1)
       {
           Verbose::cverb = &std::cout;
       }
       else
       {
           Verbose::cverb = &std::cerr;
       }
   }

    string srt_endpoint = args[0];

    UriParser usrt(srt_endpoint);

    if (usrt.scheme() != "srt")
    {
        cerr << "ERROR: the only one freestanding parameter should be an SRT uri.\n";
        cerr << "Usage: " << argv[0] << " <srt-endpoint> [ -i <input> ] [ -o <output> ] [ -e ]\n";
        return 1;
    }

    // Allowed are only one input and multiple outputs.
    // Input-echoback is treated as a single input.
    bool input_echoback = Option<OutString>(params, "no", o_echo) != "no";
    string input_spec = Option<OutString>(params, "", o_input);

    if (input_spec != "" && input_echoback)
    {
        cerr << "ERROR: input-echoback is treated as input specifcation, -i can't be specified together.\n";
        return 1;
    }

    vector<string> output_spec = Option<OutList>(params, vector<string>{}, o_output);

    if (!input_echoback)
    {
        if (input_spec == "" || output_spec.empty())
        {
            cerr << "ERROR: at least one input and one output must be specified (-io specifies both)\n";
            return 1;
        }
    }

    Verb() << "SETTINGS:";
    Verb() << "SRT connection: " << srt_endpoint;
    if (input_echoback)
    {
        Verb() << "INPUT: (from SRT connection)";
    }
    else
    {
        Verb() << "INPUT: " << input_spec;
    }

    Verb() << "OUTPUT LIST:";
    if (input_echoback)
    {
        Verb() << "\t(back to SRT connection)";
    }
    for (auto& s: output_spec)
        Verb() << "\t" << s;

    try
    {
        SrtMainLoop loop(srt_endpoint, input_echoback, input_spec, output_spec);
        loop.run();
    }
    catch (std::exception& x)
    {
        cerr << "ERROR: " << x.what() << endl;
        return 1;
    }


    return 0;
}

SrtMainLoop::SrtMainLoop(const string& srt_uri, bool input_echoback, const string& input_spec, const vector<string>& output_spec)
{
    // Now prepare all media 
    // They use pointers instead of real variables
    // so that the creation time can be delayed
    // up to this moment, and the parameters prepared
    // before passing to the constructors.

    // Start with SRT.

    UriParser srtspec(srt_uri);
    SrtModel m(srtspec.host(), srtspec.portno(), srtspec.parameters());

    // Just to keep it unchanged.
    string id = m_srtspec["streamid"];

    Verb() << "Establishing SRT connection: " << srt_uri;

    m.Establish(Ref(id));

    Verb() << "... Established. configuring other pipes:";

    // Once it's ready, use it to initialize the medium.

    m_srt_relay.reset(new SrtRelay);
    m_srt_relay->StealFrom(m);

    m_srt_source.Setup(m_srt_relay.get());

    // Now check the input medium
    if (input_echoback)
    {
        Verb() << "SRT set up as input source and the first output target";

        // Add SRT medium to output targets, and keep input medium empty.
        unique_ptr<TargetMedium> m { new TargetMedium };
        m->Setup(m_srt_relay.get());
        m_output_media.push_back(move(m));
    }
    else
    {
        // Initialize input medium and do not add SRT medium
        // to the output list, as this will be fed directly
        // by the data from this input medium in a spearate engine.
        Verb() << "Setting up input: " << input_spec;
        m_input_medium.Setup(Source::Create(input_spec));

        // Also set writing to SRT non-blocking always.
        bool no = false;
        srt_setsockflag(m_srt_relay->Socket(), SRTO_SNDSYN, &no, sizeof no);
    }

    // And the output media

    for (string spec: output_spec)
    {
        Verb() << "Setting up output: " << spec;
        unique_ptr<TargetMedium> m { new TargetMedium };
        m->Setup(Target::Create(spec));
        m_output_media.push_back(move(m));
    }

    // We're done here.
    Verb() << "MEDIA SUCCESSFULLY CREATED.";
}

void SrtMainLoop::InputRunner()
{
    // An extra thread with a loop that reads from the external input
    // and writes into the SRT medium. When echoback mode is used,
    // this thread isn't started at all and instead the SRT reading
    // serves as both SRT reading and input reading.

    auto on_return_set = OnReturnSet(m_input_running, false);

    Verb() << "RUNNING INPUT LOOP";
    for (;;)
    {
        bytevector data = m_input_medium.Extract();

        if (data.empty())
        {
            Verb() << "INPUT READING INTERRUPTED.";
            break;
        }

        Verb() << "INPUT [" << data.size() << "]  " << VerbNoEOL;
        m_srt_relay->Write(data);
    }
}

void SrtMainLoop::run()
{
    // Start the media runners.

    Verb() << "Starting media-bound threads.";

    for (auto& o: m_output_media)
        o->run();

    if (m_input_medium.med)
    {
        m_input_medium.run();
        m_input_running = true;

        std::ostringstream tns;
        tns << "Input:" << this;
        ThreadName tn(tns.str().c_str());
        m_input_thr = thread([this] {
                try {
                    InputRunner();
                } catch (...) {
                    m_input_xp = std::current_exception();
                }

                Verb() << "INPUT: thread exit";
        });
    }

    m_srt_source.run();

    Verb() << "RUNNING SRT MEDIA LOOP";
    for (;;)
    {
        bytevector data = m_srt_source.Extract();

        if (data.empty())
        {
            Verb() << "SRT READING INTERRUPTED.";
            break;
        }

        vector<string> output_report;
        bool any = false;
        int no = 1;

        for (auto i = m_output_media.begin(), i_next = i; i != m_output_media.end(); i = i_next)
        {
            ++i_next;
            auto& o = *i;
            if (!o->Schedule(data))
            {
                if (Verbose::on)
                {
                    ostringstream os;
                    os << " --XXX-> <" << no << ">";
                    output_report.push_back(os.str());
                }
                m_output_media.erase(i);
                continue;
            }

            if (Verbose::on)
            {
                ostringstream os;
                os << " --> <" << no << ">";
                output_report.push_back(os.str());
            }
            any = true;
            ++no;
        }

        if (Verbose::on)
        {
            string outputs;
            for (auto& r: output_report)
                outputs += " " + r;
            if (!any)
                outputs = " --> * (no output)";

            Verb() << VerbLock << "SRT [" << data.size() << "]  " << outputs;
        }
    }

    Verb() << "MEDIA LOOP EXIT";

    if (m_input_xp)
    {
        try {
            std::rethrow_exception(m_input_xp);
        } catch (std::exception& x) {
            cerr << "INPUT EXIT BY EXCEPTION: " << x.what() << endl;
        } catch (...) {
            cerr << "INPUT EXIT BY UNKNOWN EXCEPTION\n";
        }
    }
}
