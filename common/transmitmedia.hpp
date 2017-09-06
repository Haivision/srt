#ifndef INC__COMMON_TRANSMITMEDIA_HPP
#define INC__COMMON_TRANSMITMEDIA_HPP

#include <string>
#include <map>

#include "transmitbase.hpp"

using namespace std;

class SrtCommon
{
    int srt_conn_epoll = -1;

    void SpinWaitAsync();

protected:

    bool m_output_direction = false; //< Defines which of SND or RCV option variant should be used, also to set SRT_SENDER for output
    bool m_blocking_mode = true; //< enforces using SRTO_SNDSYN or SRTO_RCVSYN, depending on @a m_output_direction
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
    void AcceptNewClient();

    SRTSOCKET Socket() { return m_sock; }
    SRTSOCKET Listener() { return m_bindsock; }

    virtual void Close();

protected:

    void Error(UDT::ERRORINFO& udtError, string src);
    void Init(string host, int port, map<string,string> par, bool dir_output);
    int AddPoller(SRTSOCKET socket, int modes);
    virtual int ConfigurePost(SRTSOCKET sock);
    virtual int ConfigurePre(SRTSOCKET sock);

    void OpenClient(string host, int port);
    void PrepareClient();
    void SetupAdapter(const std::string& host, int port);
    void ConnectClient(string host, int port);

    void OpenServer(string host, int port)
    {
        PrepareListener(host, port, 1);
        AcceptNewClient();
    }

    void OpenRendezvous(string adapter, string host, int port);

    virtual ~SrtCommon();
};


class SrtSource: public Source, public SrtCommon
{
    int srt_epoll = -1;
    std::string hostport_copy;
public:

    SrtSource(std::string host, int port, const std::map<std::string,std::string>& par);
    SrtSource()
    {
        // Do nothing - create just to prepare for use
    }

    bytevector Read(size_t chunk) override;

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

class SrtTarget: public Target, public SrtCommon
{
    int srt_epoll = -1;
public:

    SrtTarget(std::string host, int port, const std::map<std::string,std::string>& par)
    {
        Init(host, port, par, true);

        if ( !m_blocking_mode )
        {
            srt_epoll = AddPoller(m_sock, SRT_EPOLL_OUT);
        }

    }

    SrtTarget() {}

    int ConfigurePre(SRTSOCKET sock) override;
    void Write(const bytevector& data) override;
    bool IsOpen() override { return IsUsable(); }
    bool Broken() override { return IsBroken(); }
    void Close() override { return SrtCommon::Close(); }

};

#endif
