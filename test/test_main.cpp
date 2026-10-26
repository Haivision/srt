#include <string>
#include <iterator>
#include <vector>
#include <sstream>
#include <thread>
#include <fstream>
#include <set>
#include <ctime>
#include <cstring>

#include "gtest/gtest.h"
#include "test_env.h"

#include "srt.h"
#include "netinet_any.h"
#include "core.h"
#include "api.h"
#include "queue.h"
#include "channel.h"
#include "buffer_snd.h"
#include "logging_api.h"
#include "logger_fas.h"

using namespace std;

namespace srt
{
    void SetupLogging();
}

int main(int argc, char **argv)
{
    string command_line_arg(argc == 2 ? argv[1] : "");
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new srt::TestEnv(argc, argv));

    // Set up global logging options (same set of options as srt-live-transmit),
    // once for the whole test run, before any test starts calling srt_startup().
    srt::SetupLogging();

    return RUN_ALL_TESTS();
}

namespace srt
{

TestEnv* TestEnv::me = 0;

void TestEnv::FillArgMap()
{
    // The rule is:
    // - first arguments go to an empty string key
    // - if an argument has - in the beginning, name the key
    // - key followed by args collected in a list
    // - double dash prevents interpreting further args as option keys

    string key;
    bool expectkey = true;

    argmap[""];

    for (auto& a: args)
    {
        if (a.size() > 1)
        {
            if (expectkey && a[0] == '-')
            {
                if (a[1] == '-')
                    expectkey = false;
                else if (a[1] == '/')
                    key = "";
                else
                {
                    key = a.substr(1);
                    argmap[key]; // Make sure it exists even empty
                }

                continue;
            }
        }
        argmap[key].push_back(a);
    }

    return;
}

std::string TestEnv::OptionValue(const std::string& key)
{
    std::ostringstream out;

    auto it = argmap.find(key);
    if (it != argmap.end() && !it->second.empty())
    {
        auto iv = it->second.begin();
        out << (*iv);
        while (++iv != it->second.end())
        {
            out << " " << (*iv);
        }
    }

    return out.str();
}

// Log handler used with the '-loginternal' option: same format as srt-live-transmit's.
extern "C" void TestLogHandler(void* opaque, int level, const char* file, int line, const char* area, const char* message)
{
    std::string prefix;
    if (opaque)
    {
        const char* instr = (const char*)opaque;
        size_t len = strlen(instr);
        if (len > 10)
            len = 10;
        prefix = ":" + string(instr, len);
    }

    time_t now;
    time(&now);
    char timebuf[64];
    strftime((timebuf), sizeof timebuf, "%c", localtime(&now));

    cerr << "[" << timebuf << " " << file << ":" << line
        << "(" << area << ")]{" << level << "} " << prefix << message
        << endl;
}

// Sets up the same set of logging options that srt-live-transmit supports:
// -loglevel (-ll), -logfa (-lfa), -loginternal, -logfile.
// This is done once, globally, for the whole test run (as opposed to
// HandlePerTestOptions, which reruns per test).
void SetupLogging()
{
    TestEnv& env = *TestEnv::me;

    string loglevel_opt = env.OptionPresent("loglevel") ? "loglevel"
                        : env.OptionPresent("ll") ? "ll" : "";
    if (!loglevel_opt.empty())
    {
        srt_setloglevel(hvu::logging::parse_level(env.OptionValue(loglevel_opt)));
    }

    string logfa_opt = env.OptionPresent("logfa") ? "logfa"
                      : env.OptionPresent("lfa") ? "lfa" : "";
    if (!logfa_opt.empty())
    {
        set<string> unknown_fas;
        set<int> fas = hvu::logging::parse_fa(srt::logging::logger_config(), env.OptionValue(logfa_opt), &unknown_fas);
        if (!fas.empty())
        {
            srt::resetlogfa(fas);
        }
        for (auto& fa: unknown_fas)
        {
            cerr << "WARNING: unrecognized log FA: " << fa << endl;
        }
    }

    bool log_internal = env.OptionPresent("loginternal");
    string logfile = env.OptionValue("logfile");

    // The stream must outlive the whole test run.
    static std::ofstream logfile_stream;

    if (log_internal)
    {
        srt_setlogflags(0
            | HVU_LOGF_DISABLE_TIME
            | HVU_LOGF_DISABLE_SEVERITY
            | HVU_LOGF_DISABLE_THREADNAME
            | HVU_LOGF_DISABLE_EOL
        );
        srt_setloghandler(nullptr, TestLogHandler);
    }
    else if (!logfile.empty())
    {
        logfile_stream.open(logfile.c_str());
        if (!logfile_stream)
        {
            cerr << "ERROR: Can't open '" << logfile << "' for writing - fallback to cerr" << endl;
        }
        else
        {
            srt::setlogstream(logfile_stream);
        }
    }
}

// Specific functions
bool TestEnv::Allowed_IPv6()
{
    if (TestEnv::me->OptionPresent("disable-ipv6"))
    {
        std::cout << "TEST: IPv6 testing disabled, FORCED PASS\n";
        return false;
    }
    return true;
}


void TestInit::start(int& w_retstatus)
{
    ASSERT_GE(w_retstatus = (int)srt_startup(), 0);
}

void TestInit::stop()
{
    if (TestEnv::me->OptionPresent("check-close"))
    {
        std::string remaining_sockets = CUDT::uglobal().testSocketsClear();
        // Give it one more chance to be deleted by GC
        if (remaining_sockets.size())
        {
            std::cout << "TestInit::stop: waiting extra 1.5s to ensure closed sockets\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            remaining_sockets = CUDT::uglobal().testSocketsClear();
        }
        EXPECT_TRUE(remaining_sockets.empty()) << "STILL NOT CLOSED: " << remaining_sockets;
    }
    EXPECT_NE((int)srt_cleanup(), -1);
}

// This function finds some interesting options among command
// line arguments and does specific things.
// NOTE: the more complete set of srt-live-transmit-like logging options
// (-loglevel/-ll, -logfa/-lfa, -loginternal, -logfile) is handled once,
// globally, in SetupLogging() (called from main()), not here.
void TestInit::HandlePerTestOptions()
{
    // As a short example:
    // use '-logdebug' option to turn on debug logging.

    if (TestEnv::me->OptionPresent("logdebug"))
    {
        srt_setloglevel(LOG_DEBUG);
    }

    if (TestEnv::me->OptionPresent("lognote"))
    {
        srt_setloglevel(LOG_NOTICE);
    }
}

// Copied from ../apps/apputil.cpp, can't really link this file here.
sockaddr_any CreateAddr(const std::string& name, unsigned short port, int pref_family)
{
    using namespace std;

    // Handle empty name.
    // If family is specified, empty string resolves to ANY of that family.
    // If not, it resolves to IPv4 ANY (to specify IPv6 any, use [::]).
    if (name == "")
    {
        sockaddr_any result(pref_family == AF_INET6 ? pref_family : AF_INET);
        result.hport(port);
        return result;
    }

    bool first6 = pref_family != AF_INET;
    int families[2] = {AF_INET6, AF_INET};
    if (!first6)
    {
        families[0] = AF_INET;
        families[1] = AF_INET6;
    }

    for (int i = 0; i < 2; ++i)
    {
        int family = families[i];
        sockaddr_any result (family);

        // Try to resolve the name by pton first
        if (inet_pton(family, name.c_str(), result.get_addr()) == 1)
        {
            result.hport(port); // same addr location in ipv4 and ipv6
            return result;
        }
    }

    // If not, try to resolve by getaddrinfo
    // This time, use the exact value of pref_family

    sockaddr_any result;
    addrinfo fo = {
        0,
        pref_family,
        0, 0,
        0, 0,
        NULL, NULL
    };

    addrinfo* val = nullptr;
    int erc = getaddrinfo(name.c_str(), nullptr, &fo, &val);
    if (erc == 0)
    {
        result.set(val->ai_addr);
        result.len = result.size();
        result.hport(port); // same addr location in ipv4 and ipv6
    }
    freeaddrinfo(val);

    return result;
}

UniqueSocket::~UniqueSocket()
{
    // Could be closed explicitly
    if (sock != -1)
        close();
}

void UniqueSocket::close()
{
    int close_result = srt_close(sock);
    int close_error = srt_getlasterror(nullptr);

    // XXX SRT_EINVSOCK is reported when the socket
    // has been already wiped out, which may happen to a broken socket.
    // This isn't exactly intended, although trying to close a nonexistent
    // socket is not a problem, as long as it happens before the id value rollover
    // (that is, when it's closed immediately after getting broken).
    // This solution is still slick though and should be fixed.
    //
    // Restore this, when fixed
    // EXPECT_NE(srt_close(sock), SRT_ERROR) << lab << " CREATED: "<< f << ":" << l;
    if (close_result == SRT_ERROR)
    {
        EXPECT_NE(close_error, SRT_EINVSOCK) << lab << " CREATED: "<< f << ":" << l;
    }
    else
    {
        EXPECT_EQ(close_result, 0) << lab << " CREATED: "<< f << ":" << l;
    }
    sock = -1;
}

bool TestMockCUDT::checkApplyFilterConfig(const string& s)
{
    return core->checkApplyFilterConfig(s);
}

bool TestMockCUDT::processSrtMsg(const srt::CPacket *ctrlpkt)
{
    return core->processSrtMsg(ctrlpkt);
}

int TestMockCUDT::rcvKmState()
{
    return core->m_CryptoControl.m_RcvKmState;
}

CUDTSocket* TestMockCUDT::locateSocket(int32_t s)
{
    SRTSOCKET sock (s);
    return CUDT::uglobal().locateSocket(sock);
}

bool TestMockCUDT::setSocket(int32_t sock)
{
    CUDTSocket* s = locateSocket(sock);
    if (!s)
        return false;
    core = &s->core();
    return true;
}

void TestMockCUDT::processCtrlAck(const CPacket& pkt, const sync::steady_clock::time_point& t) { core->processCtrlAck(pkt, t); }
int TestMockCUDT::flowWindowSize() const { return core->m_iFlowWindowSize; }
int64_t TestMockCUDT::lastSendSrctime() const { return core->m_iLastSndSrctime; }
void TestMockCUDT::setFlowWindowSize(int v) { core->m_iFlowWindowSize = v; }
void TestMockCUDT::setSendWindows(int flow_window, int congestion_window)
{
    core->m_iFlowWindowSize = flow_window;
    core->m_iCongestionWindow = congestion_window;
}

bool TestMockCUDT::initialKeyRefreshPending() const { return !core->isSendReady(); }
#ifdef SRT_ENABLE_ENCRYPTION
bool TestMockCUDT::prepareInitialKeyRefresh(CMultiplexer& mux, CPacket& previous_response)
{
    sync::ScopedLock lock(core->m_ConnectionLock);
    core->m_config.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_CTR;
    core->m_CryptoControl.setCryptoSecret(core->m_config.CryptoSecret);
    if (!core->m_CryptoControl.initialized()
            && !core->m_CryptoControl.init(core->m_SocketID, HSD_INITIATOR, core->m_config, false))
        return false;

    const CCryptoControl::KmMessage* initial = core->m_CryptoControl.getKmMsg(0);
    uint32_t data[SRTDATA_MAXSIZE];
    NtoHLA(data, reinterpret_cast<const uint32_t*>(initial->bytedata()), initial->bytesize() / sizeof(uint32_t));
    previous_response.allocate(initial->bytesize());
    memcpy(previous_response.m_pcData, data, initial->bytesize());
    previous_response.setLength(initial->bytesize());
    previous_response.setExtendedType(SRT_CMD_KMRSP);
    if (core->m_CryptoControl.processSrtMsg_KMRSP(
            data, initial->bytesize(), SrtVersion(1, 6, 0), true) != 1)
        return false;

    mux.m_pChannel = new CChannel;
    mux.m_pChannel->setConfig(CSrtMuxerConfig());
    mux.m_pChannel->open(CreateAddr("127.0.0.1", 0, AF_INET));
    core->m_pMuxer = &mux;
    core->m_PeerAddr = mux.m_pChannel->getSockAddr();
    core->m_uPeerSrtVersion = SrtVersion(1, 6, 0);
    core->m_config.uSenderMode = 1;
    core->m_TransferIPVersion = AF_INET;
    core->m_iMaxDataPayloadSize = 1456;
    core->m_pSndBuffer = new CSndBuffer(32 * 1024, 32, 1500,
            CPacket::HDR_SIZE + CPacket::udpHeaderSize(AF_INET), 0, 10);
    core->m_iISN = core->m_iSndNextSeqNo = core->m_iSndLastAck = 4321;
    core->m_iSndCurrSeqNo = 4320;
    core->m_stats.tsStartTime = sync::steady_clock::now() - sync::seconds_from(1);
    if (core->setupCC() != SRT_REJ_UNKNOWN)
        return false;
    setSendWindows(10, 10);
    core->m_bOpened = true;
    core->m_State = CUDT::SSS_CONNECTED;
    return initialKeyRefreshPending();
}

bool TestMockCUDT::startInitialKeyRefresh(CPacket& response)
{
    core->checkSndTimers();
    const CCryptoControl::KmMessage* refreshed = core->m_CryptoControl.getKmMsg(1);
    if (refreshed->bytesize() == 0)
        return false;
    response.allocate(refreshed->bytesize());
    NtoHLA(reinterpret_cast<uint32_t*>(response.m_pcData),
           reinterpret_cast<const uint32_t*>(refreshed->bytedata()), refreshed->bytesize() / sizeof(uint32_t));
    response.setLength(refreshed->bytesize());
    response.setExtendedType(SRT_CMD_KMRSP);
    // Make the KM retransmit due without sleeping for the default RTT.
    const int retries = refreshed->iPeerRetry;
    core->m_iSRTT = 0;
    core->checkSndTimers();
    return initialKeyRefreshPending() && retries > 0 && refreshed->iPeerRetry == retries - 1;
}

int TestMockCUDT::sendScheduledData(const char* data, int length, SRT_MSGCTRL& control)
{
    return core->sendmsg2(data, length, control);
}

bool TestMockCUDT::packScheduledData(int32_t seqno, CSndPacket& packet)
{
    CNetworkInterface source;
    return core->packData(SchedPacket(core->m_parent, seqno, sched::TP_REGULAR), packet, source) == CUDT::PACK_OK;
}

void TestMockCUDT::resetScheduledCrypto()
{
    core->m_State = CUDT::SSS_INIT;
    core->m_bOpened = false;
    core->m_pMuxer = NULL;
}

SendScheduler& TestMockCUDT::sendScheduler(CMultiplexer& mux) { return mux.m_SndQueue.m_Scheduler; }
#endif

}
