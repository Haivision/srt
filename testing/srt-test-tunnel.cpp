
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

class Medium
{
    UriParser uri;
    size_t chunk;

public:

    Medium(UriParser u, size_t ch): uri(u), chunk(ch) {}

    virtual bool IsOpen() = 0;
    virtual void Close() {}
    virtual bool End() = 0;

    enum ReadStatus
    {
        RD_DATA, RD_AGAIN, RD_EOF, RD_ERROR
    };

    enum Mode
    {
        LISTENER, CALLER
    };

    virtual ReadStatus Read(ref_t<bytevector> output) = 0;
    virtual void Write(ref_t<bytevector> portion) = 0;
    virtual void CreateListener(UriParser uri) = 0;
    virtual void CreateCaller(UriParser uri) = 0;

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

    virtual void addToEpoll(int eid) = 0;

    virtual std::pair<std::unique_ptr<Medium>,
        std::unique_ptr<Medium>> Establish() = 0;

protected:
    virtual void Init(const UriParser& uri) = 0;
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
            Error(errno, "Error while reading");
        }
    }

    master->decommission();
}

class SrtMedium: public Medium
{
    SRTSOCKET m_socket;

    bool m_listener = false;
    bool m_open = false;
    bool m_eof = false;
    bool m_broken = false;
public:

    bool IsOpen() override { return m_open; }
    bool End() override { return m_eof; }
    bool Broken() override { return m_broken; }

    virtual bytevector Read(size_t chunk) override;
    virtual void Write(ref_t<bytevector> portion) override;
    virtual void CreateListener(UriParser uri) override;
    virtual void CreateCaller(UriParser uri) override;

    virtual int addToEpoll(int eid, int modes) override
    {
        return srt_epoll_add_usock(eid, m_socket, modes);
    }

    virtual std::pair<std::unique_ptr<Medium>,
        std::unique_ptr<Medium>> Establish() = 0;
protected:
    virtual void Init(const UriParser& uri) override;

};

class TcpMedium: public Medium
{
    int m_socket;

    bool m_listener = false;
    bool m_open = false;
    bool m_eof = false;
    bool m_broken = false;
public:

    bool IsOpen() override { return m_open; }
    bool End() override { return m_eof; }
    bool Broken() override { return m_broken; }

    virtual bytevector Read(size_t chunk) override;
    virtual void Write(ref_t<bytevector> portion) override;
    virtual void CreateListener(UriParser uri) override;
    virtual void CreateCaller(UriParser uri) override;

    virtual int addToEpoll(int eid, int modes) override
    {
        return srt_epoll_add_ssock(eid, m_socket, modes);
    }

    virtual std::pair<std::unique_ptr<Medium>,
        std::unique_ptr<Medium>> Establish() = 0;

protected:
    virtual void Init(const UriParser& uri) override;

};

void SrtMedium::CreateListener(UriParser uri)
{
    m_socket = srt_create_socket();

    ConfigurePre(m_socket);

    sockaddr_in sa = CreateAddrInet(uri.host(), uri.hport());

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

void TcpMedium::CreateListener(UriParser uri)
{
    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ConfigurePre(m_socket);

    sockaddr_in sa = CreateAddrInet(uri.host(), uri.hport());

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

static std::unique_ptr<Medium> Medium::Create(const std::string& url, Medium::Mode mode)
{
    std::unique_ptr<Medium> out;
    if (url.scheme() == "srt")
    {
        out = new SrtMedium;
    }
    else if (url.scheme() == "tcp")
    {
        out = new TcpMedium;
    }
    else
    {
        Error("Medium not supported");
    }

    out->Init(url, mode);
}

struct Tunnelbox
{
    list<Tunnel> tunnels;
    mutex access;

    void install(size_t chunk, std::unique_ptr<Medium>&& acp, std::unique_ptr<Medium>&& clr)
    {
        unique_lock lk(access);
        tunnels.push_back(Tunnel(this, chunk, move(acp), move(clr)));
        Tunnel& it = tunnels.back();
        it.Start();
    }

    void decommission(Tunnel* tnl)
    {
        // Instead of putting the iterator into Tunnel,
        // simply search for the address
        unique_lock lk(access);

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
    master->decommission();
}

int main( int argc, char** argv )
{
    size_t chunk = 4096; // XXX option-configurable!

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

    UriParser us(listen_node), ut(call_node);

    // It is allowed to use both media of the same type,
    // but only srt and tcp are allowed.

    set<string> allowed = {"srt", "tcp"};
    if (!allowed.count(listen_node.scheme())|| !allowed.count(call_node.scheme()))
    {
        cerr << "ERROR: only tcp and srt schemes supported";
        return -1;
    }

    Verb() << "SOURCE type=" << us.scheme() << ", TARGET type=" << ut.scheme();

    std::unique_ptr<Medium> main_listener = Medium::Create(listen_node, Medium::LISTENER);

    // The main program loop is only to catch
    // new connections and manage them. Also takes care
    // of the broken connections.

    for (;;)
    {
        std::unique_ptr<Medium> accepted = main_listener.Accept();
        Verb() << "Connection accepted.";

        // Now call the target address.
        std::unique_ptr<Medium> caller = Medium::Create(call_node, Medium::CALLER);
        caller.Connect();

        // No exception, we are free to pass :)
        tunnels.install(chunk, move(accepted), move(caller));
    }
}


