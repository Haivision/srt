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

#include "transmitbase.hpp"
#include <udt.h> // Needs access to CUDTException

using namespace std;

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
protected:

    bool m_output_direction = false; //< Defines which of SND or RCV option variant should be used, also to set SRT_SENDER for output
    int m_timeout = 0; //< enforces using SRTO_SNDTIMEO or SRTO_RCVTIMEO, depending on @a m_output_direction
    bool m_tsbpdmode = true;
    int m_outgoing_port = 0;
    string m_mode;
    string m_adapter;
    map<string, string> m_options; // All other options, as provided in the URI
    SRTSOCKET m_sock = SRT_INVALID_SOCK;
    SRTSOCKET m_bindsock = SRT_INVALID_SOCK;
    bool IsUsable() { SRT_SOCKSTATUS st = srt_getsockstate(m_sock); return st > SRTS_INIT && st < SRTS_BROKEN; }
    bool IsBroken() { return srt_getsockstate(m_sock) > SRTS_CONNECTED; }

public:
    void InitParameters(string host, map<string,string> par);
    void PrepareListener(string host, int port, int backlog);
    void StealFrom(SrtCommon& src);
    bool AcceptNewClient();

    SRTSOCKET Socket() const { return m_sock; }
    SRTSOCKET Listener() const { return m_bindsock; }

    virtual void Close();

protected:

    void Error(string src);
    void Init(string host, int port, map<string,string> par, bool dir_output);

    virtual int ConfigurePost(SRTSOCKET sock);
    virtual int ConfigurePre(SRTSOCKET sock);

    void OpenClient(string host, int port);
    void PrepareClient();
    void SetupAdapter(const std::string& host, int port);
    void ConnectClient(string host, int port);

    void OpenServer(string host, int port)
    {
        PrepareListener(host, port, 1);
    }

    void OpenRendezvous(string adapter, string host, int port);

    virtual ~SrtCommon();
};


class SrtSource: public Source, public SrtCommon
{
    std::string hostport_copy;
public:

    SrtSource(std::string host, int port, const std::map<std::string,std::string>& par);
    SrtSource()
    {
        // Do nothing - create just to prepare for use
    }

    int Read(size_t chunk, MediaPacket& pkt, ostream& out_stats = cout) override;

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

    SRTSOCKET GetSRTSocket() const override
    { 
        SRTSOCKET socket = SrtCommon::Socket();
        if (socket == SRT_INVALID_SOCK)
            socket = SrtCommon::Listener();
        return socket;
    }

    bool AcceptNewClient() override { return SrtCommon::AcceptNewClient(); }
};

class SrtTarget: public Target, public SrtCommon
{
public:

    SrtTarget(std::string host, int port, const std::map<std::string,std::string>& par)
    {
        Init(host, port, par, true);
    }

    SrtTarget() {}

    int ConfigurePre(SRTSOCKET sock) override;
    int Write(const char* data, size_t size, int64_t src_time, ostream &out_stats = cout) override;
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

    SRTSOCKET GetSRTSocket() const override
    { 
        SRTSOCKET socket = SrtCommon::Socket();
        if (socket == SRT_INVALID_SOCK)
            socket = SrtCommon::Listener();
        return socket;
    }
    bool AcceptNewClient() override { return SrtCommon::AcceptNewClient(); }
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
    string m_host;
    int m_port = 0;


    SrtModel(string host, int port, map<string,string> par);
    void Establish(std::string& name);

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
