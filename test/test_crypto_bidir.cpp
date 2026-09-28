/*
 * Tests for the separation of the encryption keys used in both directions
 * of an HSv5 connection.
 *
 * Both directions used to share the same SEK and salt, and the IV depends only
 * on the salt and the packet sequence number. With the same ISN in both
 * directions, packets with the same sequence number were encrypted with the
 * same keystream (C_A XOR C_B == P_A XOR P_B).
 */

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "test_env.h"

#ifdef SRT_ENABLE_ENCRYPTION

#include "crypto.h"
#include "handshake.h"
#include "hcrypt_msg.h"
#include "socketconfig.h"
#include "api.h"

using namespace srt;

namespace
{

const std::string PASSPHRASE = "bidirectional-keys";
const unsigned PEER_SRT_VERSION = SrtVersion(1, 5, 5);

void configureCrypto(CCryptoControl& w_cc, CSrtConfig& w_cfg, int cryptomode)
{
    memset(&w_cfg.CryptoSecret, 0, sizeof(w_cfg.CryptoSecret));
    w_cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    w_cfg.CryptoSecret.len = PASSPHRASE.size();
    memcpy((w_cfg.CryptoSecret.str), PASSPHRASE.c_str(), PASSPHRASE.size());
    w_cfg.iCryptoMode = cryptomode;

    w_cc.setCryptoSecret(w_cfg.CryptoSecret);
    w_cc.setCryptoKeylen(16);
}

// KM as it is passed to processSrtMsg_KMREQ/KMRSP: 32-bit words in host order
// (the KM buffer itself is in network order).
std::vector<uint32_t> kmToWire(const unsigned char* km, size_t len)
{
    std::vector<uint32_t> words(len / sizeof(uint32_t));
    NtoHLA(words.data(), reinterpret_cast<const uint32_t*>(km), words.size());
    return words;
}

std::vector<unsigned char> copyKm(const CCryptoControl& cc, size_t ki)
{
    return std::vector<unsigned char>(cc.getKmMsg_data(ki), cc.getKmMsg_data(ki) + cc.getKmMsg_size(ki));
}

std::unique_ptr<CPacket> makeDataPacket(int seqno, int kflg, size_t len)
{
    std::unique_ptr<CPacket> pkt(new CPacket);
    pkt->allocate(1500);
    pkt->set_seqno(seqno);
    pkt->set_msgflags(1 | PacketBoundaryBits(PB_SOLO) | MSGNO_ENCKEYSPEC::wrap(kflg));
    pkt->set_timestamp(1000);
    std::iota(pkt->data(), pkt->data() + len, 'A');
    pkt->setLength(len);
    return pkt;
}

class CryptoBidirKeys : public ::testing::TestWithParam<int>
{
protected:
    CCryptoControl m_initiator {1};
    CCryptoControl m_responder {2};
    CSrtConfig m_cfg;

    void SetUp() override
    {
        if (GetParam() == CSrtConfig::CIPHER_MODE_AES_GCM && !HaiCrypt_IsAESGCM_Supported())
            GTEST_SKIP() << "The crypto service provider does not support AES GCM.";

        configureCrypto(m_initiator, m_cfg, GetParam());
        configureCrypto(m_responder, m_cfg, GetParam());
        ASSERT_TRUE(m_initiator.init(HSD_INITIATOR, m_cfg, true, false));
        ASSERT_TRUE(m_responder.init(HSD_RESPONDER, m_cfg, true, false));

        // Both peers declare SRT_OPT_SECDIST unless a test says otherwise.
        m_initiator.setPeerSecDist(true);
        m_responder.setPeerSecDist(true);
    }

    // Pass the KMREQ to the responder, returns the KMRSP payload (network order bytes).
    std::vector<unsigned char> responderProcess(std::vector<unsigned char> kmreq)
    {
        std::vector<uint32_t> req = kmToWire(kmreq.data(), kmreq.size());
        uint32_t out[SRTDATA_MAXSIZE];
        size_t outlen = SRTDATA_MAXSIZE;
        EXPECT_EQ(m_responder.processSrtMsg_KMREQ(req.data(), kmreq.size(), CUDT::HS_VERSION_SRT1, PEER_SRT_VERSION, out, outlen),
                  SRT_CMD_KMRSP);
        const unsigned char* p = reinterpret_cast<const unsigned char*>(out);
        return std::vector<unsigned char>(p, p + outlen * sizeof(uint32_t));
    }

    int initiatorProcess(const std::vector<unsigned char>& kmrsp)
    {
        std::vector<uint32_t> rsp = kmToWire(kmrsp.data(), kmrsp.size());
        return m_initiator.processSrtMsg_KMRSP(rsp.data(), kmrsp.size(), PEER_SRT_VERSION, true);
    }

    // Encrypt the same plaintext with the same seqno in both directions.
    // Returns true if the ciphertexts are identical (keystream reuse).
    bool sameCiphertext(size_t len = 1316)
    {
        const int seqno = 12345;
        std::unique_ptr<CPacket> a = makeDataPacket(seqno, m_initiator.getSndCryptoFlags(), len);
        std::unique_ptr<CPacket> b = makeDataPacket(seqno, m_responder.getSndCryptoFlags(), len);
        EXPECT_EQ(m_initiator.encrypt(*a), ENCS_CLEAR);
        EXPECT_EQ(m_responder.encrypt(*b), ENCS_CLEAR);
        return a->getLength() == b->getLength() && memcmp(a->data(), b->data(), len) == 0;
    }

    // Each side must decrypt what the other side sends.
    void checkCrossDecryption(size_t len = 1316)
    {
        std::unique_ptr<CPacket> ref = makeDataPacket(7, 0, len);

        std::unique_ptr<CPacket> a = makeDataPacket(7, m_initiator.getSndCryptoFlags(), len);
        ASSERT_EQ(m_initiator.encrypt(*a), ENCS_CLEAR);
        ASSERT_EQ(m_responder.decrypt(*a), ENCS_CLEAR);
        ASSERT_EQ(a->getLength(), len);
        EXPECT_EQ(memcmp(a->data(), ref->data(), len), 0);

        std::unique_ptr<CPacket> b = makeDataPacket(7, m_responder.getSndCryptoFlags(), len);
        ASSERT_EQ(m_responder.encrypt(*b), ENCS_CLEAR);
        ASSERT_EQ(m_initiator.decrypt(*b), ENCS_CLEAR);
        ASSERT_EQ(b->getLength(), len);
        EXPECT_EQ(memcmp(b->data(), ref->data(), len), 0);
    }
};

} // namespace

// The capability is declared in the HSREQ/HSRSP flags; the KM format is unchanged.
TEST_P(CryptoBidirKeys, CapabilityDeclared)
{
    EXPECT_TRUE(IsSet(SrtVersionCapabilities(), SRT_OPT_SECDIST));

    const std::vector<unsigned char> km = copyKm(m_initiator, 0);
    ASSERT_GT(km.size(), size_t(HCRYPT_MSG_KM_OFS_SALT));
    EXPECT_EQ(km[HCRYPT_MSG_KM_OFS_RESV2], 0);
    EXPECT_EQ(km[HCRYPT_MSG_KM_OFS_RESV2 + 1], 0);
}

// Both peers support independent keys: the responder returns its own KM.
TEST_P(CryptoBidirKeys, IndependentKeys)
{
    const std::vector<unsigned char> kmreq = copyKm(m_initiator, 0);
    const std::vector<unsigned char> kmrsp = responderProcess(kmreq);

    ASSERT_GT(kmrsp.size(), sizeof(uint32_t));
    EXPECT_NE(kmrsp, kmreq) << "KMRSP must carry the responder's own KM, not an echo";
    EXPECT_NE(memcmp(&kmrsp[HCRYPT_MSG_KM_OFS_SALT], &kmreq[HCRYPT_MSG_KM_OFS_SALT], HAICRYPT_SALT_SZ), 0)
        << "Responder's salt must be freshly generated";
    EXPECT_TRUE(m_responder.hasIndependentKeys());
    EXPECT_FALSE(m_responder.isSndDataGated());
    EXPECT_EQ(m_responder.m_SndKmState, SRT_KM_S_SECURED);
    EXPECT_EQ(m_responder.m_RcvKmState, SRT_KM_S_SECURED);

    // The KMRSP of a repeated handshake must be the same.
    EXPECT_EQ(responderProcess(kmreq), kmrsp);

    EXPECT_EQ(initiatorProcess(kmrsp), 1);
    EXPECT_TRUE(m_initiator.hasIndependentKeys());
    EXPECT_FALSE(m_initiator.isSndDataGated());
    EXPECT_EQ(m_initiator.m_SndKmState, SRT_KM_S_SECURED);
    EXPECT_EQ(m_initiator.m_RcvKmState, SRT_KM_S_SECURED);

    // Repeated KMRSP is harmless.
    EXPECT_EQ(initiatorProcess(kmrsp), 1);

    EXPECT_FALSE(sameCiphertext()) << "Both directions produce the same keystream";
    checkCrossDecryption();
}

// Initiator without SRT_OPT_SECDIST (older version): the responder clones the key
// (legacy behavior) and must not send data until it has switched to its own key.
TEST_P(CryptoBidirKeys, OlderInitiator)
{
    m_responder.setPeerSecDist(false);
    const std::vector<unsigned char> kmreq = copyKm(m_initiator, 0);

    const std::vector<unsigned char> kmrsp = responderProcess(kmreq);
    EXPECT_EQ(kmrsp, kmreq) << "Legacy KMRSP must be an echo";
    EXPECT_FALSE(m_responder.hasIndependentKeys());
    EXPECT_TRUE(m_responder.isSndDataGated());
    EXPECT_EQ(m_responder.m_SndKmState, SRT_KM_S_SECURED);

    // This is why sending must be held: the keys are shared.
    EXPECT_TRUE(sameCiphertext());
}

// Responder without SRT_OPT_SECDIST (older version): the KMRSP is an echo and the
// initiator must not send data until it has switched to its own key.
TEST_P(CryptoBidirKeys, OlderResponder)
{
    m_initiator.setPeerSecDist(false);
    const std::vector<unsigned char> kmreq = copyKm(m_initiator, 0);
    EXPECT_EQ(initiatorProcess(kmreq), 1);
    EXPECT_FALSE(m_initiator.hasIndependentKeys());
    EXPECT_TRUE(m_initiator.isSndDataGated());
    EXPECT_EQ(m_initiator.m_SndKmState, SRT_KM_S_SECURED);
}

// A peer declaring SRT_OPT_SECDIST whose KM is wrapped with another passphrase must be rejected.
TEST_P(CryptoBidirKeys, IndependentKeysBadSecret)
{
    CCryptoControl other {3};
    CSrtConfig cfg;
    configureCrypto(other, cfg, GetParam());
    memcpy(cfg.CryptoSecret.str, "another-passphrase", 18);
    cfg.CryptoSecret.len = 18;
    other.setCryptoSecret(cfg.CryptoSecret);
    ASSERT_TRUE(other.init(HSD_INITIATOR, cfg, true, false));

    // Well-formed KM, but not wrapped with our passphrase.
    const std::vector<unsigned char> foreign = copyKm(other, 0);
    EXPECT_EQ(initiatorProcess(foreign), -1);
    EXPECT_EQ(m_initiator.m_SndKmState, SRT_KM_S_BADSECRET);
    EXPECT_FALSE(m_initiator.hasIndependentKeys());
}

// A KMRSP that is not an echo must be rejected if the peer did not declare SRT_OPT_SECDIST.
TEST_P(CryptoBidirKeys, OwnKmWithoutSecDist)
{
    const std::vector<unsigned char> kmrsp = responderProcess(copyKm(m_initiator, 0));
    ASSERT_GT(kmrsp.size(), sizeof(uint32_t));

    m_initiator.setPeerSecDist(false);
    EXPECT_EQ(initiatorProcess(kmrsp), -1);
    EXPECT_EQ(m_initiator.m_SndKmState, SRT_KM_S_BADSECRET);
    EXPECT_FALSE(m_initiator.hasIndependentKeys());
}

INSTANTIATE_TEST_SUITE_P(CipherMode, CryptoBidirKeys,
        ::testing::Values(int(CSrtConfig::CIPHER_MODE_AES_CTR), int(CSrtConfig::CIPHER_MODE_AES_GCM)),
        [](const ::testing::TestParamInfo<int>& pinfo) {
            return pinfo.param == CSrtConfig::CIPHER_MODE_AES_GCM ? "GCM" : "CTR";
        });

// Forced key refresh primitives in haicrypt.
TEST(CryptoBidirHaicrypt, ForceRefreshAndSwitch)
{
    HaiCrypt_Cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.flags = HAICRYPT_CFG_F_CRYPTO | HAICRYPT_CFG_F_TX;
    cfg.xport = HAICRYPT_XPT_SRT;
    cfg.cryspr = HaiCryptCryspr_Get_Instance();
    cfg.key_len = 16;
    cfg.data_max_len = HAICRYPT_DEF_DATA_MAX_LENGTH;
    cfg.km_refresh_rate_pkt = HAICRYPT_DEF_KM_REFRESH_RATE;
    cfg.km_pre_announce_pkt = HAICRYPT_DEF_KM_PRE_ANNOUNCE;
    cfg.secret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    cfg.secret.len = PASSPHRASE.size();
    memcpy(cfg.secret.str, PASSPHRASE.c_str(), PASSPHRASE.size());

    HaiCrypt_Handle tx = NULL;
    ASSERT_EQ(HaiCrypt_Create(&cfg, &tx), HAICRYPT_OK);

    void* out_p[2];
    size_t out_len[2];
    ASSERT_EQ(HaiCrypt_Tx_ManageKeys(tx, out_p, out_len, 2), 1); // Initial KM
    const std::vector<unsigned char> km0((unsigned char*)out_p[0], (unsigned char*)out_p[0] + out_len[0]);

    EXPECT_EQ(HaiCrypt_Tx_GetKeyFlags(tx), int(EK_EVEN));
    EXPECT_EQ(HaiCrypt_Tx_ForceSwitch(tx), -1) << "No pending key";

    ASSERT_EQ(HaiCrypt_Tx_ForceRefresh(tx, out_p, out_len, 2), 1) << "Only the new KM must be emitted";
    const std::vector<unsigned char> km1((unsigned char*)out_p[0], (unsigned char*)out_p[0] + out_len[0]);
    EXPECT_TRUE(hcryptMsg_KM_HasBothSek(km1.data()));
    EXPECT_EQ(memcmp(&km1[HCRYPT_MSG_KM_OFS_SALT], &km0[HCRYPT_MSG_KM_OFS_SALT], HAICRYPT_SALT_SZ), 0)
        << "Refresh keeps the salt";
    EXPECT_EQ(HaiCrypt_Tx_GetKeyFlags(tx), int(EK_EVEN)) << "Refresh must not switch";
    EXPECT_EQ(HaiCrypt_Tx_ForceRefresh(tx, out_p, out_len, 2), -1) << "Refresh already in progress";

    ASSERT_EQ(HaiCrypt_Tx_ForceSwitch(tx), 0);
    EXPECT_EQ(HaiCrypt_Tx_GetKeyFlags(tx), int(EK_ODD));

    // Regular key management must not switch back.
    HaiCrypt_Tx_ManageKeys(tx, out_p, out_len, 2);
    EXPECT_EQ(HaiCrypt_Tx_GetKeyFlags(tx), int(EK_ODD));

    // A receiver fed with both KMs decrypts data sent with the new key.
    cfg.flags = HAICRYPT_CFG_F_CRYPTO;
    HaiCrypt_Handle rx = NULL;
    ASSERT_EQ(HaiCrypt_Create(&cfg, &rx), HAICRYPT_OK);
    ASSERT_GE(HaiCrypt_Rx_Process(rx, const_cast<unsigned char*>(km0.data()), km0.size(), NULL, NULL, 0), 0);
    ASSERT_GE(HaiCrypt_Rx_Process(rx, const_cast<unsigned char*>(km1.data()), km1.size(), NULL, NULL, 0), 0);

    std::unique_ptr<CPacket> pkt = makeDataPacket(100, HaiCrypt_Tx_GetKeyFlags(tx), 1000);
    std::unique_ptr<CPacket> ref = makeDataPacket(100, 0, 1000);
    ASSERT_GE(HaiCrypt_Tx_Data(tx, (uint8_t*)pkt->getHeader(), (uint8_t*)pkt->m_pcData, pkt->getLength()), 0);
    EXPECT_NE(memcmp(pkt->data(), ref->data(), 1000), 0);
    ASSERT_EQ(HaiCrypt_Rx_Data(rx, (uint8_t*)pkt->getHeader(), (uint8_t*)pkt->m_pcData, pkt->getLength()), 1000);
    EXPECT_EQ(memcmp(pkt->data(), ref->data(), 1000), 0);

    HaiCrypt_Close(rx);
    HaiCrypt_Close(tx);
}

#ifndef _WIN32

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

// UDP relay between peer A and peer B, recording the data packets in both
// directions. It can optionally clear SRT_OPT_SECDIST in the HSREQ/HSRSP
// flags during the handshake, to emulate older versions.
class UdpRelay
{
public:
    struct DataPacket
    {
        int kflg;
        std::vector<unsigned char> payload;
    };
    typedef std::map<int, DataPacket> Capture; // seqno -> first occurrence

    UdpRelay(bool hide_capability)
        : m_hide(hide_capability)
        , m_stop(false)
    {
        m_sock[0] = openSocket(m_addr[0]);
        m_sock[1] = openSocket(m_addr[1]);
    }

    ~UdpRelay()
    {
        stop();
        ::close(m_sock[0]);
        ::close(m_sock[1]);
    }

    // Address that peer A must send to (index 0), and peer B (index 1).
    const sockaddr_in& relayAddr(int i) const { return m_addr[i]; }

    void start(const sockaddr_in& peer_a, const sockaddr_in& peer_b)
    {
        m_peer[0] = peer_a;
        m_peer[1] = peer_b;
        m_thread = std::thread(&UdpRelay::run, this);
    }

    void stop()
    {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
    }

    Capture captured(int dir)
    {
        std::lock_guard<std::mutex> lk(m_lock);
        return m_capture[dir];
    }

    int hsExtensionsPatched()
    {
        std::lock_guard<std::mutex> lk(m_lock);
        return m_hsext_patched;
    }

private:
    static int openSocket(sockaddr_in& w_addr)
    {
        const int s = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sa.sin_port = 0;
        if (s == -1 || ::bind(s, (sockaddr*)&sa, sizeof sa) == -1)
            throw std::runtime_error("relay: bind failed");
        socklen_t len = sizeof w_addr;
        ::getsockname(s, (sockaddr*)&w_addr, &len);
        return s;
    }

    // Clear SRT_OPT_SECDIST in the HSREQ/HSRSP extensions of a handshake.
    void patchHandshake(unsigned char* buf, size_t len)
    {
        const size_t HDR = 16, HS = 48;
        if (len <= HDR + HS)
            return;
        size_t off = HDR + HS;
        while (off + 4 <= len)
        {
            uint32_t w;
            memcpy(&w, buf + off, 4);
            w = ntohl(w);
            const int cmd = w >> 16;
            const size_t size = (w & 0xFFFF) * 4;
            off += 4;
            if (off + size > len)
                break;
            if ((cmd == SRT_CMD_HSREQ || cmd == SRT_CMD_HSRSP) && size >= SRT_HS_E_SIZE * 4)
            {
                uint32_t flags;
                memcpy(&flags, buf + off + SRT_HS_FLAGS * 4, 4);
                flags = htonl(ntohl(flags) & ~uint32_t(SRT_OPT_SECDIST));
                memcpy(buf + off + SRT_HS_FLAGS * 4, &flags, 4);
                std::lock_guard<std::mutex> lk(m_lock);
                ++m_hsext_patched;
            }
            off += size;
        }
    }

    void record(int dir, const unsigned char* buf, size_t len)
    {
        if (len <= 16)
            return;
        uint32_t w0, w1;
        memcpy(&w0, buf, 4);
        memcpy(&w1, buf + 4, 4);
        w0 = ntohl(w0);
        w1 = ntohl(w1);
        const int seqno = w0 & 0x7FFFFFFF;
        DataPacket dp;
        dp.kflg = MSGNO_ENCKEYSPEC::unwrap(w1);
        dp.payload.assign(buf + 16, buf + len);
        std::lock_guard<std::mutex> lk(m_lock);
        m_capture[dir].insert(std::make_pair(seqno, dp));
    }

    void run()
    {
        unsigned char buf[2048];
        while (!m_stop)
        {
            pollfd fds[2] = { { m_sock[0], POLLIN, 0 }, { m_sock[1], POLLIN, 0 } };
            if (::poll(fds, 2, 50) <= 0)
                continue;
            for (int i = 0; i < 2; ++i)
            {
                if (!(fds[i].revents & POLLIN))
                    continue;
                const ssize_t n = ::recv(m_sock[i], buf, sizeof buf, 0);
                if (n <= 0)
                    continue;
                const bool control = (buf[0] & 0x80) != 0;
                if (control)
                {
                    uint32_t w0;
                    memcpy(&w0, buf, 4);
                    const int type = (ntohl(w0) >> 16) & 0x7FFF;
                    if (type == 0 && m_hide) // Handshake
                        patchHandshake(buf, n);
                }
                else
                {
                    record(i, buf, n);
                }
                // Received from peer i, forward to the other peer through the other socket.
                const int o = 1 - i;
                ::sendto(m_sock[o], buf, n, 0, (const sockaddr*)&m_peer[o], sizeof m_peer[o]);
            }
        }
    }

    const bool m_hide;
    std::atomic<bool> m_stop;
    int m_sock[2];
    sockaddr_in m_addr[2];
    sockaddr_in m_peer[2];
    std::thread m_thread;
    std::mutex m_lock;
    Capture m_capture[2];
    int m_hsext_patched = 0;
};

enum ConnMode { CM_CALLER_LISTENER, CM_RENDEZVOUS };

struct BidirParam
{
    ConnMode mode;
    bool hide_capability;
};

class CryptoBidirTransmission : public ::testing::TestWithParam<BidirParam>
{
protected:
    srt::TestInit m_srtinit;

    static SRTSOCKET createSocket(bool rendezvous)
    {
        const SRTSOCKET s = srt_create_socket();
        EXPECT_NE(s, SRT_INVALID_SOCK);
        const int yes = 1;
        const int timeo = 3000;
        EXPECT_NE(srt_setsockflag(s, SRTO_PASSPHRASE, PASSPHRASE.c_str(), (int)PASSPHRASE.size()), SRT_ERROR);
        EXPECT_NE(srt_setsockflag(s, SRTO_RCVTIMEO, &timeo, sizeof timeo), SRT_ERROR);
        EXPECT_NE(srt_setsockflag(s, SRTO_SNDTIMEO, &timeo, sizeof timeo), SRT_ERROR);
        EXPECT_NE(srt_setsockflag(s, SRTO_ENFORCEDENCRYPTION, &yes, sizeof yes), SRT_ERROR);
        if (rendezvous)
        {
            EXPECT_NE(srt_setsockflag(s, SRTO_RENDEZVOUS, &yes, sizeof yes), SRT_ERROR);
        }
        return s;
    }

    static sockaddr_in bindLoopback(SRTSOCKET s)
    {
        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        EXPECT_NE(srt_bind(s, (sockaddr*)&sa, sizeof sa), SRT_ERROR);
        int len = sizeof sa;
        EXPECT_NE(srt_getsockname(s, (sockaddr*)&sa, &len), SRT_ERROR);
        return sa;
    }
};

std::string paramName(const ::testing::TestParamInfo<BidirParam>& info)
{
    std::string n = info.param.mode == CM_RENDEZVOUS ? "Rendezvous" : "CallerListener";
    n += info.param.hide_capability ? "_OlderPeers" : "_NewPeers";
    return n;
}

} // namespace

// Exchange data in both directions through the relay and verify that no two
// packets with the same sequence number share the same keystream.
TEST_P(CryptoBidirTransmission, NoKeystreamReuse)
{
    const BidirParam param = GetParam();
    const bool rdv = param.mode == CM_RENDEZVOUS;

    UdpRelay relay(param.hide_capability);

    SRTSOCKET a = createSocket(rdv);
    SRTSOCKET b = createSocket(rdv);
    const sockaddr_in addr_a = bindLoopback(a);
    const sockaddr_in addr_b = bindLoopback(b);
    relay.start(addr_a, addr_b);

    SRTSOCKET peer_a = a, peer_b = SRT_INVALID_SOCK;
    if (rdv)
    {
        // Each peer connects to its side of the relay.
        std::thread tb([&] {
            const sockaddr_in r = relay.relayAddr(1);
            EXPECT_NE(srt_connect(b, (const sockaddr*)&r, sizeof r), SRT_ERROR) << srt_getlasterror_str();
        });
        const sockaddr_in r = relay.relayAddr(0);
        EXPECT_NE(srt_connect(a, (const sockaddr*)&r, sizeof r), SRT_ERROR) << srt_getlasterror_str();
        tb.join();
        peer_b = b;
    }
    else
    {
        ASSERT_NE(srt_listen(b, 1), SRT_ERROR);
        const sockaddr_in r = relay.relayAddr(0);
        ASSERT_NE(srt_connect(a, (const sockaddr*)&r, sizeof r), SRT_ERROR) << srt_getlasterror_str();
        sockaddr_in sa;
        int salen = sizeof sa;
        peer_b = srt_accept(b, (sockaddr*)&sa, &salen);
        ASSERT_NE(peer_b, SRT_INVALID_SOCK) << srt_getlasterror_str();
    }

    ASSERT_EQ(srt_getsockstate(peer_a), SRTS_CONNECTED);
    ASSERT_EQ(srt_getsockstate(peer_b), SRTS_CONNECTED);
    if (param.hide_capability)
    {
        ASSERT_GE(relay.hsExtensionsPatched(), 2) << "The relay did not see the HSREQ/HSRSP exchange";
    }

    // Same plaintext in both directions, so that the same keystream would give the same ciphertext.
    const int NPKT = 100;
    std::vector<char> msg(1316);
    std::iota(msg.begin(), msg.end(), 'a');

    auto sender = [&](SRTSOCKET s) {
        for (int i = 0; i < NPKT; ++i)
            ASSERT_EQ(srt_sendmsg(s, msg.data(), (int)msg.size(), -1, 1), (int)msg.size()) << srt_getlasterror_str();
    };
    auto receiver = [&](SRTSOCKET s, int* w_count) {
        std::vector<char> buf(1500);
        for (int i = 0; i < NPKT; ++i)
        {
            const int n = srt_recvmsg(s, buf.data(), (int)buf.size());
            if (n <= 0)
                break;
            if (n == (int)msg.size() && std::equal(msg.begin(), msg.end(), buf.begin()))
                ++*w_count;
        }
    };

    int recv_a = 0, recv_b = 0;
    std::thread ra(receiver, peer_a, &recv_a);
    std::thread rb(receiver, peer_b, &recv_b);
    std::thread sa(sender, peer_a);
    std::thread sb(sender, peer_b);
    sa.join();
    sb.join();
    ra.join();
    rb.join();

    EXPECT_EQ(recv_a, NPKT) << "B -> A data lost or corrupted";
    EXPECT_EQ(recv_b, NPKT) << "A -> B data lost or corrupted";

    srt_close(peer_a);
    srt_close(peer_b);
    if (!rdv)
        srt_close(b);
    relay.stop();

    const UdpRelay::Capture ab = relay.captured(0);
    const UdpRelay::Capture ba = relay.captured(1);
    ASSERT_GE(ab.size(), size_t(NPKT));
    ASSERT_GE(ba.size(), size_t(NPKT));

    int overlap = 0, identical = 0;
    for (UdpRelay::Capture::const_iterator i = ab.begin(); i != ab.end(); ++i)
    {
        UdpRelay::Capture::const_iterator j = ba.find(i->first);
        if (j == ba.end())
            continue;
        ++overlap;
        if (i->second.payload == j->second.payload)
            ++identical;
    }
    EXPECT_EQ(identical, 0) << "Keystream reused for " << identical << " of " << overlap << " sequence numbers";

    // Caller-listener: both directions start at the same ISN, so the check above is meaningful.
    if (!rdv)
    {
        EXPECT_GE(overlap, NPKT);
    }

    // When the peer lacks the capability, no data may be sent with the initial (shared) even key.
    // Otherwise the keys are distinct from the handshake on, so no refresh is done.
    const int expected_kflg = param.hide_capability ? int(EK_ODD) : int(EK_EVEN);
    for (UdpRelay::Capture::const_iterator i = ab.begin(); i != ab.end(); ++i)
        EXPECT_EQ(i->second.kflg, expected_kflg) << "A -> B seqno " << i->first;
    for (UdpRelay::Capture::const_iterator i = ba.begin(); i != ba.end(); ++i)
        EXPECT_EQ(i->second.kflg, expected_kflg) << "B -> A seqno " << i->first;
}

INSTANTIATE_TEST_SUITE_P(Modes, CryptoBidirTransmission,
        ::testing::Values(
            BidirParam { CM_CALLER_LISTENER, false },
            BidirParam { CM_CALLER_LISTENER, true },
            BidirParam { CM_RENDEZVOUS, false },
            BidirParam { CM_RENDEZVOUS, true }),
        paramName);

#endif // !_WIN32

#endif // SRT_ENABLE_ENCRYPTION
