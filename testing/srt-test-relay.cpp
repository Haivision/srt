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

#include "platform_sys.h"

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
#include <csignal>
#include <sys/stat.h>
#include <srt.h>
#include <udt.h>

#include "apputil.hpp"
#include "uriparser.hpp"
#include "logsupport.hpp"
#include "logging.h"
#include "socketoptions.hpp"
#include "verbose.hpp"
#include "testmedia.hpp"
#include "threadname.h"

bool Upload(UriParser& srt, UriParser& file);
bool Download(UriParser& srt, UriParser& file);

srt_logging::Logger applog(SRT_LOGFA_APP, srt_logger_config, "srt-relay");

volatile bool g_program_interrupted = false;
volatile bool g_program_established = false;

SrtModel* g_pending_model = nullptr;

thread::id g_root_thread = std::this_thread::get_id();

static void OnINT_SetInterrupted(int)
{
    Verb() << VerbLock << "SIGINT: Setting interrupt state.";
    ::g_program_interrupted = true;

    // Just for a case, forcefully close all active SRT sockets.
    SrtModel* pm = ::g_pending_model;
    if (pm)
    {
        // The program is hanged on accepting a new SRT connection.
        // We need to check which thread we've fallen into.
        if (this_thread::get_id() == g_root_thread)
        {
            // Throw an exception, it will be caught in a predicted place.
            throw std::runtime_error("Interrupted on request");
        }
        else
        {
            // This is some other thread, so close the listener socket.
            // This will cause the accept block to be interrupted.
            for (SRTSOCKET i: { pm->Socket(), pm->Listener() })
                if (i != SRT_INVALID_SOCK)
                    srt_close(i);
        }
    }

}

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
    class SrtMainLoop* master = nullptr;
    MediumDir* med = nullptr;
    unique_ptr<MediumDir> pinned_med;
    list<MediaPacket> buffer;
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
        if (!med)
            return;

        applog.Debug() << "Medium(" << typeid(*med).name() << ") quit. Buffer contains " << buffer.size() << " blocks";

        string name;
        if (Verbose::on)
            name = typeid(*med).name();

        med->Close();
        if (thr.joinable())
        {
            applog.Debug() << "Medium::quit: Joining medium thread (" << name << ") ...";
            thr.join();
            applog.Debug() << "... done";
        }

        if (xp)
        {
            try {
                std::rethrow_exception(xp);
            } catch (TransmissionError& e) {
                if (Verbose::on)
                    Verb() << VerbLock << "Medium " << this << " exited with Transmission Error:\n\t" << e.what();
                else
                    cerr << "Transmission Error: " << e.what() << endl;
            } catch (...) {
                if (Verbose::on)
                    Verb() << VerbLock << "Medium " << this << " exited with UNKNOWN EXCEPTION:";
                else
                    cerr << "UNKNOWN EXCEPTION on medium\n";
            }
        }

        // Prevent further quits from running
        med = nullptr;
    }

    void Setup(SrtMainLoop* mst, MediumDir* t)
    {
        med = t;
        master = mst;
        // Leave pinned_med as 0
    }

    void Setup(SrtMainLoop* mst, unique_ptr<MediumDir>&& medbase)
    {
        pinned_med = move(medbase);
        med = pinned_med.get();
        master = mst;
    }

    virtual ~Medium()
    {
        //Verb() << "Medium: " << this << " DESTROYED. Threads quit.";
        quit();
    }
};

size_t g_chunksize = 0;
size_t g_default_live_chunksize = 1316;
size_t g_default_file_chunksize = 1456;

struct SourceMedium: Medium<Source>
{
    // Source Runner: read payloads and put on the buffer
    void Runner() override;

    // External user: call this to get the buffer.
    MediaPacket Extract();
};

struct TargetMedium: Medium<Target>
{
    void Runner() override;

    bool Schedule(const MediaPacket& data)
    {
        lock_guard<mutex> lg(buffer_lock);
        if (!running || ::g_program_interrupted)
            return false;

        applog.Debug() << "TargetMedium(" << typeid(*med).name() << "): [" << data.payload.size() << "] CLIENT -> BUFFER";
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

    void MakeStop() { m_input_running = false; }
    bool IsRunning() { return m_input_running; }

    ~SrtMainLoop()
    {
        if (m_input_thr.joinable())
            m_input_thr.join();
    }
};


void SourceMedium::Runner()
{
    ThreadName::set("SourceRN");
    if (!master)
    {
        cerr << "IPE: incorrect setup, master empty\n";
        return;
    }

    /* Don't stop me now...
    struct OnReturn
    {
        SrtMainLoop* m;
        OnReturn(SrtMainLoop* mst): m(mst) {}
        ~OnReturn()
        {
            m->MakeStop();
        }
    } on_return(master);
    */

    Verb() << VerbLock << "Starting SourceMedium: " << this;
    for (;;)
    {
        auto input = med->Read(g_chunksize);
        if (input.payload.empty() && med->End())
        {
            Verb() << VerbLock << "Exiting SourceMedium: " << this;
            return;
        }
        applog.Debug() << "SourceMedium(" << typeid(*med).name() << "): [" << input.payload.size() << "] MEDIUM -> BUFFER. signal(" << &ready << ")";

        lock_guard<mutex> g(buffer_lock);
        buffer.push_back(input);
        ready.notify_one();
    }
}

MediaPacket SourceMedium::Extract()
{
    if (!master)
        return {};

    unique_lock<mutex> g(buffer_lock);
    for (;;)
    {
        if (!buffer.empty())
        {
            MediaPacket top;
            swap(top, *buffer.begin());
            buffer.pop_front();
            applog.Debug() << "SourceMedium(" << typeid(*med).name() << "): [" << top.payload.size() << "] BUFFER -> CLIENT";
            return top;
        }
        else
        {
            // Don't worry about the media status as long as you have somthing in the buffer.
            // Purge the buffer first, then worry about the other things.
            if (!running || ::g_program_interrupted)
            {
                applog.Debug() << "Extract(" << typeid(*med).name() << "): INTERRUPTED READING ("
                                                                                << (!running ? "local" : (!master->IsRunning() ? "master" : "unknown")) << ")";
                //Verb() << "SourceMedium " << this << " not running";
                return {};
            }

        }

        // Block until ready
        applog.Debug() << "Extract(" << typeid(*med).name() << "): " << this << " wait(" << &ready << ") -->";
        ready.wait_for(g, chrono::seconds(1), [this] { return running && master->IsRunning() && !buffer.empty(); });
        applog.Debug() << "Extract(" << typeid(*med).name() << "): " << this << " <-- notified (running:"
            << boolalpha << running << " master:" << master->IsRunning() << " buffer:" << buffer.size() << ")";
    }
}

void TargetMedium::Runner()
{
    ThreadName::set("TargetRN");
    auto on_return_set = OnReturnSet(running, false);
    Verb() << VerbLock << "Starting TargetMedium: " << this;
    for (;;)
    {
        MediaPacket val;
        {
            unique_lock<mutex> lg(buffer_lock);
            if (buffer.empty())
            {
                if (!running)
                {
                    applog.Debug() << "TargetMedium(" << typeid(*med).name() << "): buffer empty, medium stopped, exiting.";
                    return;
                }

                bool gotsomething = ready.wait_for(lg, chrono::seconds(1), [this] { return !running || !buffer.empty(); } );
                applog.Debug() << "TargetMedium(" << typeid(*med).name() << "): [" << val.payload.size() << "] BUFFER update (timeout:"
                                << boolalpha << gotsomething << " running: " << running << ")";
                if (::g_program_interrupted || !running || !med || med->Broken())
                {
                    applog.Debug() << "TargetMedium(" << typeid(*med).name() << "): buffer empty, medium "
                                   << (!::g_program_interrupted ?
                                           (running ?
                                                (med ?
                                                    (med->Broken() ? "broken" : "UNKNOWN")
                                                : "deleted")
                                           : "stopped")
                                      : "killed");
                    return;
                }
                if (!gotsomething) // exit on timeout
                    continue;
            }
            swap(val, *buffer.begin());
            applog.Debug() << "TargetMedium(" << typeid(*med).name() << "): [" << val.payload.size() << "] BUFFER extraction";

            buffer.pop_front();
        }

        // Check before writing
        if (med->Broken())
        {
            applog.Debug() << "TargetMedium(" << typeid(*med).name() << "): [" << val.payload.size() << "] BUFFER -> DISCARDED (medium broken)";
            running = false;
            return;
        }

        applog.Debug() << "TargetMedium(" << typeid(*med).name() << "): [" << val.payload.size() << "] BUFFER -> MEDIUM";
        // You get the data to send, send them.
        med->Write(val);
    }
}


int main( int argc, char** argv )
{
    OptionName
        o_loglevel = { "ll", "loglevel" },
        o_logfa = { "lf", "logfa" },
        o_verbose = {"v", "verbose" },
        o_input = {"i", "input"},
        o_output = {"o", "output"},
        o_echo = {"e", "io", "input-echoback"},
        o_chunksize = {"c", "chunk"}
    ;

    // Options that expect no arguments (ARG_NONE) need not be mentioned.
    vector<OptionScheme> optargs = {
        { o_loglevel, OptionScheme::ARG_ONE },
        { o_logfa, OptionScheme::ARG_ONE },
        { o_input, OptionScheme::ARG_ONE },
        { o_output, OptionScheme::ARG_VAR },
        { o_chunksize, OptionScheme::ARG_ONE }
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
        cerr << "Options:\n";
        cerr << "\t-v  .  .  .  .  .  .  .  .  .  .  Verbose mode\n";
        cerr << "\t-ll <level=error>  .  .  .  .  .  Log level for SRT\n";
		cerr << "\t-lf <logfa=all>    .  .  .  .  .  Log Functional Areas enabled\n";
        cerr << "\t-c  <size=1316[live]|1456[file]>  Single reading buffer size\n";
		cerr << "\t-i  <URI> .  .  .  .  .  .  .  .  Input medium spec\n";
		cerr << "\t-o  <URI> .  .  .  .  .  .  .  .  Output medium spec\n";
		cerr << "\t-e  .  .  .  (conflicts with -i)  Feed SRT output back to SRT input\n";
		cerr << "\nNote: specify `transtype=file` for using TCP-like stream mode\n";
        return 1;
    }

    string loglevel = Option<OutString>(params, "error", o_loglevel);
    string logfa = Option<OutString>(params, "", o_logfa);
    srt_logging::LogLevel::type lev = SrtParseLogLevel(loglevel);
    UDT::setloglevel(lev);
    if (logfa == "")
    {
        UDT::addlogfa(SRT_LOGFA_APP);
    }
    else
    {
        // Add only selected FAs
        set<string> unknown_fas;
        set<srt_logging::LogFA> fas = SrtParseLogFA(logfa, &unknown_fas);
        UDT::resetlogfa(fas);

        // The general parser doesn't recognize the "app" FA, we check it here.
        if (unknown_fas.count("app"))
            UDT::addlogfa(SRT_LOGFA_APP);
    }

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

    string chunk = Option<OutString>(params, "", o_chunksize);
    if (chunk != "")
    {
        ::g_chunksize = stoi(chunk);
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

#ifdef _MSC_VER
	// Replacement for sigaction, just use 'signal'
	// This may make this working kinda impaired and unexpected,
	// but still better that not compiling at all.
	signal(SIGINT, OnINT_SetInterrupted);
#else
    struct sigaction sigIntHandler;

    sigIntHandler.sa_handler = OnINT_SetInterrupted;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
#endif

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

    // Start with output media so that they are ready when
    // the data come in.

    for (string spec: output_spec)
    {
        Verb() << "Setting up output: " << spec;
        unique_ptr<TargetMedium> m { new TargetMedium };
        m->Setup(this, Target::Create(spec));
        m_output_media.push_back(move(m));
    }


    // Start with SRT.

    UriParser srtspec(srt_uri);
    string transtype = srtspec["transtype"].deflt("live");

    SrtModel m(srtspec.host(), srtspec.portno(), srtspec.parameters());

    // Just to keep it unchanged.
    string id = m_srtspec["streamid"];

    Verb() << "Establishing SRT connection: " << srt_uri;

    ::g_pending_model = &m;
    m.Establish((id));

    ::g_program_established = true;
    ::g_pending_model = nullptr;

    Verb() << "... Established. configuring other pipes:";

    // Once it's ready, use it to initialize the medium.

    m_srt_relay.reset(new SrtRelay);
    m_srt_relay->StealFrom(m);

    m_srt_source.Setup(this, m_srt_relay.get());

    bool file_mode = (transtype == "file");

    if (g_chunksize == 0)
    {
        if (file_mode)
            g_chunksize = g_default_file_chunksize;
        else
            g_chunksize = g_default_live_chunksize;

        Verb() << "DEFAULT CHUNKSIZE used: " << g_chunksize;
    }

    // Now check the input medium
    if (input_echoback)
    {
        Verb() << "SRT set up as input source and the first output target";

        // Add SRT medium to output targets, and keep input medium empty.
        unique_ptr<TargetMedium> m { new TargetMedium };
        m->Setup(this, m_srt_relay.get());
        m_output_media.push_back(move(m));
    }
    else
    {
        // Initialize input medium and do not add SRT medium
        // to the output list, as this will be fed directly
        // by the data from this input medium in a spearate engine.
        Verb() << "Setting up input: " << input_spec;
        m_input_medium.Setup(this, Source::Create(input_spec));

        if (!file_mode)
        {
            // Also set writing to SRT non-blocking always.
            bool no = false;
            srt_setsockflag(m_srt_relay->Socket(), SRTO_SNDSYN, &no, sizeof no);
        }
    }

    // We're done here.
    Verb() << "MEDIA SUCCESSFULLY CREATED.";
}

void SrtMainLoop::InputRunner()
{
    ThreadName::set("InputRN");
    // An extra thread with a loop that reads from the external input
    // and writes into the SRT medium. When echoback mode is used,
    // this thread isn't started at all and instead the SRT reading
    // serves as both SRT reading and input reading.

    auto on_return_set = OnReturnSet(m_input_running, false);

    Verb() << VerbLock << "RUNNING INPUT LOOP";
    for (;;)
    {
        applog.Debug() << "SrtMainLoop::InputRunner: extracting...";
        auto data = m_input_medium.Extract();

        if (data.payload.empty())
        {
            Verb() << "INPUT READING INTERRUPTED.";
            break;
        }

        //Verb() << "INPUT [" << data.size() << "]  " << VerbNoEOL;
        applog.Debug() << "SrtMainLoop::InputRunner: [" << data.payload.size() << "] CLIENT -> SRT-RELAY";
        m_srt_relay->Write(data);
    }
}

void SrtMainLoop::run()
{
    // Start the media runners.

    Verb() << VerbLock << "STARTING OUTPUT threads:";

    for (auto& o: m_output_media)
        o->run();

    Verb() << VerbLock << "STARTING SRT INPUT LOOP";
    m_srt_source.run();

    Verb() << VerbLock << "STARTING INPUT ";
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

    Verb() << VerbLock << "RUNNING SRT MEDIA LOOP";
    for (;;)
    {
        applog.Debug() << "SrtMainLoop::run: SRT-RELAY: extracting...";
        auto data = m_srt_source.Extract();

        if (data.payload.empty())
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
            applog.Debug() << "SrtMainLoop::run: [" << data.payload.size() << "] SRT-RELAY: resending to output #" << no << "...";
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
        applog.Debug() << "SrtMainLoop::run: [" << data.payload.size() << "] SRT-RELAY -> OUTPUTS: " << Printable(output_report);

        if (Verbose::on)
        {
            string outputs;
            for (auto& r: output_report)
                outputs += " " + r;
            if (!any)
                outputs = " --> * (no output)";

            Verb() << VerbLock << "SRT [" << data.payload.size() << "]  " << outputs;
        }
    }

    Verb() << "MEDIA LOOP EXIT";
    m_srt_source.quit();

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
