// MSVS likes to complain about lots of standard C functions being unsafe.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#include <io.h>
#endif

#include "platform_sys.h"

#define REQUIRE_CXX11 1

#include <cctype>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <cstring>
#include <csignal>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "apputil.hpp"  // CreateAddrInet
#include "uriparser.hpp"  // UriParser
#include "socketoptions.hpp"
#include "logsupport.hpp"
#include "transmitbase.hpp" // bytevector typedef to avoid collisions
#include "verbose.hpp"

// NOTE: This is without "haisrt/" because it uses an internal path
// to the library. Application using the "installed" library should
// use <srt/srt.h>
#include <srt.h>
#include <udt.h> // This TEMPORARILY contains extra C++-only SRT API.
#include <logging.h>
#include <api.h>
#include <utilities.h>

/*
# MAF contents for this file. Note that not every file from the support
# library is used, but to simplify the build definition it links against
# the whole srtsupport library.

SOURCES
srt-test-tunnel.cpp
testmedia.cpp
../apps/verbose.cpp
../apps/socketoptions.cpp
../apps/uriparser.cpp
../apps/logsupport.cpp

*/

using namespace std;

class Medium
{
    static int s_counter;
    int m_counter;
public:
    enum ReadStatus
    {
        RD_DATA, RD_AGAIN, RD_EOF, RD_ERROR
    };

    enum Mode
    {
        LISTENER, CALLER
    };

protected:
    UriParser m_uri;
    size_t m_chunk = 0;
    map<string, string> m_options;
    Mode m_mode;

    bool m_listener = false;
    bool m_open = false;
    bool m_eof = false;
    bool m_broken = false;

    mutex access; // For closing

    template <class DerivedMedium, class SocketType>
    static Medium* CreateAcceptor(DerivedMedium* self, const sockaddr_in& sa, SocketType sock, size_t chunk)
    {
        string addr = SockaddrToString(sockaddr_any((sockaddr*)&sa, sizeof sa));
        DerivedMedium* m = new DerivedMedium(UriParser(self->type() + string("://") + addr), chunk);
        m->m_socket = sock;
        return m;
    }

public:

    string uri() { return m_uri.uri(); }
    string id()
    {
        std::ostringstream os;
        os << type() << m_counter;
        return os.str();
    }

    Medium(UriParser u, size_t ch): m_counter(s_counter++), m_uri(u), m_chunk(ch) {}
    Medium(): m_counter(s_counter++) {}

    virtual const char* type() = 0;
    virtual bool IsOpen() = 0;
    virtual void Close() = 0;
    virtual bool End() = 0;

    virtual int ReadInternal(char* output, int size) = 0;
    virtual bool IsErrorAgain() = 0;

    ReadStatus Read(bytevector& output);
    virtual void Write(bytevector& portion) = 0;

    virtual void CreateListener() = 0;
    virtual void CreateCaller() = 0;
    virtual unique_ptr<Medium> Accept() = 0;
    virtual void Connect() = 0;

    static std::unique_ptr<Medium> Create(const std::string& url, size_t chunk, Mode);

    virtual bool Broken() = 0;
    virtual size_t Still() { return 0; }

    class ReadEOF: public std::runtime_error
    {
    public:
        ReadEOF(const std::string& fn): std::runtime_error( "EOF while reading file: " + fn )
        {
        }
    };

    class TransmissionError: public std::runtime_error
    {
    public:
        TransmissionError(const std::string& fn): std::runtime_error( fn )
        {
        }
    };

    static void Error(const string& text)
    {
        throw TransmissionError("ERROR (internal): " + text);
    }

    virtual ~Medium()
    {
    }

protected:
    void InitMode(Mode m)
    {
        m_mode = m;
        Init();

        if (m_mode == LISTENER)
        {
            CreateListener();
            m_listener = true;
        }
        else
        {
            CreateCaller();
        }

        m_open = true;
    }

    virtual void Init() {}

};

class Engine
{
    Medium* media[2];
    std::thread thr;
    class Tunnel* parent_tunnel;
    std::string nameid;

    int status = 0;
    Medium::ReadStatus rdst = Medium::RD_ERROR;
    UDT::ERRORINFO srtx;

public:
    enum Dir { DIR_IN, DIR_OUT };

    int stat() { return status; }

    Engine(Tunnel* p, Medium* m1, Medium* m2, const std::string& nid)
        :
#ifdef HAVE_FULL_CXX11
		media {m1, m2},
#endif
		parent_tunnel(p), nameid(nid)
    {
#ifndef HAVE_FULL_CXX11
		// MSVC is not exactly C++11 compliant and complains around
		// initialization of an array.
		// Leaving this method of initialization for clarity and
		// possibly more preferred performance.
		media[0] = m1;
		media[1] = m2;
#endif
    }

    void Start()
    {
        Verb() << "START: " << media[DIR_IN]->uri() << " --> " << media[DIR_OUT]->uri();
        std::string thrn = media[DIR_IN]->id() + ">" + media[DIR_OUT]->id();
        ThreadName tn(thrn.c_str());

        thr = thread([this]() { Worker(); });
    }

    void Stop()
    {
        // If this thread is already stopped, don't stop.
        if (thr.joinable())
        {
            if (thr.get_id() == std::this_thread::get_id())
            {
                // If this is this thread which called this, no need
                // to stop because this thread will exit by itself afterwards.
                // You must, however, detach yourself, or otherwise the thr's
                // destructor would kill the program.
                thr.detach();
            }
            else
            {
                thr.join();
            }
        }
    }

    void Worker();
};


struct Tunnelbox;

class Tunnel
{
    Tunnelbox* parent_box;
    std::unique_ptr<Medium> med_acp, med_clr;
    Engine acp_to_clr, clr_to_acp;
    volatile bool running = true;
    mutex access;

public:

    string show()
    {
        return med_acp->uri() + " <-> " + med_clr->uri();
    }

    Tunnel(Tunnelbox* m, std::unique_ptr<Medium>&& acp, std::unique_ptr<Medium>&& clr):
        parent_box(m),
        med_acp(move(acp)), med_clr(move(clr)),
        acp_to_clr(this, med_acp.get(), med_clr.get(), med_acp->id() + ">" + med_clr->id()),
        clr_to_acp(this, med_clr.get(), med_acp.get(), med_clr->id() + ">" + med_acp->id())
    {
    }

    void Start()
    {
        acp_to_clr.Start();
        clr_to_acp.Start();
    }

    // This is to be called by an Engine from Engine::Worker
    // thread.
    // [[affinity = acp_to_clr.thr || clr_to_acp.thr]];
    void decommission_engine(Medium* which_medium)
    {
        // which_medium is the medium that failed.
        // Upon breaking of one medium from the pair,
        // the other needs to be closed as well.
        Verb() << "Medium broken: " << which_medium->uri();

        bool stop = true;

        /*
        {
            lock_guard<mutex> lk(access);
            if (acp_to_clr.stat() == -1 && clr_to_acp.stat() == -1)
            {
                Verb() << "Tunnel: Both engine decommissioned, will stop the tunnel.";
                // Both engines are down, decommission the tunnel.
                // Note that the status -1 means that particular engine
                // is not currently running and you can safely
                // join its thread.
                stop = true;
            }
            else
            {
                Verb() << "Tunnel: Decommissioned one engine, waiting for the other one to report";
            }
        }
        */

        if (stop)
        {
            // First, stop all media.
            med_acp->Close();
            med_clr->Close();

            // Then stop the tunnel (this is only a signal
            // to a cleanup thread to delete it).
            Stop();
        }
    }

    void Stop();

    bool decommission_if_dead(bool forced); // [[affinity = g_tunnels.thr]]
};

void Engine::Worker()
{
    bytevector outbuf;

    Medium* which_medium = media[DIR_IN];

    for (;;)
    {
        try
        {
            which_medium = media[DIR_IN];
            rdst = media[DIR_IN]->Read((outbuf));
            switch (rdst)
            {
            case Medium::RD_DATA:
                {
                    which_medium = media[DIR_OUT];
                    // We get the data, write them to the output
                    media[DIR_OUT]->Write((outbuf));
                }
                break;

            case Medium::RD_EOF:
                status = -1;
                throw Medium::ReadEOF("");

            case Medium::RD_AGAIN:
            case Medium::RD_ERROR:
                status = -1;
                Medium::Error("Error while reading");
            }
        }
        catch (Medium::ReadEOF&)
        {
            Verb() << "EOF. Exitting engine.";
            break;
        }
        catch (Medium::TransmissionError& er)
        {
            Verb() << er.what() << " - interrupting engine: " << nameid;
            break;
        }
    }

    // This is an engine thread and it should simply
    // tell the parent_box Tunnel that it is no longer
    // operative. It's not necessary to inform it which
    // of two engines is decommissioned - it should only
    // know that one of them got down. It will then check
    // if both are down here and decommission the whole
    // tunnel if so.
    parent_tunnel->decommission_engine(which_medium);
}

class SrtMedium: public Medium
{
    SRTSOCKET m_socket = SRT_ERROR;
    friend class Medium;
public:

#ifdef HAVE_FULL_CXX11
    using Medium::Medium;

#else // MSVC and gcc 4.7 not exactly support C++11

    SrtMedium(UriParser u, size_t ch): Medium(u, ch) {}

#endif

    bool IsOpen() override { return m_open; }
    bool End() override { return m_eof; }
    bool Broken() override { return m_broken; }

    void Close() override
    {
        Verb() << "Closing SRT socket for " << uri();
        lock_guard<mutex> lk(access);
        if (m_socket == SRT_ERROR)
            return;
        srt_close(m_socket);
        m_socket = SRT_ERROR;
    }

    virtual const char* type() override { return "srt"; }
    virtual int ReadInternal(char* output, int size) override;
    virtual bool IsErrorAgain() override;

    virtual void Write(bytevector& portion) override;
    virtual void CreateListener() override;
    virtual void CreateCaller() override;
    virtual unique_ptr<Medium> Accept() override;
    virtual void Connect() override;

protected:
    virtual void Init() override;

    void ConfigurePre();
    void ConfigurePost(SRTSOCKET socket);

    using Medium::Error;

    static void Error(UDT::ERRORINFO& ri, const string& text)
    {
        throw TransmissionError("ERROR: " + text + ": " + ri.getErrorMessage());
    }

    virtual ~SrtMedium() override
    {
        Close();
    }
};

class TcpMedium: public Medium
{
    int m_socket = -1;
    friend class Medium;
public:

#ifdef HAVE_FULL_CXX11
    using Medium::Medium;

#else // MSVC not exactly supports C++11

    TcpMedium(UriParser u, size_t ch): Medium(u, ch) {}

#endif

#ifdef _WIN32
    static int tcp_close(int socket)
    {
        return ::closesocket(socket);
    }

    enum { DEF_SEND_FLAG = 0 };

#elif defined(LINUX) || defined(GNU) || defined(CYGWIN)
    static int tcp_close(int socket)
    {
        return ::close(socket);
    }

    enum { DEF_SEND_FLAG = MSG_NOSIGNAL };

#else
    static int tcp_close(int socket)
    {
        return ::close(socket);
    }

    enum { DEF_SEND_FLAG = 0 };

#endif

    bool IsOpen() override { return m_open; }
    bool End() override { return m_eof; }
    bool Broken() override { return m_broken; }

    void Close() override
    {
        Verb() << "Closing TCP socket for " << uri();
        lock_guard<mutex> lk(access);
        if (m_socket == -1)
            return;
        tcp_close(m_socket);
        m_socket = -1;
    }

    virtual const char* type() override { return "tcp"; }
    virtual int ReadInternal(char* output, int size) override;
    virtual bool IsErrorAgain() override;
    virtual void Write(bytevector& portion) override;
    virtual void CreateListener() override;
    virtual void CreateCaller() override;
    virtual unique_ptr<Medium> Accept() override;
    virtual void Connect() override;

protected:

    void ConfigurePre()
    {
#if defined(__APPLE__)
        int optval = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval));
#endif
    }

    void ConfigurePost(int)
    {
    }

    using Medium::Error;

    static void Error(int verrno, const string& text)
    {
        char rbuf[1024];
        throw TransmissionError("ERROR: " + text + ": " + SysStrError(verrno, rbuf, 1024));
    }

    virtual ~TcpMedium()
    {
        Close();
    }
};

void SrtMedium::Init()
{
    // This function is required due to extra option
    // check need

    if (m_options.count("mode"))
        Error("No option 'mode' is required, it defaults to position of the argument");

    if (m_options.count("blocking"))
        Error("Blocking is not configurable here.");

    // XXX
    // Look also for other options that should not be here.

    // Enforce the transtype = file
    m_options["transtype"] = "file";
}

void SrtMedium::ConfigurePre()
{
    vector<string> fails;
    m_options["mode"] = "caller";
    SrtConfigurePre(m_socket, "", m_options, &fails);
    if (!fails.empty())
    {
        cerr << "Failed options: " << Printable(fails) << endl;
    }
}

void SrtMedium::ConfigurePost(SRTSOCKET so)
{
    vector<string> fails;
    SrtConfigurePost(so, m_options, &fails);
    if (!fails.empty())
    {
        cerr << "Failed options: " << Printable(fails) << endl;
    }
}

void SrtMedium::CreateListener()
{
    int backlog = 5; // hardcoded!

    m_socket = srt_create_socket();

    ConfigurePre();

    sockaddr_in sa = CreateAddrInet(m_uri.host(), m_uri.portno());

    int stat = srt_bind(m_socket, (sockaddr*)&sa, sizeof sa);

    if ( stat == SRT_ERROR )
    {
        srt_close(m_socket);
        Error(UDT::getlasterror(), "srt_bind");
    }

    stat = srt_listen(m_socket, backlog);
    if ( stat == SRT_ERROR )
    {
        srt_close(m_socket);
        Error(UDT::getlasterror(), "srt_listen");
    }

    m_listener = true;
};

void TcpMedium::CreateListener()
{
    int backlog = 5; // hardcoded!

    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ConfigurePre();

    sockaddr_in sa = CreateAddrInet(m_uri.host(), m_uri.portno());

    int stat = ::bind(m_socket, (sockaddr*)&sa, sizeof sa);

    if (stat == -1)
    {
        tcp_close(m_socket);
        Error(errno, "bind");
    }

    stat = listen(m_socket, backlog);
    if ( stat == -1 )
    {
        tcp_close(m_socket);
        Error(errno, "listen");
    }

    m_listener = true;
}

unique_ptr<Medium> SrtMedium::Accept()
{
    sockaddr_in sa;
    int salen = sizeof sa;
    SRTSOCKET s = srt_accept(m_socket, (sockaddr*)&sa, &salen);
    if (s == SRT_ERROR)
    {
        Error(UDT::getlasterror(), "srt_accept");
    }

    ConfigurePost(s);
    unique_ptr<Medium> med(CreateAcceptor(this, sa, s, m_chunk));
    Verb() << "accepted a connection from " << med->uri();

    return med;
}

unique_ptr<Medium> TcpMedium::Accept()
{
    sockaddr_in sa;
    socklen_t salen = sizeof sa;
    int s = ::accept(m_socket, (sockaddr*)&sa, &salen);
    if (s == -1)
    {
        Error(errno, "accept");
    }

    unique_ptr<Medium> med(CreateAcceptor(this, sa, s, m_chunk));
    Verb() << "accepted a connection from " << med->uri();

    return med;
}

void SrtMedium::CreateCaller()
{
    m_socket = srt_create_socket();
    ConfigurePre();

    // XXX setting up outgoing port not supported
}

void TcpMedium::CreateCaller()
{
    m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ConfigurePre();
}

void SrtMedium::Connect()
{
    sockaddr_in sa = CreateAddrInet(m_uri.host(), m_uri.portno());

    int st = srt_connect(m_socket, (sockaddr*)&sa, sizeof sa);
    if (st == SRT_ERROR)
        Error(UDT::getlasterror(), "srt_connect");

    ConfigurePost(m_socket);
}

void TcpMedium::Connect()
{
    sockaddr_in sa = CreateAddrInet(m_uri.host(), m_uri.portno());

    int st = ::connect(m_socket, (sockaddr*)&sa, sizeof sa);
    if (st == -1)
        Error(errno, "connect");

    ConfigurePost(m_socket);
}

int SrtMedium::ReadInternal(char* w_buffer, int size)
{
    int st = srt_recv(m_socket, (w_buffer), size);
    if (st == SRT_ERROR)
        return -1;
    return st;
}

int TcpMedium::ReadInternal(char* w_buffer, int size)
{
    return ::recv(m_socket, (w_buffer), size, 0);
}

bool SrtMedium::IsErrorAgain()
{
    return srt_getlasterror(NULL) == SRT_EASYNCRCV;
}

bool TcpMedium::IsErrorAgain()
{
    return errno == EAGAIN;
}

// The idea of Read function is to get the buffer that
// possibly contains some data not written to the output yet,
// but the time has come to read. We can't let the buffer expand
// more than the size of the chunk, so if the buffer size already
// exceeds it, don't return any data, but behave as if they were read.
// This will cause the worker loop to redirect to Write immediately
// thereafter and possibly will flush out the remains of the buffer.
// It's still possible that the buffer won't be completely purged
Medium::ReadStatus Medium::Read(bytevector& w_output)
{
    // Don't read, but fake that you read
    if (w_output.size() > m_chunk)
    {
        Verb() << "BUFFER EXCEEDED";
        return RD_DATA;
    }

    // Resize to maximum first
    size_t shift = w_output.size();
    if (shift && m_eof)
    {
        // You have nonempty buffer, but eof was already
        // encountered. Report as if something was read.
        //
        // Don't read anything because this will surely
        // result in error since now.
        return RD_DATA;
    }

    size_t pred_size = shift + m_chunk;

    w_output.resize(pred_size);
    int st = ReadInternal((w_output.data() + shift), m_chunk);
    if (st == -1)
    {
        if (IsErrorAgain())
            return RD_AGAIN;

        return RD_ERROR;
    }

    if (st == 0)
    {
        m_eof = true;
        if (shift)
        {
            // If there's 0 (eof), but you still have data
            // in the buffer, fake that they were read. Only
            // when the buffer was empty at entrance should this
            // result with EOF.
            //
            // Set back the size this buffer had before we attempted
            // to read into it.
            w_output.resize(shift);
            return RD_DATA;
        }
        w_output.clear();
        return RD_EOF;
    }

    w_output.resize(shift+st);
    return RD_DATA;
}

void SrtMedium::Write(bytevector& w_buffer)
{
    int st = srt_send(m_socket, w_buffer.data(), w_buffer.size());
    if (st == SRT_ERROR)
    {
        Error(UDT::getlasterror(), "srt_send");
    }

    // This should be ==, whereas > is not possible, but
    // this should simply embrace this case as a sanity check.
    if (st >= int(w_buffer.size()))
        w_buffer.clear();
    else if (st == 0)
    {
        Error("Unexpected EOF on Write");
    }
    else
    {
        // Remove only those bytes that were sent
        w_buffer.erase(w_buffer.begin(), w_buffer.begin()+st);
    }
}

void TcpMedium::Write(bytevector& w_buffer)
{
    int st = ::send(m_socket, w_buffer.data(), w_buffer.size(), DEF_SEND_FLAG);
    if (st == -1)
    {
        Error(errno, "send");
    }

    // This should be ==, whereas > is not possible, but
    // this should simply embrace this case as a sanity check.
    if (st >= int(w_buffer.size()))
        w_buffer.clear();
    else if (st == 0)
    {
        Error("Unexpected EOF on Write");
    }
    else
    {
        // Remove only those bytes that were sent
        w_buffer.erase(w_buffer.begin(), w_buffer.begin()+st);
    }
}

std::unique_ptr<Medium> Medium::Create(const std::string& url, size_t chunk, Medium::Mode mode)
{
    UriParser uri(url);
    std::unique_ptr<Medium> out;

    // Might be something smarter, but there are only 2 types.
    if (uri.scheme() == "srt")
    {
        out.reset(new SrtMedium(uri, chunk));
    }
    else if (uri.scheme() == "tcp")
    {
        out.reset(new TcpMedium(uri, chunk));
    }
    else
    {
        Error("Medium not supported");
    }

    out->InitMode(mode);

    return out;
}

struct Tunnelbox
{
    list<unique_ptr<Tunnel>> tunnels;
    mutex access;
    condition_variable decom_ready;
    bool main_running = true;
    thread thr;

    void signal_decommission()
    {
        lock_guard<mutex> lk(access);
        decom_ready.notify_one();
    }

    void install(std::unique_ptr<Medium>&& acp, std::unique_ptr<Medium>&& clr)
    {
        lock_guard<mutex> lk(access);
        Verb() << "Tunnelbox: Starting tunnel: " << acp->uri() << " <-> " << clr->uri();

        tunnels.emplace_back(new Tunnel(this, move(acp), move(clr)));
        // Note: after this instruction, acp and clr are no longer valid!
        auto& it = tunnels.back();

        it->Start();
    }

    void start_cleaner()
    {
        thr = thread( [this]() { CleanupWorker(); } );
    }

    void stop_cleaner()
    {
        if (thr.joinable())
            thr.join();
    }

private:

    void CleanupWorker()
    {
        unique_lock<mutex> lk(access);

        while (main_running)
        {
            decom_ready.wait(lk);

            // Got a signal, find a tunnel ready to cleanup.
            // We just get the signal, but we don't know which
            // tunnel has generated it.
            for (auto i = tunnels.begin(), i_next = i; i != tunnels.end(); i = i_next)
            {
                ++i_next;
                // Bound in one call the check if the tunnel is dead
                // and decommissioning because this must be done in
                // the one critical section - make sure no other thread
                // is accessing it at the same time and also make join all
                // threads that might have been accessing it. After
                // exitting as true (meaning that it was decommissioned
                // as expected) it can be safely deleted.
                if ((*i)->decommission_if_dead(main_running))
                {
                    tunnels.erase(i);
                }
            }
        }
    }

};

void Tunnel::Stop()
{
    // Check for running must be done without locking
    // because if the tunnel isn't running
    if (!running)
        return; // already stopped

    lock_guard<mutex> lk(access);

    // Ok, you are the first to make the tunnel
    // not running and inform the tunnelbox.
    running = false;
    parent_box->signal_decommission();
}

bool Tunnel::decommission_if_dead(bool forced)
{
    lock_guard<mutex> lk(access);
    if (running && !forced)
        return false; // working, not to be decommissioned

    // Join the engine threads, make sure nothing
    // is running that could use the data.
    acp_to_clr.Stop();
    clr_to_acp.Stop();


    // Done. The tunnelbox after calling this can
    // safely delete the decommissioned tunnel.
    return true;
}

int Medium::s_counter = 1;

Tunnelbox g_tunnels;
std::unique_ptr<Medium> main_listener;

size_t default_chunk = 4096;

const srt_logging::LogFA SRT_LOGFA_APP = 10;

int OnINT_StopService(int)
{
    g_tunnels.main_running = false;
    g_tunnels.signal_decommission();

    // Will cause the Accept() block to exit.
    main_listener->Close();

    return 0;
}

int main( int argc, char** argv )
{
    if (!SysInitializeNetwork())
    {
        cerr << "Fail to initialize network module.";
        return 1;
    }

    size_t chunk = default_chunk;

    OptionName
        o_loglevel = { "ll", "loglevel" },
        o_logfa = { "lf", "logfa" },
        o_chunk = {"c", "chunk" },
        o_verbose = {"v", "verbose" },
        o_noflush = {"s", "skipflush" };

    // Options that expect no arguments (ARG_NONE) need not be mentioned.
    vector<OptionScheme> optargs = {
        { o_loglevel, OptionScheme::ARG_ONE },
        { o_logfa, OptionScheme::ARG_ONE },
        { o_chunk, OptionScheme::ARG_ONE }
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
    if ( args.size() < 2 )
    {
        cerr << "Usage: " << argv[0] << " <listen-uri> <call-uri>\n";
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
        Verbose::cverb = &std::cout;
    }

    string chunks = Option<OutString>(params, "", o_chunk);
    if ( chunks!= "" )
    {
        chunk = stoi(chunks);
    }

    string listen_node = args[0];
    string call_node = args[1];

    UriParser ul(listen_node), uc(call_node);

    // It is allowed to use both media of the same type,
    // but only srt and tcp are allowed.

    set<string> allowed = {"srt", "tcp"};
    if (!allowed.count(ul.scheme())|| !allowed.count(uc.scheme()))
    {
        cerr << "ERROR: only tcp and srt schemes supported";
        return -1;
    }

    Verb() << "LISTEN type=" << ul.scheme() << ", CALL type=" << uc.scheme();

    g_tunnels.start_cleaner();

    main_listener = Medium::Create(listen_node, chunk, Medium::LISTENER);

    // The main program loop is only to catch
    // new connections and manage them. Also takes care
    // of the broken connections.

    for (;;)
    {
        try
        {
            Verb() << "Waiting for connection...";
            std::unique_ptr<Medium> accepted = main_listener->Accept();
            if (!g_tunnels.main_running)
            {
                Verb() << "Service stopped. Exitting.";
                break;
            }
            Verb() << "Connection accepted. Connecting to the relay...";

            // Now call the target address.
            std::unique_ptr<Medium> caller = Medium::Create(call_node, chunk, Medium::CALLER);
            caller->Connect();

            Verb() << "Connected. Establishing pipe.";

            // No exception, we are free to pass :)
            g_tunnels.install(move(accepted), move(caller));
        }
        catch (...)
        {
            Verb() << "Connection reported, but failed";
        }
    }

    g_tunnels.stop_cleaner();

    return 0;
}


