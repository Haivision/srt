/*
 * Behavioural tests for PR #3384 (SRT_OPT_SECDIST - per-direction IV distinction).
 *
 * Only SRT_ENABLE_ENCRYPTION is required (AES-CTR). Unlike the Crypto/CryptoCtr
 * fixtures in test_crypto.cpp these are NOT behind ENABLE_AEAD_API_PREVIEW, so
 * they run in the default CI configuration.
 *
 *  - SecDist.*    : two CCryptoControl objects wired together exactly the way
 *                   CUDT wires them (HSv5 conclusion KMX, then runtime UMSG_EXT
 *                   KMX with HS_VERSION_UDT4, as in CUDT::processSrtMsg()).
 *  - SecDistE2E.* : real sockets over loopback, forcing a key refresh through
 *                   SRTO_KMREFRESHRATE. Public API only, so this part also
 *                   builds unchanged against pre-PR releases for comparison.
 */

#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "test_env.h"

#ifdef SRT_ENABLE_ENCRYPTION

#include "crypto.h"
#include "core.h"
#include "handshake.h"
#include "hcrypt_msg.h"
#include "hcrypt.h"
#include "socketconfig.h"
#include "api.h"
#include "srt.h"

using namespace srt;

namespace
{

const std::string kPwd = "secdist-review-pw";
const size_t      kPld = 1316;
const int         kKeyLen = 32; // AES-256

SrtVersionInfo peerInfo(unsigned int hs_version, uint32_t peer_flags)
{
    // NOTE: 1.5.8 is the earlierst release with this change; if anything
    // is going to change here in the future, might be the version
    // should be specified differently.
    return SrtVersionInfo {hs_version, SrtVersion(1, 5, 8), peer_flags};
}

// CUDT converts every received SRT control payload to host-order words before
// handing it over to CCryptoControl (which converts it back).
std::vector<uint32_t> asReceived(const void* wire, size_t bytelen)
{
    std::vector<uint32_t> w(bytelen / sizeof(uint32_t));
    memcpy(&w[0], wire, w.size() * sizeof(uint32_t));
    NtoHLA(&w[0], &w[0], w.size());
    return w;
}

void configure(CCryptoControl& cc, CSrtConfig& cfg, unsigned km_refresh, unsigned km_preannounce)
{
    memset(&cfg.CryptoSecret, 0, sizeof(cfg.CryptoSecret));
    cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    cfg.CryptoSecret.len = kPwd.size();
    memcpy(cfg.CryptoSecret.str, kPwd.c_str(), kPwd.size());
    cc.setCryptoSecret(cfg.CryptoSecret);
    cfg.iSndCryptoKeyLen = kKeyLen;
    cc.setCryptoKeylen(kKeyLen);
    cfg.iCryptoMode       = CSrtConfig::CIPHER_MODE_AES_CTR;
    cfg.uKmRefreshRatePkt = km_refresh;
    cfg.uKmPreAnnouncePkt = km_preannounce;
}

void fillPattern(char* p, size_t len, uint32_t seed)
{
    uint32_t x = 2166136261u ^ (seed * 2654435761u);
    for (size_t i = 0; i < len; ++i)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        p[i] = char(x >> 24);
    }
}

std::unique_ptr<CPacket> plainPacket(int32_t seq, int kflags, uint32_t seed)
{
    std::unique_ptr<CPacket> p(new CPacket);
    p->allocate(1500);
    p->set_seqno(seq);
    p->set_msgflags(1 | PacketBoundaryBits(PB_SOLO) | MSGNO_ENCKEYSPEC::wrap(kflags));
    p->set_timestamp(356);
    fillPattern(p->data(), kPld, seed);
    p->setLength(kPld);
    return p;
}

bool samePayload(const CPacket& a, const CPacket& b)
{
    return a.getLength() == b.getLength() && memcmp(a.data(), b.data(), a.getLength()) == 0;
}

const SRTSOCKET SOCKID_INITIATOR = 101, SOCKID_RESPONDER = 102;

} // namespace

class SecDist : public srt::Test
{
protected:
    SecDist()
        : m_cryptoInitiator(SOCKID_INITIATOR)
        , m_cryptoResponder(SOCKID_RESPONDER)
        , m_flagsInitiator(0)
        , m_flagsResponder(0)
    {
    }

    void setup() override {}
    void teardown() override {}

    // HSv5 conclusion handshake KMX. ini_adv/rsp_adv are the SRT_HS_FLAGS each side
    // advertises; each CCryptoControl gets its *peer's* flags (CUDT::m_uPeerSrtFlags).
    void handshake(uint32_t ini_adv, uint32_t rsp_adv, unsigned ini_refresh = 0, unsigned ini_pre = 0,
                   unsigned rsp_refresh = 0, unsigned rsp_pre = 0)
    {
        m_flagsInitiator = ini_adv;
        m_flagsResponder = rsp_adv;
        CSrtConfig ci, cr;
        configure(m_cryptoInitiator, ci, ini_refresh, ini_pre);
        configure(m_cryptoResponder, cr, rsp_refresh, rsp_pre);
        ASSERT_TRUE(m_cryptoInitiator.init(HSD_INITIATOR, ci, true, false));
        ASSERT_TRUE(m_cryptoResponder.init(HSD_RESPONDER, cr, true, false));

        const size_t km_len = m_cryptoInitiator.getKmMsg_size(0);
        ASSERT_GT(km_len, 0u);
        std::vector<uint32_t> kmreq = asReceived(m_cryptoInitiator.getKmMsg_data(0), km_len);
        uint32_t kmrsp[SRTDATA_MAXSIZE];
        size_t   kmrsp_words = SRTDATA_MAXSIZE;
        ASSERT_EQ(m_cryptoResponder.processSrtMsg_KMREQ(&kmreq[0], km_len,
                    peerInfo(CUDT::HS_VERSION_SRT1, m_flagsInitiator), (kmrsp), (kmrsp_words)),
                  SRT_CMD_KMRSP);
        ASSERT_GT(kmrsp_words, 1u);
        std::vector<uint32_t> rsp = asReceived(kmrsp, kmrsp_words * sizeof(uint32_t));
        ASSERT_EQ(m_cryptoInitiator.processSrtMsg_KMRSP(&rsp[0], kmrsp_words * sizeof(uint32_t),
                    peerInfo(CUDT::HS_VERSION_SRT1, m_flagsResponder), true),
                  SRT_CMD_ACCEPT);

        ASSERT_EQ(m_cryptoInitiator.m_SndKmState, SRT_KM_S_SECURED);
        ASSERT_EQ(m_cryptoInitiator.m_RcvKmState, SRT_KM_S_SECURED);
        ASSERT_EQ(m_cryptoResponder.m_SndKmState, SRT_KM_S_SECURED);
        ASSERT_EQ(m_cryptoResponder.m_RcvKmState, SRT_KM_S_SECURED);

        for (int ki = 0; ki < 2; ++ki)
        {
            m_sent_km[0][ki].assign(m_cryptoInitiator.getKmMsg_data(ki), m_cryptoInitiator.getKmMsg_data(ki) + m_cryptoInitiator.getKmMsg_size(ki));
            m_sent_km[1][ki].assign(m_cryptoResponder.getKmMsg_data(ki), m_cryptoResponder.getKmMsg_data(ki) + m_cryptoResponder.getKmMsg_size(ki));
        }
    }

    // One data packet as CUDT::packData() does it: encrypt, then checkSndKMRefresh()
    // -> regenCryptoKm(this, false). Any new KM goes to the peer as a runtime KMREQ
    // (CUDT::processSrtMsg: HS_VERSION_UDT4) and the peer's KMRSP comes back the same way.
    int encryptAndRefresh(bool from_ini, CPacket& pkt)
    {
        CCryptoControl& tx      = from_ini ? m_cryptoInitiator : m_cryptoResponder;
        CCryptoControl& rx      = from_ini ? m_cryptoResponder : m_cryptoInitiator;
        const uint32_t  tx_sees = from_ini ? m_flagsResponder : m_flagsInitiator;
        const uint32_t  rx_sees = from_ini ? m_flagsInitiator : m_flagsResponder;

        EXPECT_EQ(tx.encrypt(pkt), ENCS_CLEAR);
        tx.regenCryptoKm(NULL, false);

        int kmx = 0;
        for (int ki = 0; ki < 2; ++ki)
        {
            const unsigned char*        km   = tx.getKmMsg_data(ki);
            const size_t                len  = tx.getKmMsg_size(ki);
            std::vector<unsigned char>& last = m_sent_km[from_ini ? 0 : 1][ki];
            if (len == 0 || (last.size() == len && memcmp(&last[0], km, len) == 0))
                continue;
            last.assign(km, km + len);

            std::vector<uint32_t> req = asReceived(km, len);
            uint32_t out[SRTDATA_MAXSIZE];
            size_t   out_words = SRTDATA_MAXSIZE;
            EXPECT_EQ(rx.processSrtMsg_KMREQ(&req[0], len,
                        peerInfo(CUDT::HS_VERSION_UDT4, rx_sees), out, out_words),
                    SRT_CMD_KMRSP);
            std::vector<uint32_t> rsp = asReceived(out, out_words * sizeof(uint32_t));
            EXPECT_EQ(tx.processSrtMsg_KMRSP(&rsp[0], out_words * sizeof(uint32_t),
                        peerInfo(CUDT::HS_VERSION_UDT4, tx_sees), /*explicitly not a handshake*/ false),
                    SRT_CMD_ACCEPT);
            ++kmx;
        }
        return kmx;
    }

    struct Rotation
    {
        int  kmx;
        bool saw_odd;
        int  bad[2];   // [0]: initiator -> responder, [1]: responder -> initiator
        int  first[2];
    };

    Rotation runTraffic(int npkts)
    {
        Rotation r = {0, false, {0, 0}, {-1, -1}};
        for (int i = 0; i < npkts; ++i)
        {
            for (int dir = 0; dir < 2; ++dir)
            {
                const bool      from_ini = dir == 0;
                CCryptoControl& tx       = from_ini ? m_cryptoInitiator : m_cryptoResponder;
                CCryptoControl& rx       = from_ini ? m_cryptoResponder : m_cryptoInitiator;
                const int       kf       = tx.getSndCryptoFlags();
                r.saw_odd                = r.saw_odd || kf == EK_ODD;

                std::unique_ptr<CPacket> ref = plainPacket(5000 + i, kf, 2 * i + dir);
                std::unique_ptr<CPacket> pkt(ref->clone());
                r.kmx += encryptAndRefresh(from_ini, *pkt);
                if (rx.decrypt(*pkt) != ENCS_CLEAR || !samePayload(*pkt, *ref))
                {
                    if (r.bad[dir]++ == 0)
                        r.first[dir] = i;
                }
            }
        }
        return r;
    }

    CCryptoControl m_cryptoInitiator;
    CCryptoControl m_cryptoResponder;
    uint32_t       m_flagsInitiator;
    uint32_t       m_flagsResponder;
    std::vector<unsigned char> m_sent_km[2][2];
};

// hcrypt_SetIV() with role 0 must be byte-identical to the pre-PR macros
// (hcrypt_SetCtrIV / hcrypt_SetGcmIV), which is what old peers compute.
TEST_F(SecDist, IvRole0MatchesLegacyLayout)
{
    std::mt19937 rng(12345);
    for (int t = 0; t < 20000; ++t)
    {
        const hcrypt_Pki pki = (hcrypt_Pki)rng();
        unsigned char    salt[16];
        for (int i = 0; i < 16; ++i)
            salt[i] = (unsigned char)rng();

        unsigned char legacy_ctr[16] = {0}, legacy_gcm[12] = {0};
        memcpy(&legacy_ctr[10], &pki, 4);
        memcpy(&legacy_gcm[8], &pki, 4);
        for (int i = 0; i < 14; ++i)
            legacy_ctr[i] ^= salt[i];
        for (int i = 0; i < 12; ++i)
            legacy_gcm[i] ^= salt[i];

        unsigned char iv[3][16];
        for (unsigned char role = 0; role < 3; ++role)
            hcrypt_SetIV(iv[role], pki, salt, role, hcrypt_IV_Ctr);
        ASSERT_EQ(0, memcmp(iv[0], legacy_ctr, 16)) << "CTR role 0 differs from legacy at t=" << t;
        EXPECT_NE(iv[1][0], iv[2][0]);
        EXPECT_EQ(0, memcmp(iv[1] + 1, iv[0] + 1, 15));
        EXPECT_EQ(0, memcmp(iv[2] + 1, iv[0] + 1, 15));

        hcrypt_SetIV(iv[0], pki, salt, 0, hcrypt_IV_Gcm);
        ASSERT_EQ(0, memcmp(iv[0], legacy_gcm, 12)) << "GCM role 0 differs from legacy at t=" << t;
    }
}

// Both peers new (SECDIST negotiated): the same (SEK, salt, seq) must not produce the
// same keystream in both directions, and each side must still decrypt the other.
TEST_F(SecDist, NewPeers_DirectionsUseDistinctKeystreams)
{
    ASSERT_NO_FATAL_FAILURE(handshake(SRT_OPT_SECDIST, SRT_OPT_SECDIST));

    std::unique_ptr<CPacket> p_ini = plainPacket(1000, m_cryptoInitiator.getSndCryptoFlags(), 1);
    std::unique_ptr<CPacket> p_rsp = plainPacket(1000, m_cryptoResponder.getSndCryptoFlags(), 2);
    std::unique_ptr<CPacket> c_ini(p_ini->clone()), c_rsp(p_rsp->clone());
    ASSERT_EQ(m_cryptoInitiator.encrypt(*c_ini), ENCS_CLEAR);
    ASSERT_EQ(m_cryptoResponder.encrypt(*c_rsp), ENCS_CLEAR);

    // Two-time-pad check: C1^C2 == P1^P2 only when the keystreams are equal.
    size_t xor_equal = 0;
    for (size_t i = 0; i < kPld; ++i)
        xor_equal += ((c_ini->data()[i] ^ c_rsp->data()[i]) == (p_ini->data()[i] ^ p_rsp->data()[i]));
    EXPECT_LT(xor_equal, kPld / 2) << "keystream reused across directions";

    ASSERT_EQ(m_cryptoResponder.decrypt(*c_ini), ENCS_CLEAR);
    ASSERT_EQ(m_cryptoInitiator.decrypt(*c_rsp), ENCS_CLEAR);
    EXPECT_TRUE(samePayload(*c_ini, *p_ini));
    EXPECT_TRUE(samePayload(*c_rsp, *p_rsp));
}

// Peer without SECDIST (any pre-PR version): wire format must stay exactly as before.
// This also documents the original issue: C1^C2 == P1^P2 for every byte.
TEST_F(SecDist, LegacyPeer_KeepsPreviousWireFormat)
{
    ASSERT_NO_FATAL_FAILURE(handshake(0, 0));

    std::unique_ptr<CPacket> p_ini = plainPacket(1000, m_cryptoInitiator.getSndCryptoFlags(), 1);
    std::unique_ptr<CPacket> p_rsp = plainPacket(1000, m_cryptoResponder.getSndCryptoFlags(), 2);
    std::unique_ptr<CPacket> c_ini(p_ini->clone()), c_rsp(p_rsp->clone());
    ASSERT_EQ(m_cryptoInitiator.encrypt(*c_ini), ENCS_CLEAR);
    ASSERT_EQ(m_cryptoResponder.encrypt(*c_rsp), ENCS_CLEAR);

    size_t xor_equal = 0;
    for (size_t i = 0; i < kPld; ++i)
        xor_equal += ((c_ini->data()[i] ^ c_rsp->data()[i]) == (p_ini->data()[i] ^ p_rsp->data()[i]));
    EXPECT_EQ(xor_equal, kPld) << "legacy mode is expected to reuse the keystream (pre-PR behaviour)";

    ASSERT_EQ(m_cryptoResponder.decrypt(*c_ini), ENCS_CLEAR);
    ASSERT_EQ(m_cryptoInitiator.decrypt(*c_rsp), ENCS_CLEAR);
    EXPECT_TRUE(samePayload(*c_ini, *p_ini));
    EXPECT_TRUE(samePayload(*c_rsp, *p_rsp));
}

// Negative control: AES-CTR has no integrity check, so a role mismatch is NOT reported;
// decrypt() returns ENCS_CLEAR and hands garbage to the application. Only a payload
// comparison (as in the tests above) can detect a broken role assignment.
TEST_F(SecDist, CtrRoleMismatchIsSilent_NegativeControl)
{
    // Asymmetric negotiation: responder applies SECDIST, initiator does not.
    ASSERT_NO_FATAL_FAILURE(handshake(SRT_OPT_SECDIST, 0));

    std::unique_ptr<CPacket> p_rsp = plainPacket(1000, m_cryptoResponder.getSndCryptoFlags(), 2);
    std::unique_ptr<CPacket> c_rsp(p_rsp->clone());
    ASSERT_EQ(m_cryptoResponder.encrypt(*c_rsp), ENCS_CLEAR);
    EXPECT_EQ(m_cryptoInitiator.decrypt(*c_rsp), ENCS_CLEAR);
    EXPECT_FALSE(samePayload(*c_rsp, *p_rsp));
}

// Caller-sender: the INITIATOR rotates its TX key (runtime KMX), both directions must
// stay decryptable.
TEST_F(SecDist, InitiatorKeyRefresh_BothDirectionsDecryptable)
{
    ASSERT_NO_FATAL_FAILURE(handshake(SRT_OPT_SECDIST, SRT_OPT_SECDIST, 64, 16, 0, 0));
    const Rotation r = runTraffic(400);
    EXPECT_GT(r.kmx, 0) << "no runtime KMX happened";
    EXPECT_TRUE(r.saw_odd) << "sender never switched keys";
    EXPECT_EQ(r.bad[0], 0) << "initiator->responder broken from packet " << r.first[0];
    EXPECT_EQ(r.bad[1], 0) << "responder->initiator broken from packet " << r.first[1];
}

// Listener-sender: the RESPONDER rotates its TX key (runtime KMX), both directions must
// stay decryptable.
TEST_F(SecDist, ResponderKeyRefresh_BothDirectionsDecryptable)
{
    ASSERT_NO_FATAL_FAILURE(handshake(SRT_OPT_SECDIST, SRT_OPT_SECDIST, 0, 0, 64, 16));
    const Rotation r = runTraffic(400);
    EXPECT_GT(r.kmx, 0) << "no runtime KMX happened";
    EXPECT_TRUE(r.saw_odd) << "sender never switched keys";
    EXPECT_EQ(r.bad[0], 0) << "initiator->responder broken from packet " << r.first[0];
    EXPECT_EQ(r.bad[1], 0) << "responder->initiator broken from packet " << r.first[1];
}

// Same as above with a legacy peer (no SECDIST): control for the previous test.
TEST_F(SecDist, ResponderKeyRefresh_LegacyPeer_Control)
{
    ASSERT_NO_FATAL_FAILURE(handshake(0, 0, 0, 0, 64, 16));
    const Rotation r = runTraffic(400);
    EXPECT_GT(r.kmx, 0) << "no runtime KMX happened";
    EXPECT_TRUE(r.saw_odd) << "sender never switched keys";
    EXPECT_EQ(r.bad[0], 0) << "initiator->responder broken from packet " << r.first[0];
    EXPECT_EQ(r.bad[1], 0) << "responder->initiator broken from packet " << r.first[1];
}

// ---------------------------------------------------------------------------
// End-to-end over loopback (real CUDT handshake, UMSG_EXT KMX, key switch).
// ---------------------------------------------------------------------------

namespace
{

struct E2EResult
{
    int sent;
    int received;
    int corrupted;
    int first_corrupted;
    int early_corrupted; // among the first 64 packets (before any refresh)
};

void runLoopback(bool listener_sends, int kmrefresh, int npkts, E2EResult& w_res)
{
    w_res.sent = w_res.received = w_res.corrupted = w_res.early_corrupted = 0;
    w_res.first_corrupted = -1;

    SRTSOCKET lsn = srt_create_socket(), clr = srt_create_socket();
    ASSERT_NE(lsn, SRT_INVALID_SOCK);
    ASSERT_NE(clr, SRT_INVALID_SOCK);

    const std::string pw = "e2e-secdist-review-pw";
    const int  latency = 300;
    const bool no      = false;
    for (int i = 0; i < 2; ++i)
    {
        const SRTSOCKET s = i ? clr : lsn;
        ASSERT_EQ(srt_setsockflag(s, SRTO_PASSPHRASE, pw.c_str(), (int)pw.size()), 0);
        ASSERT_EQ(srt_setsockflag(s, SRTO_LATENCY, &latency, sizeof latency), 0);
        ASSERT_EQ(srt_setsockflag(s, SRTO_TLPKTDROP, &no, sizeof no), 0);
        if (kmrefresh > 0)
        {
            ASSERT_EQ(srt_setsockflag(s, SRTO_KMREFRESHRATE, &kmrefresh, sizeof kmrefresh), 0);
        }
    }

    sockaddr_in sa = sockaddr_in();
    sa.sin_family  = AF_INET;
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);
    int bind_res = -1;
    for (int port = 5100; port <= 5555; ++port)
    {
        sa.sin_port = htons(port);
        bind_res    = srt_bind(lsn, (sockaddr*)&sa, sizeof sa);
        if (bind_res == 0)
            break;
    }
    ASSERT_EQ(bind_res, 0);
    ASSERT_EQ(srt_listen(lsn, 1), 0);

    SRTSOCKET acc = SRT_INVALID_SOCK;
    std::thread acceptor([&] {
        sockaddr_storage ss;
        int sl = sizeof ss;
        acc = srt_accept(lsn, (sockaddr*)&ss, &sl);
    });
    const int conn = srt_connect(clr, (sockaddr*)&sa, sizeof sa);
    if (conn == SRT_ERROR)
        srt_close(lsn); // unblocks srt_accept()
    acceptor.join();
    ASSERT_NE(conn, SRT_ERROR) << srt_getlasterror_str();
    ASSERT_NE(acc, SRT_INVALID_SOCK) << srt_getlasterror_str();

    const SRTSOCKET snd = listener_sends ? acc : clr;
    const SRTSOCKET rcv = listener_sends ? clr : acc;
    const int rcvtimeo = 5000;
    ASSERT_EQ(srt_setsockflag(rcv, SRTO_RCVTIMEO, &rcvtimeo, sizeof rcvtimeo), 0);

    std::thread sender([&] {
        std::vector<char> buf(kPld);
        for (int i = 0; i < npkts; ++i)
        {
            fillPattern(&buf[0], kPld, (uint32_t)i);
            if (srt_sendmsg2(snd, &buf[0], (int)kPld, NULL) == (int)kPld)
                ++w_res.sent;
            if (i % 16 == 15)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    std::vector<char> rbuf(1500), expect(kPld);
    for (int i = 0; i < npkts; ++i)
    {
        const int n = srt_recvmsg2(rcv, &rbuf[0], (int)rbuf.size(), NULL);
        if (n <= 0)
            break;
        fillPattern(&expect[0], kPld, (uint32_t)i);
        if (n != (int)kPld || memcmp(&rbuf[0], &expect[0], kPld) != 0)
        {
            if (w_res.corrupted++ == 0)
                w_res.first_corrupted = i;
        }
        ++w_res.received;
    }
    sender.join();

    srt_close(acc);
    srt_close(clr);
    srt_close(lsn);
}

bool bindFree(SRTSOCKET s, sockaddr_in& w_sa, int from_port)
{
    w_sa            = sockaddr_in();
    w_sa.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1", &w_sa.sin_addr) != 1)
        return false;
    for (int port = from_port; port <= 5555; ++port)
    {
        w_sa.sin_port = htons(port);
        if (srt_bind(s, (sockaddr*)&w_sa, sizeof w_sa) == 0)
            return true;
    }
    return false;
}

// Both peers send and receive at the same time. w_res[d] describes direction
// conn[d] -> conn[1-d]. Caller-listener: conn[0] is the caller (INITIATOR) and
// conn[1] the accepted socket (RESPONDER). Rendezvous: roles come from the
// cookie contest, so either direction may be the RESPONDER's.
void runBidir(bool rendezvous, int kmrefresh, int npkts, E2EResult (&w_res)[2])
{
    for (int d = 0; d < 2; ++d)
    {
        w_res[d].sent = w_res[d].received = w_res[d].corrupted = w_res[d].early_corrupted = 0;
        w_res[d].first_corrupted = -1;
    }

    const std::string pw       = "e2e-secdist-review-pw";
    const int         latency  = 300;
    const int         rcvtimeo = 5000;
    const bool        no = false, yes = true;

    SRTSOCKET a = srt_create_socket(), b = srt_create_socket();
    ASSERT_NE(a, SRT_INVALID_SOCK);
    ASSERT_NE(b, SRT_INVALID_SOCK);
    for (int i = 0; i < 2; ++i)
    {
        const SRTSOCKET s = i ? b : a;
        if (rendezvous)
        {
            ASSERT_EQ(srt_setsockflag(s, SRTO_RENDEZVOUS, &yes, sizeof yes), 0);
        }
        ASSERT_EQ(srt_setsockflag(s, SRTO_PASSPHRASE, pw.c_str(), (int)pw.size()), 0);
        ASSERT_EQ(srt_setsockflag(s, SRTO_LATENCY, &latency, sizeof latency), 0);
        ASSERT_EQ(srt_setsockflag(s, SRTO_TLPKTDROP, &no, sizeof no), 0);
        if (kmrefresh > 0)
        {
            ASSERT_EQ(srt_setsockflag(s, SRTO_KMREFRESHRATE, &kmrefresh, sizeof kmrefresh), 0);
        }
    }

    sockaddr_in sa_a, sa_b;
    ASSERT_TRUE(bindFree(b, sa_b, 5100));
    SRTSOCKET conn[2] = {a, SRT_INVALID_SOCK};
    int       cres[2] = {SRT_ERROR, SRT_ERROR};
    if (rendezvous)
    {
        ASSERT_TRUE(bindFree(a, sa_a, ntohs(sa_b.sin_port) + 1));
        std::thread peer([&] { cres[1] = srt_connect(b, (sockaddr*)&sa_a, sizeof sa_a); });
        cres[0] = srt_connect(a, (sockaddr*)&sa_b, sizeof sa_b);
        peer.join();
        conn[1] = b;
    }
    else
    {
        ASSERT_EQ(srt_listen(b, 1), 0);
        std::thread acceptor([&] {
            sockaddr_storage ss;
            int              sl = sizeof ss;
            conn[1]             = srt_accept(b, (sockaddr*)&ss, &sl);
        });
        cres[0] = srt_connect(a, (sockaddr*)&sa_b, sizeof sa_b);
        if (cres[0] == SRT_ERROR)
            srt_close(b); // unblocks srt_accept()
        acceptor.join();
        cres[1] = conn[1] == SRT_INVALID_SOCK ? SRT_ERROR : 0;
    }
    ASSERT_NE(cres[0], SRT_ERROR) << srt_getlasterror_str();
    ASSERT_NE(cres[1], SRT_ERROR) << srt_getlasterror_str();
    for (int i = 0; i < 2; ++i)
        ASSERT_EQ(srt_setsockflag(conn[i], SRTO_RCVTIMEO, &rcvtimeo, sizeof rcvtimeo), 0);

    std::vector<std::thread> th;
    for (int d = 0; d < 2; ++d)
    {
        const uint32_t salt = uint32_t(d + 1) << 20; // distinct payloads per direction
        th.push_back(std::thread([&, d, salt] {
            std::vector<char> buf(kPld);
            for (int i = 0; i < npkts; ++i)
            {
                fillPattern(&buf[0], kPld, salt ^ (uint32_t)i);
                if (srt_sendmsg2(conn[d], &buf[0], (int)kPld, NULL) == (int)kPld)
                    ++w_res[d].sent;
                if (i % 16 == 15)
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }));
        th.push_back(std::thread([&, d, salt] {
            std::vector<char> rbuf(1500), expect(kPld);
            for (int i = 0; i < npkts; ++i)
            {
                const int n = srt_recvmsg2(conn[1 - d], &rbuf[0], (int)rbuf.size(), NULL);
                if (n <= 0)
                    break;
                fillPattern(&expect[0], kPld, salt ^ (uint32_t)i);
                if (n != (int)kPld || memcmp(&rbuf[0], &expect[0], kPld) != 0)
                {
                    if (w_res[d].corrupted++ == 0)
                        w_res[d].first_corrupted = i;
                    if (i < 64)
                        ++w_res[d].early_corrupted;
                }
                ++w_res[d].received;
            }
        }));
    }
    for (size_t i = 0; i < th.size(); ++i)
        th[i].join();

    srt_close(conn[1]);
    srt_close(a);
    if (conn[1] != b)
        srt_close(b);
}

void reportBidir(const char* d0, const char* d1, const E2EResult (&r)[2], int npkts, bool tolerate_early = false)
{
    const char* name[2] = {d0, d1};
    for (int d = 0; d < 2; ++d)
    {
        std::cout << name[d] << ": sent=" << r[d].sent << " received=" << r[d].received
                  << " corrupted=" << r[d].corrupted << " first_corrupted=" << r[d].first_corrupted
                  << " early_corrupted=" << r[d].early_corrupted << std::endl;
        EXPECT_EQ(r[d].received, npkts) << name[d];
        if (tolerate_early)
        {
            // Rendezvous + low SRTO_KMREFRESHRATE: v1.5.7 already (rarely) delivers
            // packet #0 corrupted. That is pre-existing and unrelated to SECDIST, so
            // only the window after the first KM pre-announce (#64+) is asserted here.
            EXPECT_EQ(r[d].corrupted - r[d].early_corrupted, 0)
                << name[d] << ": first corrupted payload at #" << r[d].first_corrupted;
            if (r[d].early_corrupted)
                std::cout << "NOTE: " << name[d] << ": " << r[d].early_corrupted
                          << " early packet(s) corrupted (pre-existing, also on v1.5.7)" << std::endl;
        }
        else
        {
            EXPECT_EQ(r[d].corrupted, 0) << name[d] << ": first corrupted payload at #" << r[d].first_corrupted;
        }
    }
}

} // namespace

TEST(SecDistE2E, CallerSender_KeyRefresh)
{
    srt::TestInit srtinit;
    E2EResult r;
    ASSERT_NO_FATAL_FAILURE(runLoopback(false, 128, 600, r));
    std::cout << "sent=" << r.sent << " received=" << r.received << " corrupted=" << r.corrupted
              << " first_corrupted=" << r.first_corrupted << std::endl;
    EXPECT_EQ(r.received, 600);
    EXPECT_EQ(r.corrupted, 0) << "first corrupted payload at #" << r.first_corrupted;
}

TEST(SecDistE2E, ListenerSender_KeyRefresh)
{
    srt::TestInit srtinit;
    E2EResult r;
    ASSERT_NO_FATAL_FAILURE(runLoopback(true, 128, 600, r));
    std::cout << "sent=" << r.sent << " received=" << r.received << " corrupted=" << r.corrupted
              << " first_corrupted=" << r.first_corrupted << std::endl;
    EXPECT_EQ(r.received, 600);
    EXPECT_EQ(r.corrupted, 0) << "first corrupted payload at #" << r.first_corrupted;
}

TEST(SecDistE2E, ListenerSender_NoRefresh_Control)
{
    srt::TestInit srtinit;
    E2EResult r;
    ASSERT_NO_FATAL_FAILURE(runLoopback(true, 0, 600, r));
    std::cout << "sent=" << r.sent << " received=" << r.received << " corrupted=" << r.corrupted
              << " first_corrupted=" << r.first_corrupted << std::endl;
    EXPECT_EQ(r.received, 600);
    EXPECT_EQ(r.corrupted, 0) << "first corrupted payload at #" << r.first_corrupted;
}

TEST(SecDistE2E, CallerListener_Bidir_KeyRefresh)
{
    srt::TestInit srtinit;
    E2EResult r[2];
    ASSERT_NO_FATAL_FAILURE(runBidir(false, 128, 600, r));
    reportBidir("caller->listener", "listener->caller", r, 600);
}

TEST(SecDistE2E, Rendezvous_Bidir_KeyRefresh)
{
    srt::TestInit srtinit;
    E2EResult r[2];
    ASSERT_NO_FATAL_FAILURE(runBidir(true, 128, 600, r));
    reportBidir("A->B", "B->A", r, 600, true);
}

TEST(SecDistE2E, Rendezvous_Bidir_NoRefresh_Control)
{
    srt::TestInit srtinit;
    E2EResult r[2];
    ASSERT_NO_FATAL_FAILURE(runBidir(true, 0, 600, r));
    reportBidir("A->B", "B->A", r, 600);
}

#endif // SRT_ENABLE_ENCRYPTION
