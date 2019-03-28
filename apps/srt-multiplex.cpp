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
#include "transmitbase.hpp"
#include "transmitmedia.hpp"
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


using namespace std;

// The length of the SRT payload used in srt_recvmsg call.
// So far, this function must be used and up to this length of payload.
const size_t DEFAULT_CHUNK = 1316;

const srt_logging::LogFA SRT_LOGFA_APP = 10;
srt_logging::Logger applog(SRT_LOGFA_APP, srt_logger_config, "srt-mplex");

volatile bool siplex_int_state = false;
void OnINT_SetIntState(int)
{
    cerr << "\n-------- REQUESTED INTERRUPT!\n";
    siplex_int_state = true;
}

volatile bool alarm_state = false;
void OnALRM_SetAlarmState(int)
{
    alarm_state = true;
}

map<string,string> defined_streams;
string file_pattern = "output%.dat";

struct MediumPair
{
    unique_ptr<Source> src;
    unique_ptr<Target> tar;
    thread runner;
    size_t chunk = DEFAULT_CHUNK;
    volatile bool interrupted = false;
    volatile bool has_quit = false;
    bytevector initial_portion;
    string name;

    MediumPair(unique_ptr<Source> s, unique_ptr<Target> t): src(move(s)), tar(move(t)) {}

    void Stop()
    {
        interrupted = true;
        runner.join();
        src.reset();
        tar.reset();
    }

    void TransmissionLoop()
    {
        struct MarkQuit
        {
            volatile bool& q;

            ~MarkQuit()
            {
                q = true;
                applog.Note() << "MediumPair: Giving it 5 seconds delay before exiting";
                this_thread::sleep_for(chrono::seconds(5));
            }
        } mq { has_quit };

        applog.Note() << "STARTING TRANSMiSSION: " << name;

        if (!initial_portion.empty())
        {
            tar->Write(initial_portion);
            if ( tar->Broken() )
            {
                applog.Note() << "OUTPUT BROKEN for loop: " << name;
                return;
            }
            initial_portion.clear();
        }

        try
        {
            for (;;)
            {
                ostringstream sout;
                alarm(1);
                bytevector data;
                src->Read(chunk, data);
                alarm(0);
                if (alarm_state)
                {
                    alarm_state = false;
                    // This means that it's just a checkpoint.
                    if ( interrupted )
                        break;
                    continue;
                }
                sout << " << " << data.size() << "  ->  ";
                if ( data.empty() && src->End() )
                {
                    sout << "EOS";
                    applog.Note() << sout.str();
                    break;
                }
                tar->Write(data);
                if ( tar->Broken() )
                {
                    sout << " OUTPUT broken";
                    applog.Note() << sout.str();
                    break;
                }
                sout << " sent";
                if ( siplex_int_state )
                {
                    sout << " --- (interrupted on request)";
                    applog.Note() << sout.str();
                    break;
                }
                applog.Note() << sout.str();
            }
        }
        catch (Source::ReadEOF& x)
        {
            applog.Note() << "EOS - closing media for loop: " << name;
            src->Close();
            tar->Close();
            applog.Note() << "CLOSED: " << name;
        }
        catch (std::runtime_error& x)
        {
            applog.Note() << "INTERRUPTED: " << x.what();
            src->Close();
            tar->Close();
            applog.Note() << "CLOSED: " << name;
        }
        catch (...)
        {
            applog.Note() << "UNEXPECTED EXCEPTION, rethrowing";
            throw;
        }
    }
};


class MediaBase
{
public:
    list<MediumPair> media;

    /// Take the Source and Target and bind them for a transmission.
    /// This spawns a thread for transmission.
    /// @param src source medium
    /// @param tar target medium
    /// @param initial_portion First portion of data read from @c src for any extra checks, which
    ///        are still meant to be delivered to @c tar
    MediumPair& Link(std::unique_ptr<Source> src, std::unique_ptr<Target> tar, bytevector&& initial_portion, string name, string thread_name)
    {
        media.emplace_back(move(src), move(tar));
        MediumPair& med = media.back();
        med.initial_portion = move(initial_portion);
        med.name = name;

        // Ok, got this, so we can start transmission.
        ThreadName tn(thread_name.c_str());

        med.runner = thread( [&med]() { med.TransmissionLoop(); });
        return med;
    }

    void StopAll()
    {
        for (auto& x: media)
            x.Stop();
    }

    ~MediaBase()
    {
        StopAll();
    }

} g_media_base;

string ResolveFilePattern(int number)
{
    vector<string> parts;
    Split(::file_pattern, '%', back_inserter(parts));
    ostringstream os;
    os << parts[0];
    for (auto i = parts.begin()+1; i < parts.end(); ++i)
        os << number << *i;

    return os.str();
}

string SelectMedium(string id, bool mode_output)
{
    static int number = 0;

    // Empty ID is incorrect.
    if ( id == "" )
    {
        applog.Error() << "SelectMedium: empty id";
        return "";
    }

    string uri = map_get(defined_streams, id);

    // Test the URI if it is openable.
    UriParser u(uri);
    if ( u.scheme() == "file" && u.path() == "" )
    {
        if (mode_output)
        {
            ++number;
            string sol = ResolveFilePattern(number);
            applog.Warn() << "SelectMedium: for [" << id << "] uri '" << uri << "' is file with no path - autogenerating filename: " << sol;
            return sol;
        }
        applog.Error() << "SelectMedium: id not found: [" << id << "]";
        return "";
    }

    applog.Note() << "SelectMedium: for [" << id << "] found medium: " << uri;
    return uri;
}

bool PrepareStreamNames(const map<string,vector<string>>& params, bool mode_output)
{
    vector<string> v;
    string flag;

    if (mode_output)
    {
        // You have an incoming stream over SRT and you need to
        // redirect it to the correct locally defined output stream.

        if (params.count("o") && !params.at("o").empty())
        {
            // We have a defined list of parameters.
            // Check if there's just one item and it's a file pattern

            // Each stream needs to be defined separately, at least to have IDs
            // If this is a file without path, use the default file pattern.

            v = params.at("o");
            flag = "o";
        }
    }
    else
    {
        // You have some input media and you want to send them all
        // over SRT medium.
        if (params.count("i"))
        {
            v = params.at("i");
            flag = "i";
        }
    }

    if ( v.empty() )
        return false;

    for (string& s: v)
    {
        UriParser u(s);
        string id = u["id"];
        if ( id != "" )
        {
            defined_streams[id] = s;
        }
        else
        {
            cerr << "Parameter at -" << flag << " without id: " << s << endl;
            return false;
        }
    }

    return true;
}

bool SelectAndLink(SrtModel& m, string id, bool mode_output)
{
    // So, we have made a connection that is now contained in m.
    // For that connection we need to select appropriate stream
    // to send.
    //
    // XXX
    // Currently only one method implemented: select appropriate number from the list.

    // If SRT mode is caller, then SelectMedium will always return
    // a nonempty string that is a key in defined_streams map.
    // This is because in this case the id comes directly from
    // that map's keys.
    string medium = SelectMedium(id, mode_output);
    if ( medium == "" )
    {
        // No medium available for that stream, ignore it.
        m.Close();
        return false;
    }

    // Now create a medium and store.
    unique_ptr<Source> source;
    unique_ptr<Target> target;
    string name;
    ostringstream os;
    SRTSOCKET sock = m.Socket();

    string thread_name;

    if ( mode_output )
    {
        // Create Source out of SrtModel and Target from the given medium
        auto s = new SrtSource();
        s->StealFrom(m);
        source.reset(s);

        target = Target::Create(medium);

        os << m.m_host << ":" << m.m_port << "[" << id << "]%" << sock << "  ->  " << medium;
        thread_name = "TL>" + medium;
    }
    else
    {
        // Create Source of given medium and Target of SrtModel.
        source = Source::Create(medium);
        auto t = new SrtTarget();
        t->StealFrom(m);
        target.reset(t);

        os << medium << "  ->  " << m.m_host << ":" << m.m_port << "[" << id << "]%" << sock;
        thread_name = "TL<" + medium;
    }

    bytevector dummy_initial_portion;
    g_media_base.Link(move(source), move(target), move(dummy_initial_portion), os.str(), thread_name);

    return true;
}

void Stall()
{
    // Call this function if everything is running in their own
    // threads and there's nothing more to run. Check periodically
    // if all threads are still alive, quit if all are dead.

    while (!siplex_int_state)
    {
        this_thread::sleep_for(chrono::seconds(1));

        // Check all cars if any crashed
        for (auto i = g_media_base.media.begin(), i_next = i; i != g_media_base.media.end(); i = i_next)
        {
            ++i_next;
            if (i->has_quit)
            {
                Verb() << "Found QUIT mediumpair: " << i->name << " - removing from base";
                i->Stop();
                g_media_base.media.erase(i);
            }
        }

        if (g_media_base.media.empty())
        {
            Verb() << "All media have quit. Marking exit.";
            break;
        }
    }
}


void Usage(string program)
{
    cerr << "Usage: " << program << " <SRT URI> [-i INPUT...] [-o OUTPUT...]\n";
}

void Help(string program)
{
    Usage(program);
    cerr << endl;
    cerr <<
"SIPLEX is a program that demonstrates two SRT features:\n"
" - using one UDP outgoing port for multiple connecting SRT sockets\n"
" - setting a resource ID on a socket visible on the listener side\n"
"\n"
"The <SRT URI> will be input or output depending on the further -i/-o option.\n"
"The URIs specified as -i INPUT... will be used for input and therefore SRT for output,\n"
"and in the other way around if you use -o OUTPUT...\n"
"For every such URI you must specify additionally a parameter named 'id', which will be\n"
"interperted by the application and used to set resource id on an SRT socket when connecting\n"
"or to match with the id extracted from the accepted socket of incoming connection.\n"
"Example:\n"
"\tSender:    srt-multiplex srt://remhost:2000 -i udp://:5000?id=low udp://:6000?id=high\n"
"\tReceiver:  srt-multiplex srt://:2000 -o output-high.ts?id=high output-low.ts?id=low\n"
"\nHere you create a Sender which will connect to 'remhost' port 2000 using multiple SRT\n"
"sockets, all of which will be using the same outgoing port. Here the port is autoselected\n"
"by the first socket when connecting, every next one will reuse that port. Alternatively you\n"
"can enforce the outgoing port using 'port' parameter in the SRT URI.\n\n"
"Then for every input resource a separate connection is made and appropriate resource id\n"
"will be set to particular socket assigned to that resource according to the 'id' parameter.\n"
"When the listener side (here Receiver) gets the socket accepted, it will have the resource\n"
"id set just as the caller side did, in which case srt-multiplex will search for this id among\n"
"the registered resources and match the resource (output here) with this id. If the resource is\n"
"not found, the connection is closed immediately. This works the same way regardless of which\n"
"direction is used by caller or listener\n";

}

int main( int argc, char** argv )
{
    // This is mainly required on Windows to initialize the network system,
    // for a case when the instance would use UDP. SRT does it on its own, independently.
    if ( !SysInitializeNetwork() )
        throw std::runtime_error("Can't initialize network!");

    // Initialize signals

    signal_alarm(OnALRM_SetAlarmState);
    signal(SIGINT, OnINT_SetIntState);
    signal(SIGTERM, OnINT_SetIntState);

    // Symmetrically, this does a cleanup; put into a local destructor to ensure that
    // it's called regardless of how this function returns.
    struct NetworkCleanup
    {
        ~NetworkCleanup()
        {
            SysCleanupNetwork();
        }
    } cleanupobj;

    // Check options
    vector<OptionScheme> optargs = {
        { {"ll", "loglevel"}, OptionScheme::ARG_ONE },
        { {"i"}, OptionScheme::ARG_VAR },
        { {"o"}, OptionScheme::ARG_VAR }
    };
    map<string, vector<string>> params = ProcessOptions(argv, argc, optargs);

    // The call syntax is:
    //
    //      srt-multiplex <SRT URI> -o/-i ARGS...
    //
    // SRT URI should contain:
    // srt://[host]:port?mode=MODE&adapter=ADAPTER&port=PORT&otherparameters...
    // 
    // Extra parameters:
    //
    // mode: caller/listener/rendezvous. Default: if host empty, listener, otherwise caller.
    // adapter: IP to select network device for listner or rendezvous. Default: for listener taken from host, otherwise 0.0.0.0
    // port: default=0. Used only for caller mode, sets the outgoing port number. If 0, system-selected (default behavior)
    //
    // Syntax cases for -i:
    //
    // Every item from ARGS... is an input URI. For every such case a new socket should be
    // created and the data should be transmitted through that socket.
    //
    // Syntax cases for -o:
    //
    // EMPTY ARGS...: use 'output%.dat' file patter for every stream.
    // PATTERN (one argument that contains % somewhere): define the output file pattern
    // URI...: try to match the input stream to particular URI by 'name' parameter. If none matches, ignore.

    if ( params.count("-help") )
    {
        Help(argv[0]);
        return 1;
    }

    if ( params[""].empty() )
    {
        Usage(argv[0]);
        return 1;
    }

    if (params[""].size() > 1)
    {
        cerr << "Extra parameter after the first one: " << Printable(params[""]) << endl;
        return 1;
    }

    // Force exist
    (void)params["o"];
    (void)params["i"];

    if (!params["o"].empty() && !params["i"].empty())
    {
        cerr << "Input-output mixed mode not supported. Specify either -i or -o.\n";
        return 1;
    }

    bool mode_output = false;

    if (params["i"].empty())
    {
        mode_output = true;
    }

    if ( !PrepareStreamNames(params, mode_output))
    {
        cerr << "Incorrect input/output specification\n";
        return 1;
    }

    if ( defined_streams.empty() )
    {
        cerr << "No streams defined\n";
        return 1;
    }

    string loglevel = Option<OutString>(params, "error", "ll", "loglevel");
    srt_logging::LogLevel::type lev = SrtParseLogLevel(loglevel);
    UDT::setloglevel(lev);
    UDT::addlogfa(SRT_LOGFA_APP);

    string verbo = Option<OutString>(params, "no", "v", "verbose");
    if ( verbo == "" || !false_names.count(verbo) )
        Verbose::on = true;


    string srt_uri = params[""][0];

    UriParser up(srt_uri);

    if ( up.scheme() != "srt" )
    {
        cerr << "First parameter must be a SRT-scheme URI\n";
        return 1;
    }

    int iport = atoi(up.port().c_str());
    if ( iport <= 1024 )
    {
        cerr << "Port value invalid: " << iport << " - must be >1024\n";
        return 1;
    }

    SrtModel m(up.host(), iport, up.parameters());

    ThreadName::set("main");

    // Note: for input, there must be an exactly defined
    // number of sources. The loop rolls up to all these sources.
    //
    // For output, if you use defined output URI, roll the loop until
    // they are all managed.
    // If you use file pattern, then:
    // - if SRT is in listener mode, just listen infinitely
    // - if SRT is in caller mode, the limit number of the streams must be used. Default is 10.

    set<string> ids;
    for (auto& mp: defined_streams)
        ids.insert(mp.first);

    try
    {
        for(;;)
        {
            string id = *ids.begin();
            m.Establish(Ref(id));

            // The 'id' could have been altered.
            // If Establish did connect(), then it gave this stream id,
            // in which case it will return unchanged. If it did accept(),
            // then it will be overwritten with the received stream id.
            // Whatever the result was, we need to bind the transmitter with
            // the local resource of this id, and if this failed, simply
            // close the stream and ignore it.

            // Select medium from parameters.
            if ( SelectAndLink(m, id, mode_output) )
            {
                ids.erase(id);
                if (ids.empty())
                    break;
            }

            ThreadName::set("main");
        }

        applog.Note() << "All local stream definitions covered. Waiting for interrupt/broken all connections.";
        Stall();
    }
    catch (std::exception& x)
    {
        cerr << "CATCH!\n" << x.what() << endl;;
    }
}





