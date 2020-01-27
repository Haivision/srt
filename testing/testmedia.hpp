/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC__COMMON_TRANSMITMEDIA_HPP
#define INC__COMMON_TRANSMITMEDIA_HPP

#include <string>
#include <map>
#include <stdexcept>
#include <deque>

#include "testmediabase.hpp"
#include <udt.h> // Needs access to CUDTException

extern srt_listen_callback_fn* transmit_accept_hook_fn;
extern void* transmit_accept_hook_op;

extern std::shared_ptr<SrtStatsWriter> transmit_stats_writer;

using namespace std;

const srt_logging::LogFA SRT_LOGFA_APP = 10;
extern srt_logging::Logger applog;

// Trial version of an exception. Try to implement later an official
// interruption mechanism in SRT using this.

struct TransmissionError: public std::runtime_error
{
    TransmissionError(const std::string& arg):
        std::runtime_error(arg)
    {
    }
};

class SrtCommon
{
    int srt_conn_epoll = -1;

    void SpinWaitAsync();

protected:

    struct Connection
    {
        string host;
        int port;
        SRTSOCKET socket = SRT_INVALID_SOCK;

        Connection(string h, int p): host(h), port(p) {}
    };

    int srt_epoll = -1;
    SRT_EPOLL_T m_direction = SRT_EPOLL_OPT_NONE; //< Defines which of SND or RCV option variant should be used, also to set SRT_SENDER for output
    bool m_blocking_mode = true; //< enforces using SRTO_SNDSYN or SRTO_RCVSYN, depending on @a m_direction
    int m_timeout = 0; //< enforces using SRTO_SNDTIMEO or SRTO_RCVTIMEO, depending on @a m_direction
    bool m_tsbpdmode = true;
    int m_outgoing_port = 0;
    string m_mode;
    string m_adapter;
    map<string, string> m_options; // All other options, as provided in the URI
    vector<Connection> m_group_nodes;
    string m_group_type;
    vector<SRT_SOCKGROUPDATA> m_group_data;
#ifdef SRT_OLD_APP_READER
    int32_t m_group_seqno = -1;

    struct ReadPos
    {
        int32_t sequence;
        bytevector packet;
    };
    map<SRTSOCKET, ReadPos> m_group_positions;
    SRTSOCKET m_group_active; // The link from which the last packet was delivered
#endif

    SRTSOCKET m_sock = SRT_INVALID_SOCK;
    SRTSOCKET m_bindsock = SRT_INVALID_SOCK;
    bool m_listener_group = false;
    bool IsUsable() { SRT_SOCKSTATUS st = srt_getsockstate(m_sock); return st > SRTS_INIT && st < SRTS_BROKEN; }
    bool IsBroken() { return srt_getsockstate(m_sock) > SRTS_CONNECTED; }

    void UpdateGroupStatus(const SRT_SOCKGROUPDATA* grpdata, size_t grpdata_size);

public:
    void InitParameters(string host, string path, map<string,string> par);
    void PrepareListener(string host, int port, int backlog);
    void StealFrom(SrtCommon& src);
    void AcceptNewClient();

    SRTSOCKET Socket() { return m_sock; }
    SRTSOCKET Listener() { return m_bindsock; }

    virtual void Close();

protected:

    void Error(string src, SRT_REJECT_REASON reason = SRT_REJ_UNKNOWN);
    void Init(string host, int port, string path, map<string,string> par, SRT_EPOLL_OPT dir);
    int AddPoller(SRTSOCKET socket, int modes);
    virtual int ConfigurePost(SRTSOCKET sock);
    virtual int ConfigurePre(SRTSOCKET sock);

    void OpenClient(string host, int port);
    void OpenGroupClient();
    void PrepareClient();
    void SetupAdapter(const std::string& host, int port);
    void ConnectClient(string host, int port);
    void SetupRendezvous(string adapter, int port);

    void OpenServer(string host, int port, int backlog = 1)
    {
        PrepareListener(host, port, backlog);
        if (transmit_accept_hook_fn)
        {
            srt_listen_callback(m_bindsock, transmit_accept_hook_fn, transmit_accept_hook_op);
        }
        AcceptNewClient();
    }

    void OpenRendezvous(string adapter, string host, int port)
    {
        PrepareClient();
        SetupRendezvous(adapter, port);
        ConnectClient(host, port);
    }

    virtual ~SrtCommon();
};


class SrtSource: public virtual Source, public virtual SrtCommon
{
    std::string hostport_copy;
public:

    SrtSource(std::string host, int port, std::string path, const std::map<std::string,std::string>& par);
    SrtSource()
    {
        // Do nothing - create just to prepare for use
    }

    bytevector Read(size_t chunk) override;
    bytevector GroupRead(size_t chunk);
    bool GroupCheckPacketAhead(bytevector& output);


    /*
       In this form this isn't needed.
       Unblock if any extra settings have to be made.
    virtual int ConfigurePre(UDTSOCKET sock) override
    {
        int result = SrtCommon::ConfigurePre(sock);
        if ( result == -1 )
            return result;
        return 0;
    }
    */

    bool IsOpen() override { return IsUsable(); }
    bool End() override { return IsBroken(); }
    void Close() override { return SrtCommon::Close(); }
};

class SrtTarget: public virtual Target, public virtual SrtCommon
{
public:

    SrtTarget(std::string host, int port, std::string path, const std::map<std::string,std::string>& par);
    SrtTarget() {}

    int ConfigurePre(SRTSOCKET sock) override;
    void Write(const bytevector& data) override;
    bool IsOpen() override { return IsUsable(); }
    bool Broken() override { return IsBroken(); }
    void Close() override { return SrtCommon::Close(); }

    size_t Still() override
    {
        size_t bytes;
        int st = srt_getsndbuffer(m_sock, nullptr, &bytes);
        if (st == -1)
            return 0;
        return bytes;
    }

};

class SrtRelay: public Relay, public SrtSource, public SrtTarget
{
public:
    SrtRelay(std::string host, int port, std::string path, const std::map<std::string,std::string>& par);
    SrtRelay() {}

    int ConfigurePre(SRTSOCKET sock) override
    {
        // This overrides the change introduced in SrtTarget,
        // which sets the SRTO_SENDER flag. For a bidirectional transmission
        // this flag should not be set, as the connection should be checked
        // for being 1.3.0 clients only.
        return SrtCommon::ConfigurePre(sock);
    }

    // Override separately overridden methods by SrtSource and SrtTarget
    bool IsOpen() override { return IsUsable(); }
    void Close() override { return SrtCommon::Close(); }
};


// This class is used when we don't know yet whether the given URI
// designates an effective listener or caller. So we create it, initialize,
// then we know what mode we'll be using.
//
// When caller, then we will do connect() using this object, then clone out
// a new object - of a direction specific class - which will steal the socket
// from this one and then roll the data. After this, this object is ready
// to connect again, and will create its own socket for that occasion, and
// the whole procedure repeats.
//
// When listener, then this object will be doing accept() and with every
// successful acceptation it will clone out a new object - of a direction
// specific class - which will steal just the connection socket from this
// object. This object will still live on and accept new connections and
// so on.
class SrtModel: public SrtCommon
{
public:
    bool is_caller = false;
    bool is_rend = false;
    string m_host;
    int m_port = 0;


    SrtModel(string host, int port, map<string,string> par);
    void Establish(std::string& w_name);

    void Close()
    {
        if (m_sock != SRT_INVALID_SOCK)
        {
            srt_close(m_sock);
            m_sock = SRT_INVALID_SOCK;
        }
    }
};



#endif
