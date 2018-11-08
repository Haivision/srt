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
#include <deque>
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
#include <api.h>

using namespace std;

class Medium
{
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
public:

    string uri() { return m_uri.uri(); }

    Medium(UriParser u, size_t ch): m_uri(u), m_chunk(ch) {}
    Medium() {}

    virtual bool IsOpen() = 0;
    virtual void Close() {}
    virtual bool End() = 0;

    virtual int ReadInternal(char* output, int size) = 0;
    virtual bool IsErrorAgain() = 0;

    ReadStatus Read(ref_t<bytevector> output);
    virtual void Write(ref_t<bytevector> portion) = 0;

    virtual void CreateListener() = 0;
    virtual void CreateCaller() = 0;
    virtual unique_ptr<Medium> Accept() = 0;
    virtual void Connect() = 0;

    static std::unique_ptr<Medium> Create(const std::string& url, Mode);

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
    size_t chunk;

    enum { DIR_IN, DIR_OUT };
    Medium* media[2];
    std::thread thr;
    class Tunnel* master;

    int status = 0;
    Medium::ReadStatus rdst = Medium::RD_ERROR;
    UDT::ERRORINFO srtx;

public:

    Engine(Tunnel* p, size_t ch, Medium* m1, Medium* m2)
        : chunk(ch), media {m1, m2}, master(p)
    {
    }

    void Start()
    {
        Verb() << "START: " << media[DIR_IN]->uri() << " --> " << media[DIR_OUT]->uri();
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


class Tunnelbox;

class Tunnel
{
    Tunnelbox* master;
    size_t chunk;
    std::unique_ptr<Medium> med_acp, med_clr;
    Engine acp_to_clr, clr_to_acp;

public:

    string show()
    {
        return med_acp->uri() + " <-> " + med_clr->uri();
    }

    Tunnel(Tunnelbox* m, size_t ch, std::unique_ptr<Medium>&& acp, std::unique_ptr<Medium>&& clr):
        master(m),
        chunk(ch),
        med_acp(move(acp)), med_clr(move(clr)),
        acp_to_clr(this, ch, acp.get(), clr.get()),
        clr_to_acp(this, ch, clr.get(), acp.get())
    {
    }

    void Start()
    {
        acp_to_clr.Start();
        clr_to_acp.Start();
    }

    void decommission();
};

void Engine::Worker()
{
    bytevector outbuf;

    for (;;)
    {
        rdst = media[DIR_IN]->Read(Ref(outbuf));
        switch (rdst)
        {
        case Medium::RD_DATA:
            {
                // We get the data, write them to the output
                media[DIR_OUT]->Write(Ref(outbuf));
            }
            break;

        case Medium::RD_EOF:
            throw Medium::ReadEOF("");

        case Medium::RD_AGAIN:
        case Medium::RD_ERROR:
            status = -1;
            Medium::Error("Error while reading");
        }
    }

    master->decommission();
}

class SrtMedium: public Medium
{
    SRTSOCKET m_socket = SRT_ERROR;
public:

    bool IsOpen() override { return m_open; }
    bool End() override { return m_eof; }
    bool Broken() override { return m_broken; }

    virtual int ReadInternal(char* output, int size) override;
    virtual bool IsErrorAgain() override;

    virtual void Write(ref_t<bytevector> portion) override;
    virtual void CreateListener() override;
    virtual void CreateCaller() override;
    virtual unique_ptr<Medium> Accept() override;
    virtual void Connect() override;

protected:
    virtual void Init() override;

    void ConfigurePre(SRTSOCKET socket);
    void ConfigurePost(SRTSOCKET socket);

    using Medium::Error;

    static void Error(UDT::ERRORINFO& ri, const string& text)
    {
        throw TransmissionError("ERROR: " + text + ": " + ri.getErrorMessage());
    }
};

class TcpMedium: public Medium
{
    int m_socket = -1;
public:

    bool IsOpen() override { return m_open; }
    bool End() override { return m_eof; }
    bool Broken() override { return m_broken; }

    virtual int ReadInternal(char* output, int size) override;
    virtual bool IsErrorAgain() override;
    virtual void Write(ref_t<bytevector> portion) override;
    virtual void CreateListener() override;
    virtual void CreateCaller() override;
    virtual unique_ptr<Medium> Accept() override;
    virtual void Connect() override;

protected:

    // Just models. No options are predicted for now.
    void ConfigurePre(int )
    {
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

void SrtMedium::ConfigurePre(SRTSOCKET so)
{
    vector<string> fails;
    SrtConfigurePre(so, "--", m_options, &fails);
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

    ConfigurePre(m_socket);

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
    ConfigurePre(m_socket);

    sockaddr_in sa = CreateAddrInet(m_uri.host(), m_uri.portno());

    int stat = bind(m_socket, (sockaddr*)&sa, sizeof sa);

    if (stat == -1)
    {
        close(m_socket);
        Error(errno, "bind");
    }

    stat = listen(m_socket, backlog);
    if ( stat == -1 )
    {
        close(m_socket);
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

    Verb() << "accepted a connection from " << SockaddrToString((sockaddr*)&sa);

    ConfigurePost(s);

    SrtMedium* m = new SrtMedium;
    m->m_socket = s;
    m->m_chunk = m_chunk;

    unique_ptr<Medium> med(m);
    return move(med);
}

unique_ptr<Medium> TcpMedium::Accept()
{
    sockaddr_in sa;
    int salen = sizeof sa;
    int s = ::accept(m_socket, (sockaddr*)&sa, &salen);
    if (s == -1)
    {
        Error(errno, "accept");
    }

    Verb() << "accepted a connection from " << SockaddrToString((sockaddr*)&sa);

    ConfigurePost(s);

    TcpMedium* m = new TcpMedium;
    m->m_socket = s;
    m->m_chunk = m_chunk;

    unique_ptr<Medium> med(m);
    return move(med);
}

void SrtMedium::CreateCaller()
{
    m_socket = srt_create_socket();
    ConfigurePre(m_socket);

    // XXX setting up outgoing port not supported
}

void TcpMedium::CreateCaller()
{
    m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ConfigurePre(m_socket);
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

int SrtMedium::ReadInternal(char* buffer, int size)
{
    int st = srt_recv(m_socket, buffer, size);
    if (st == SRT_ERROR)
        return -1;
    return st;
}

int TcpMedium::ReadInternal(char* buffer, int size)
{
    return read(m_socket, buffer, size);
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
Medium::ReadStatus Medium::Read(ref_t<bytevector> r_output)
{
    bytevector& output = *r_output;

    // Don't read, but fake that you read
    if (output.size() > m_chunk)
    {
        Verb() << "BUFFER EXCEEDED";
        return RD_DATA;
    }

    // Resize to maximum first
    size_t shift = output.size();
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

    output.resize(pred_size);
    int st = ReadInternal(output.data() + shift, m_chunk);
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
            output.resize(shift);
            return RD_DATA;
        }
        output.clear();
        return RD_EOF;
    }

    output.resize(shift+st);
    return RD_DATA;
}

void SrtMedium::Write(ref_t<bytevector> r_buffer)
{
    bytevector& buffer = *r_buffer;

    int st = srt_send(m_socket, buffer.data(), buffer.size());
    if (st == SRT_ERROR)
    {
        Error(UDT::getlasterror(), "srt_send");
    }

    // This should be ==, whereas > is not possible, but
    // this should simply embrace this case as a sanity check.
    if (st >= int(buffer.size()))
        buffer.clear();
    else if (st == 0)
    {
        Error("Unexpected EOF on Write");
    }
    else
    {
        // Remove only those bytes that were sent
        buffer.erase(buffer.begin(), buffer.begin()+st);
    }
}

void TcpMedium::Write(ref_t<bytevector> r_buffer)
{
    bytevector& buffer = *r_buffer;

    int st = ::write(m_socket, buffer.data(), buffer.size());
    if (st == -1)
    {
        Error(errno, "send");
    }

    // This should be ==, whereas > is not possible, but
    // this should simply embrace this case as a sanity check.
    if (st >= int(buffer.size()))
        buffer.clear();
    else if (st == 0)
    {
        Error("Unexpected EOF on Write");
    }
    else
    {
        // Remove only those bytes that were sent
        buffer.erase(buffer.begin(), buffer.begin()+st);
    }
}

std::unique_ptr<Medium> Medium::Create(const std::string& url, Medium::Mode mode)
{
    UriParser uri(url);
    std::unique_ptr<Medium> out;
    if (uri.scheme() == "srt")
    {
        out.reset(new SrtMedium);
    }
    else if (uri.scheme() == "tcp")
    {
        out.reset(new TcpMedium);
    }
    else
    {
        Error("Medium not supported");
    }

    out->m_uri = uri;
    out->InitMode(mode);

    return move(out);
}

struct Tunnelbox
{
    list<Tunnel> tunnels;
    mutex access;

    void install(size_t chunk, std::unique_ptr<Medium>&& acp, std::unique_ptr<Medium>&& clr)
    {
        lock_guard<mutex> lk(access);
        tunnels.push_back(Tunnel(this, chunk, move(acp), move(clr)));
        Tunnel& it = tunnels.back();

        Verb() << "Tunnelbox: Starting tunnel: " << acp->uri() << " <-> " << clr->uri();
        it.Start();
    }

    void decommission(Tunnel* tnl)
    {
        // Instead of putting the iterator into Tunnel,
        // simply search for the address
        lock_guard<mutex> lk(access);

        Verb() << "Tunnelbox: decommissioning: " << tnl->show();

        for (auto i = tunnels.begin(); i != tunnels.end(); ++i)
        {
            if (&*i == tnl)
            {
                tunnels.erase(i);
                break;
            }
        }
    }
};

void Tunnel::decommission()
{
    acp_to_clr.Stop();
    clr_to_acp.Stop();

    // Threads stopped. Now you can delete yourself.
    master->decommission(this);
}

Tunnelbox g_tunnels;

size_t default_chunk = 4096;

const logging::LogFA SRT_LOGFA_APP = 10;

int main( int argc, char** argv )
{
    size_t chunk = default_chunk;

    set<string>
        o_loglevel = { "ll", "loglevel" },
        o_chunk = {"c", "chunk" },
        o_verbose = {"v", "verbose" },
        o_noflush = {"s", "skipflush" };

    // Options that expect no arguments (ARG_NONE) need not be mentioned.
    vector<OptionScheme> optargs = {
        { o_loglevel, OptionScheme::ARG_ONE },
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
    logging::LogLevel::type lev = SrtParseLogLevel(loglevel);
    UDT::setloglevel(lev);
    UDT::addlogfa(SRT_LOGFA_APP);

    string verbo = Option<OutString>(params, "no", o_verbose);
    if ( verbo == "" || !false_names.count(verbo) )
        Verbose::on = true;

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

    std::unique_ptr<Medium> main_listener = Medium::Create(listen_node, Medium::LISTENER);

    // The main program loop is only to catch
    // new connections and manage them. Also takes care
    // of the broken connections.

    for (;;)
    {
        try
        {
            Verb() << "Waiting for connection...";
            std::unique_ptr<Medium> accepted = main_listener->Accept();
            Verb() << "Connection accepted. Connecting to the relay...";

            // Now call the target address.
            std::unique_ptr<Medium> caller = Medium::Create(call_node, Medium::CALLER);
            caller->Connect();

            Verb() << "Connected. Establishing pipe.";

            // No exception, we are free to pass :)
            g_tunnels.install(chunk, move(accepted), move(caller));
        }
        catch (...)
        {
            Verb() << "Connection reported, but failed";
        }
    }
}


