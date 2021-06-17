/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_COMMON_TRANSMITMEDIA_HPP
#define INC_SRT_COMMON_TRANSMITMEDIA_HPP

#include <string>
#include <map>
#include <stdexcept>
#include <deque>
#include <atomic>

#include "apputil.hpp"
#include "testmediabase.hpp"
#include <udt.h> // Needs access to CUDTException
#include <netinet_any.h>

extern srt_listen_callback_fn* transmit_accept_hook_fn;
extern void* transmit_accept_hook_op;
extern std::atomic<bool> transmit_int_state;

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

    friend void TransmitGroupSocketConnect(void* srtcommon, SRTSOCKET sock, int error, const sockaddr* peer, int token);

    struct ConnectionBase
    {
        string host;
        int port;
        int weight = 0;
        SRTSOCKET socket = SRT_INVALID_SOCK;
        sockaddr_any source;
        sockaddr_any target;
        int token = -1;

        ConnectionBase(string h, int p): host(h), port(p), source(AF_INET) {}
    };

    struct Connection: ConnectionBase
    {
#if ENABLE_EXPERIMENTAL_BONDING
        SRT_SOCKOPT_CONFIG* options = nullptr;
#endif
        int error = SRT_SUCCESS;
        int reason = SRT_REJ_UNKNOWN;

        Connection(string h, int p): ConnectionBase(h, p) {}
        Connection(Connection&& old): ConnectionBase(old)
        {
#if ENABLE_EXPERIMENTAL_BONDING
            if (old.options)
            {
                options = old.options;
                old.options = nullptr;
            }
#endif
        }
        ~Connection()
        {
#if ENABLE_EXPERIMENTAL_BONDING
            srt_delete_config(options);
#endif
        }
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
    string m_group_config;
#if ENABLE_EXPERIMENTAL_BONDING
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

    SRTSOCKET Socket() const { return m_sock; }
    SRTSOCKET Listener() const { return m_bindsock; }

    void Acquire(SRTSOCKET s)
    {
        m_sock = s;
        if (s & SRTGROUP_MASK)
            m_listener_group = true;
    }

    virtual void Close();

protected:

    void Error(string src, int reason = SRT_REJ_UNKNOWN, int force_result = 0);
    void Init(string host, int port, string path, map<string,string> par, SRT_EPOLL_OPT dir);
    int AddPoller(SRTSOCKET socket, int modes);
    virtual int ConfigurePost(SRTSOCKET sock);
    virtual int ConfigurePre(SRTSOCKET sock);

    void OpenClient(string host, int port);
#if ENABLE_EXPERIMENTAL_BONDING
    void OpenGroupClient();
#endif
    void PrepareClient();
    void SetupAdapter(const std::string& host, int port);
    void ConnectClient(string host, int port);
    void SetupRendezvous(string adapter, string host, int port);

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
        SetupRendezvous(adapter, host, port);
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

    MediaPacket Read(size_t chunk) override;
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
    void Write(const MediaPacket& data) override;
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

class UdpCommon
{
protected:
    int m_sock = -1;
    sockaddr_any sadr;
    std::string adapter;
    std::map<std::string, std::string> m_options;
    void Setup(std::string host, int port, std::map<std::string,std::string> attr);
    void Error(int err, std::string src);

    ~UdpCommon();
};


class UdpSource: public virtual Source, public virtual UdpCommon
{
    bool eof = true;
public:

    UdpSource(string host, int port, const map<string,string>& attr);

    MediaPacket Read(size_t chunk) override;

    bool IsOpen() override { return m_sock != -1; }
    bool End() override { return eof; }
};

class UdpTarget: public virtual Target, public virtual UdpCommon
{
public:
    UdpTarget(string host, int port, const map<string,string>& attr);

    void Write(const MediaPacket& data) override;
    bool IsOpen() override { return m_sock != -1; }
    bool Broken() override { return false; }
};

class UdpRelay: public Relay, public UdpSource, public UdpTarget
{
public:
    UdpRelay(string host, int port, const map<string,string>& attr):
        UdpSource(host, port, attr),
        UdpTarget(host, port, attr)
    {
    }

    bool IsOpen() override { return m_sock != -1; }
};



#endif
