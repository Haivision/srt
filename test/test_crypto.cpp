#include <array>
#include <numeric>
#include <vector>
#include <thread>
#include <mutex>

#include "gtest/gtest.h"
#include "test_env.h"

#ifdef SRT_ENABLE_ENCRYPTION


#include "crypto.h"
#include "handshake.h"
#include "hcrypt_msg.h"
#include "hcrypt.h" // Imports the CRYSPR_HAS_AESGCM definition.
#include "socketconfig.h"
#include "api.h"

// processSrtMsg_KMRSP must reject malformed wire-supplied lengths before they
// reach the fixed-size stack buffer / uninitialised-read paths inside the
// function. Built into the library unconditionally, so this test runs
// regardless of SRT_ENABLE_ENCRYPTION.
TEST(CryptoKMRSP, RejectsMalformedLengths)
{
    srt::CCryptoControl crypt(0);
    std::vector<uint32_t> garbage(SRT_CMD_MAXSZ, 0);
    const unsigned srtv = srt::SrtVersion(1, 5, 3);

    // Oversize: would overflow uint32_t srtd[SRTDATA_MAXSIZE].
    EXPECT_EQ(crypt.processSrtMsg_KMRSP(garbage.data(), SRT_CMD_MAXSZ + sizeof(uint32_t), srtv),
              srt::SRT_CMD_NONE);
}


#ifdef ENABLE_AEAD_API_PREVIEW

class Crypto
    : public srt::Test
{
protected:
    Crypto()
        : m_crypt(0)
    {
        // initialization code here
    }

    virtual ~Crypto()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:
    void setup() override
    {
        using namespace srt;

        CSrtConfig cfg;

        memset(&cfg.CryptoSecret, 0, sizeof(cfg.CryptoSecret));
        cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
        cfg.CryptoSecret.len = (m_pwd.size() <= (int)sizeof(cfg.CryptoSecret.str) ? m_pwd.size() : (int)sizeof(cfg.CryptoSecret.str));
        memcpy((cfg.CryptoSecret.str), m_pwd.c_str(), m_pwd.size());

        m_crypt.setCryptoSecret(cfg.CryptoSecret);

        // 2 = 128, 3 = 192, 4 = 256
        cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
        m_crypt.setCryptoKeylen(cfg.iSndCryptoKeyLen);

        cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_GCM;
        EXPECT_TRUE(m_crypt.init(HSD_INITIATOR, cfg, true, HaiCrypt_IsAESGCM_Supported()));

        const unsigned char* kmmsg = m_crypt.getKmMsg_data(0);
        const size_t km_len = m_crypt.getKmMsg_size(0);
        uint32_t kmout[72];
        size_t kmout_len = 72;

        std::array<uint32_t, 72> km_nworder;
        NtoHLA(km_nworder.data(), reinterpret_cast<const uint32_t*>(kmmsg), km_len);
        m_crypt.processSrtMsg_KMREQ(km_nworder.data(), km_len, 5, SrtVersion(1, 5, 3), kmout, kmout_len);
    }

    void teardown() override
    {
    }

protected:

    srt::CCryptoControl m_crypt;
    const std::string m_pwd = "abcdefghijk";
};


// Check that destroying the buffer also frees memory units.
TEST_F(Crypto, GCM)
{
    using namespace srt;

    if (HaiCrypt_IsAESGCM_Supported() == 0)
        GTEST_SKIP() << "The crypto service provider does not support AES GCM.";

    const size_t mtu_size = 1500;
    const size_t pld_size = 1316;
    const size_t tag_len  = 16;

    CPacket pkt;
    pkt.allocate(mtu_size);

    const int seqno = 1;
    const int msgno = 1;
    const int inorder = 1;
    const int kflg = m_crypt.getSndCryptoFlags();

    pkt.set_seqno(seqno);
    pkt.set_msgflags(msgno | inorder | PacketBoundaryBits(PB_SOLO) | MSGNO_ENCKEYSPEC::wrap(kflg));
    pkt.set_timestamp(356);

    std::iota(pkt.data(), pkt.data() + pld_size, '0');
    pkt.setLength(pld_size);

    EXPECT_EQ(m_crypt.encrypt(pkt), ENCS_CLEAR);
    EXPECT_EQ(pkt.getLength(), pld_size + tag_len);

    auto pkt_enc = std::unique_ptr<CPacket>(pkt.clone());

    EXPECT_EQ(m_crypt.decrypt(pkt), ENCS_CLEAR);
    EXPECT_EQ(pkt.getLength(), pld_size);

    // Modify the payload and expect auth to fail.
    pkt_enc->data()[10] = '5';
    EXPECT_EQ(m_crypt.decrypt(*pkt_enc.get()), ENCS_FAILED);
}

// KMREQ that fails AES-KW unwrap must not downgrade a SECURED session.
TEST_F(Crypto, KMREQ_Unwrap_Failure_Does_Not_Downgrade_Secured)
{
    using namespace srt;

    if (HaiCrypt_IsAESGCM_Supported() == 0)
        GTEST_SKIP() << "The crypto service provider does not support AES GCM.";

    ASSERT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED);

    // KMREQ wrapped with a different passphrase -> HAICRYPT_ERROR_WRONG_SECRET.
    CCryptoControl other(/*socket id*/1);
    CSrtConfig cfg;
    memset(&cfg.CryptoSecret, 0, sizeof(cfg.CryptoSecret));
    cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    const std::string other_pwd = "completely_different_xy";
    cfg.CryptoSecret.len = (int)other_pwd.size();
    memcpy(cfg.CryptoSecret.str, other_pwd.c_str(), other_pwd.size());
    other.setCryptoSecret(cfg.CryptoSecret);

    cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
    other.setCryptoKeylen(cfg.iSndCryptoKeyLen);
    cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_GCM;
    ASSERT_TRUE(other.init(HSD_INITIATOR, cfg, true, HaiCrypt_IsAESGCM_Supported()));

    const unsigned char* kmmsg = other.getKmMsg_data(0);
    const size_t km_len = other.getKmMsg_size(0);
    ASSERT_GT(km_len, 0u);

    std::array<uint32_t, 72> km_nworder;
    NtoHLA(km_nworder.data(), reinterpret_cast<const uint32_t*>(kmmsg), km_len);

    uint32_t kmout[72];
    size_t kmout_len = 72;
    m_crypt.processSrtMsg_KMREQ(km_nworder.data(), km_len, 5, SrtVersion(1, 5, 3),
                                kmout, kmout_len);

    EXPECT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED);
}

// Malformed KMREQ (too-small payload) on a SECURED session must not
// downgrade state.
TEST_F(Crypto, KMREQ_MalformedSize_Does_Not_Downgrade_Secured)
{
    using namespace srt;

    if (HaiCrypt_IsAESGCM_Supported() == 0)
        GTEST_SKIP() << "The crypto service provider does not support AES GCM.";

    ASSERT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED);

    // Payload <= HCRYPT_MSG_KM_OFS_SALT trips the size sanity check.
    uint32_t tiny[2] = {0, 0};
    uint32_t kmout[72];
    size_t kmout_len = 72;
    EXPECT_EQ(m_crypt.processSrtMsg_KMREQ(tiny, sizeof(tiny),
                5, SrtVersion(1, 5, 3), kmout, kmout_len),
            SRT_CMD_NONE);
    EXPECT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED);
    EXPECT_EQ(kmout[SRT_KMR_KMSTATE], (uint32_t)SRT_KM_S_BADSECRET);
    EXPECT_EQ(kmout_len, 1u);
}

// KMREQ with KLEN byte zeroed -> hcryptMsg_KM_GetSekLen returns 0,
// tripping the empty-SEK rejection. Must not downgrade SECURED state.
TEST_F(Crypto, KMREQ_EmptySEK_Does_Not_Downgrade_Secured)
{
    using namespace srt;

    if (HaiCrypt_IsAESGCM_Supported() == 0)
        GTEST_SKIP() << "The crypto service provider does not support AES GCM.";

    ASSERT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED);

    // Take a structurally-valid KMREQ and zero the KLEN field.
    const unsigned char* kmmsg = m_crypt.getKmMsg_data(0);
    const size_t km_len = m_crypt.getKmMsg_size(0);
    std::array<unsigned char, HCRYPT_MSG_KM_MAX_SZ> patched;
    memcpy(patched.data(), kmmsg, km_len);
    patched[HCRYPT_MSG_KM_OFS_KLEN] = 0;

    std::array<uint32_t, 72> km_nworder;
    NtoHLA(km_nworder.data(), reinterpret_cast<const uint32_t*>(patched.data()), km_len);

    uint32_t kmout[72] = {0};
    size_t kmout_len = 72;
    EXPECT_EQ(m_crypt.processSrtMsg_KMREQ(km_nworder.data(), km_len,
                5, SrtVersion(1, 5, 3), kmout, kmout_len),
            SRT_CMD_NONE);
    EXPECT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED);
    EXPECT_EQ(kmout[SRT_KMR_KMSTATE], (uint32_t)SRT_KM_S_BADSECRET);
    EXPECT_EQ(kmout_len, 1u);
}

// Forged KMRSP claiming any peer-failure state must not downgrade a
// SECURED session. The dispatcher accepts KMRSPs unconditionally so each
// peerstate branch in processSrtMsg_KMRSP is reachable off-path.
TEST_F(Crypto, DISABLED_KMRSP_PeerFailure_Does_Not_Downgrade_Secured)
{
    using namespace srt;

    if (HaiCrypt_IsAESGCM_Supported() == 0)
        GTEST_SKIP() << "The crypto service provider does not support AES GCM.";

    const SRT_KM_STATE wire_peerstates[] = {
        SRT_KM_S_BADSECRET,
        SRT_KM_S_NOSECRET,
        SRT_KM_S_UNSECURED,
#ifdef ENABLE_AEAD_API_PREVIEW
        SRT_KM_S_BADCRYPTOMODE,
#endif
        // An out-of-enum value drives the default ("IPE: unknown peer
        // error state") branch in the switch.
        (SRT_KM_STATE)99,
    };

    for (size_t i = 0; i < sizeof(wire_peerstates)/sizeof(wire_peerstates[0]); ++i)
    {
        // Reset the agent into a fully-SECURED state for each iteration.
        m_crypt.m_RcvKmState = SRT_KM_S_SECURED;
        m_crypt.m_SndKmState = SRT_KM_S_SECURED;

        // Wire format is network byte order; the function will HtoNLA it
        // back. Pre-NtoHLA so srtd[SRT_KMR_KMSTATE] inside the function
        // reads the intended peerstate.
        uint32_t wire = (uint32_t)wire_peerstates[i];
        uint32_t input = 0;
        NtoHLA(&input, &wire, 1);
        EXPECT_EQ(m_crypt.processSrtMsg_KMRSP(&input, sizeof(input), SrtVersion(1, 5, 3)),
                    SRT_CMD_NONE);

        EXPECT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED)
            << "peerstate=" << (int)wire_peerstates[i] << " downgraded m_RcvKmState";
        EXPECT_EQ(m_crypt.m_SndKmState, SRT_KM_S_SECURED)
            << "peerstate=" << (int)wire_peerstates[i] << " downgraded m_SndKmState";
    }
}

// After a forged KMREQ unwrap failure on a SECURED session, m_SndKmState
// must also stay SECURED so sendingAllowed() keeps returning true.
TEST_F(Crypto, KMREQ_Unwrap_Failure_Preserves_SndKmState_Secured)
{
    using namespace srt;

    if (HaiCrypt_IsAESGCM_Supported() == 0)
        GTEST_SKIP() << "The crypto service provider does not support AES GCM.";

    // setup() leaves m_SndKmState=SECURING. Simulate a fully-handshaken
    // session where the peer's KMRSP would have moved it to SECURED.
    m_crypt.m_SndKmState = SRT_KM_S_SECURED;
    ASSERT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED);
    ASSERT_EQ(m_crypt.m_SndKmState, SRT_KM_S_SECURED);

    // Forge a KMREQ via a second CCryptoControl with a different passphrase.
    CCryptoControl other(/*socket id*/1);
    CSrtConfig cfg;
    memset(&cfg.CryptoSecret, 0, sizeof(cfg.CryptoSecret));
    cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    const std::string other_pwd = "different_passphrase_xy";
    cfg.CryptoSecret.len = (int)other_pwd.size();
    memcpy(cfg.CryptoSecret.str, other_pwd.c_str(), other_pwd.size());
    other.setCryptoSecret(cfg.CryptoSecret);
    cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
    other.setCryptoKeylen(cfg.iSndCryptoKeyLen);
    cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_GCM;
    ASSERT_TRUE(other.init(HSD_INITIATOR, cfg, true, HaiCrypt_IsAESGCM_Supported()));

    const unsigned char* kmmsg = other.getKmMsg_data(0);
    const size_t km_len = other.getKmMsg_size(0);
    std::array<uint32_t, 72> km_nworder;
    NtoHLA(km_nworder.data(), reinterpret_cast<const uint32_t*>(kmmsg), km_len);

    uint32_t kmout[72];
    size_t kmout_len = 72;
    m_crypt.processSrtMsg_KMREQ(km_nworder.data(), km_len, 5, SrtVersion(1, 5, 3),
                                kmout, kmout_len);

    EXPECT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED);
    EXPECT_EQ(m_crypt.m_SndKmState, SRT_KM_S_SECURED);
}

// Regression-test fixture that runs in default builds (CTR mode, no AEAD
// preview required). Mirrors the setup of the Crypto fixture above.
class CryptoCtr
: public srt::Test
{
protected:
    CryptoCtr() : m_crypt(0) {}

    void setup() override
    {
        using namespace srt;
        CSrtConfig cfg;
        memset(&cfg.CryptoSecret, 0, sizeof(cfg.CryptoSecret));
        cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
        cfg.CryptoSecret.len = (m_pwd.size() <= (int)sizeof(cfg.CryptoSecret.str) ? m_pwd.size() : (int)sizeof(cfg.CryptoSecret.str));
        memcpy((cfg.CryptoSecret.str), m_pwd.c_str(), m_pwd.size());

        m_crypt.setCryptoSecret(cfg.CryptoSecret);
        cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
        m_crypt.setCryptoKeylen(cfg.iSndCryptoKeyLen);
        cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_CTR;
        EXPECT_TRUE(m_crypt.init(HSD_INITIATOR, cfg, true, false));

        const unsigned char* kmmsg = m_crypt.getKmMsg_data(0);
        const size_t km_len = m_crypt.getKmMsg_size(0);
        uint32_t kmout[72];
        size_t kmout_len = 72;

        std::array<uint32_t, 72> km_nworder;
        NtoHLA(km_nworder.data(), reinterpret_cast<const uint32_t*>(kmmsg), km_len);
        m_crypt.processSrtMsg_KMREQ(km_nworder.data(), km_len, 5, SrtVersion(1, 5, 3), kmout, kmout_len);
    }

    void teardown() override
    {
    }

protected:
    srt::CCryptoControl m_crypt;
    const std::string m_pwd = "abcdefghijk";
};

// Regression test: validates that the legitimate SECURING -> SECURED
// transition still happens on a valid KMREQ. Catches a future over-
// applied SECURED-preserving guard that would block this path.
TEST_F(CryptoCtr, InitialHandshakeReachesSecured)
{
    EXPECT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED);
}

// Regression test: from the SECURING initial state, a KMREQ wrapped with
// the wrong passphrase must still drive m_RcvKmState to BADSECRET. The
// SECURED-preserving guard must not block the initial transition.
TEST_F(CryptoCtr, WrongPassphraseAtInitialReachesBadSecret)
{
    using namespace srt;

    // Build a fresh crypter that has NOT been driven through setup's
    // bootstrap KMREQ, so m_RcvKmState is still SECURING.
    CCryptoControl fresh(/*socket id*/2);
    CSrtConfig fresh_cfg;
    memset(&fresh_cfg.CryptoSecret, 0, sizeof(fresh_cfg.CryptoSecret));
    fresh_cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    fresh_cfg.CryptoSecret.len = (int)m_pwd.size();
    memcpy(fresh_cfg.CryptoSecret.str, m_pwd.c_str(), m_pwd.size());
    fresh.setCryptoSecret(fresh_cfg.CryptoSecret);
    fresh_cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
    fresh.setCryptoKeylen(fresh_cfg.iSndCryptoKeyLen);
    fresh_cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_CTR;
    ASSERT_TRUE(fresh.init(HSD_INITIATOR, fresh_cfg, true, false));
    ASSERT_NE(fresh.m_RcvKmState, SRT_KM_S_SECURED);

    // Forge a KMREQ from a peer using a different passphrase.
    CCryptoControl other(/*socket id*/3);
    CSrtConfig other_cfg;
    memset(&other_cfg.CryptoSecret, 0, sizeof(other_cfg.CryptoSecret));
    other_cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    const std::string other_pwd = "wrong_passphrase_xy";
    other_cfg.CryptoSecret.len = (int)other_pwd.size();
    memcpy(other_cfg.CryptoSecret.str, other_pwd.c_str(), other_pwd.size());
    other.setCryptoSecret(other_cfg.CryptoSecret);
    other_cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
    other.setCryptoKeylen(other_cfg.iSndCryptoKeyLen);
    other_cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_CTR;
    ASSERT_TRUE(other.init(HSD_INITIATOR, other_cfg, true, false));

    const unsigned char* kmmsg = other.getKmMsg_data(0);
    const size_t km_len = other.getKmMsg_size(0);
    std::array<uint32_t, 72> km_nworder;
    NtoHLA(km_nworder.data(), reinterpret_cast<const uint32_t*>(kmmsg), km_len);

    const SRT_KM_STATE prev_state = fresh.m_RcvKmState;

    uint32_t kmout[72];
    size_t kmout_len = 72;
    int cmd = fresh.processSrtMsg_KMREQ(km_nworder.data(), km_len, 5, SrtVersion(1, 5, 3),
            kmout, kmout_len);

    // The guard must NOT block this transition: from SECURING the state
    // must reach BADSECRET so the connection can be rejected.
    // XXX NOTE: The behavior has been changed and now KMREQ failure is
    // simply ignored, if it was done as update. And as we create the
    // crypto with init, this is initialized just like through handshake,
    // so this KMREQ is considered a KMX update. We have then the right
    // state in the output array, but the state remains secure.
    EXPECT_EQ(fresh.m_RcvKmState, prev_state);
    EXPECT_EQ(cmd, SRT_CMD_NONE);
    EXPECT_EQ(kmout[0], SRT_KM_S_BADSECRET);
}

// Regression test: a fresh KMREQ wrapped with the SAME passphrase
// (i.e. legitimate key rotation) on an already-SECURED session must
// succeed. State stays SECURED and the KMRSP returned is a full success
// response, not a 1-word error.
//
// This is the highest-risk regression vector for the SECURED-preserving
// guards: if they accidentally over-applied to the success path, key
// rotation would silently fail and streams would eventually drop.
TEST_F(CryptoCtr, KmRefreshOnSecuredSucceeds)
{
    using namespace srt;

    ASSERT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED);

    // Build a fresh sender with the SAME passphrase. Its generated KM
    // message will carry a different SEK but the same KEK as m_crypt,
    // so the unwrap will succeed on m_crypt's side. This mirrors what
    // happens when the live sender's regenCryptoKm rolls a new key.
    CCryptoControl rotator(/*socket id*/4);
    CSrtConfig cfg;
    memset(&cfg.CryptoSecret, 0, sizeof(cfg.CryptoSecret));
    cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    cfg.CryptoSecret.len = (int)m_pwd.size();
    memcpy(cfg.CryptoSecret.str, m_pwd.c_str(), m_pwd.size());
    rotator.setCryptoSecret(cfg.CryptoSecret);
    cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
    rotator.setCryptoKeylen(cfg.iSndCryptoKeyLen);
    cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_CTR;
    ASSERT_TRUE(rotator.init(HSD_INITIATOR, cfg, true, false));

    const unsigned char* kmmsg = rotator.getKmMsg_data(0);
    const size_t km_len = rotator.getKmMsg_size(0);
    std::array<uint32_t, 72> km_nworder;
    NtoHLA(km_nworder.data(), reinterpret_cast<const uint32_t*>(kmmsg), km_len);

    uint32_t kmout[72] = {0};
    size_t kmout_len = 72;
    m_crypt.processSrtMsg_KMREQ(km_nworder.data(), km_len, 5, SrtVersion(1, 5, 3),
            kmout, kmout_len);

    // State remains SECURED across the rotation.
    EXPECT_EQ(m_crypt.m_RcvKmState, SRT_KM_S_SECURED);

    // The success KMRSP echoes the input KM message (multi-word). A
    // rejection path would have set kmout_len == 1 and written a single
    // SRT_KMR_KMSTATE word.
    EXPECT_GT(kmout_len, 1u);
}

// Regression test: a RESPONDER-side bidirectional handshake must reach
// SECURED on BOTH directions via the RX -> TX context clone path in
// processSrtMsg_KMREQ. This is the most complex success transition in
// the handler and is unguarded by design (gated by m_SndKmState ==
// SECURING && !m_hSndCrypto).
TEST_F(CryptoCtr, ResponderHandshakeReachesSecuredViaClone)
{
    using namespace srt;

    // RESPONDER doesn't create m_hSndCrypto in init(), so the
    // SECURING + !m_hSndCrypto precondition for the clone block holds.
    CCryptoControl responder(/*socket id*/8);
    CSrtConfig cfg;
    memset(&cfg.CryptoSecret, 0, sizeof(cfg.CryptoSecret));
    cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    cfg.CryptoSecret.len = (int)m_pwd.size();
    memcpy(cfg.CryptoSecret.str, m_pwd.c_str(), m_pwd.size());
    responder.setCryptoSecret(cfg.CryptoSecret);
    cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
    responder.setCryptoKeylen(cfg.iSndCryptoKeyLen);
    cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_CTR;
    ASSERT_TRUE(responder.init(HSD_RESPONDER, cfg, true, false));

    // Use the fixture's own (INITIATOR-side) KM message as the KMREQ
    // payload for the responder. Same passphrase, so unwrap succeeds.
    const unsigned char* kmmsg = m_crypt.getKmMsg_data(0);
    const size_t km_len = m_crypt.getKmMsg_size(0);
    std::array<uint32_t, 72> km_nworder;
    NtoHLA(km_nworder.data(), reinterpret_cast<const uint32_t*>(kmmsg), km_len);

    uint32_t kmout[72];
    size_t kmout_len = 72;
    responder.processSrtMsg_KMREQ(km_nworder.data(), km_len, 5,
            SrtVersion(1, 5, 3), kmout, kmout_len);

    EXPECT_EQ(responder.m_RcvKmState, SRT_KM_S_SECURED);
    EXPECT_EQ(responder.m_SndKmState, SRT_KM_S_SECURED);
}

// Regression test: when the agent has no passphrase but the peer sends
// a valid KMREQ, m_RcvKmState must transition UNSECURED -> NOSECRET. The
// SECURED-preserving guard must not block this legitimate non-SECURED
// transition.
TEST_F(CryptoCtr, AgentWithoutPasswordGetsNoSecret)
{
    using namespace srt;

    // Agent with no passphrase. init() leaves both states at UNSECURED.
    CCryptoControl no_pw_agent(/*socket id*/5);
    CSrtConfig agent_cfg;
    memset(&agent_cfg.CryptoSecret, 0, sizeof(agent_cfg.CryptoSecret));
    // typ left as 0 / len 0 -> hasPassphrase() returns false.
    no_pw_agent.setCryptoSecret(agent_cfg.CryptoSecret);
    agent_cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
    no_pw_agent.setCryptoKeylen(agent_cfg.iSndCryptoKeyLen);
    agent_cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_CTR;
    ASSERT_TRUE(no_pw_agent.init(HSD_INITIATOR, agent_cfg, true, false));
    ASSERT_NE(no_pw_agent.m_RcvKmState, SRT_KM_S_SECURED);

    // Peer with a valid passphrase sends KMREQ.
    CCryptoControl peer(/*socket id*/6);
    CSrtConfig peer_cfg;
    memset(&peer_cfg.CryptoSecret, 0, sizeof(peer_cfg.CryptoSecret));
    peer_cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    peer_cfg.CryptoSecret.len = (int)m_pwd.size();
    memcpy(peer_cfg.CryptoSecret.str, m_pwd.c_str(), m_pwd.size());
    peer.setCryptoSecret(peer_cfg.CryptoSecret);
    peer_cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
    peer.setCryptoKeylen(peer_cfg.iSndCryptoKeyLen);
    peer_cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_CTR;
    ASSERT_TRUE(peer.init(HSD_INITIATOR, peer_cfg, true, false));

    const unsigned char* kmmsg = peer.getKmMsg_data(0);
    const size_t km_len = peer.getKmMsg_size(0);
    std::array<uint32_t, 72> km_nworder;
    NtoHLA(km_nworder.data(), reinterpret_cast<const uint32_t*>(kmmsg), km_len);

    uint32_t kmout[72];
    size_t kmout_len = 72;
    no_pw_agent.processSrtMsg_KMREQ(km_nworder.data(), km_len, 5,
            SrtVersion(1, 5, 3), kmout, kmout_len);

    EXPECT_EQ(no_pw_agent.m_RcvKmState, SRT_KM_S_NOSECRET);
}

// Regression test: receiving a KMRSP error report on a non-SECURED
// session must still update state per the peer's report. The SECURED-
// preserving guard must not block this.
TEST_F(CryptoCtr, KmrspPeerNoSecretOnNonSecured)
{
    using namespace srt;

    // Fresh crypter with passphrase but no bootstrap KMREQ processed.
    CCryptoControl fresh(/*socket id*/7);
    CSrtConfig cfg;
    memset(&cfg.CryptoSecret, 0, sizeof(cfg.CryptoSecret));
    cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    cfg.CryptoSecret.len = (int)m_pwd.size();
    memcpy(cfg.CryptoSecret.str, m_pwd.c_str(), m_pwd.size());
    fresh.setCryptoSecret(cfg.CryptoSecret);
    cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
    fresh.setCryptoKeylen(cfg.iSndCryptoKeyLen);
    cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_CTR;
    ASSERT_TRUE(fresh.init(HSD_INITIATOR, cfg, true, false));
    ASSERT_NE(fresh.m_RcvKmState, SRT_KM_S_SECURED);

    // KMRSP carrying a single peerstate word = NOSECRET (peer has no PW).
    uint32_t wire = (uint32_t)SRT_KM_S_NOSECRET;
    uint32_t input = 0;
    NtoHLA(&input, &wire, 1);
    fresh.processSrtMsg_KMRSP(&input, sizeof(input), SrtVersion(1, 5, 3));

    // Per crypto.cpp KMRSP NOSECRET branch: RX -> UNSECURED, SND -> NOSECRET.
    EXPECT_EQ(fresh.m_RcvKmState, SRT_KM_S_UNSECURED);
    EXPECT_EQ(fresh.m_SndKmState, SRT_KM_S_NOSECRET);
}

// Regression test: a successful KMRSP (a KMRSP whose multi-word body
// matches a stored sender KM message) must transition both m_RcvKmState
// and m_SndKmState to SECURED via the success branch in
// processSrtMsg_KMRSP. Mirror of the KMREQ success path.
TEST_F(CryptoCtr, KmrspSuccessTransitionsToSecured)
{
    using namespace srt;

    // Fresh initiator with a passphrase and the standard SND KM message
    // built by init(), but without the bootstrap KMREQ that setup ran on
    // m_crypt. m_SndKmState should be SECURING here.
    CCryptoControl fresh(/*socket id*/9);
    CSrtConfig cfg;
    memset(&cfg.CryptoSecret, 0, sizeof(cfg.CryptoSecret));
    cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    cfg.CryptoSecret.len = (int)m_pwd.size();
    memcpy(cfg.CryptoSecret.str, m_pwd.c_str(), m_pwd.size());
    fresh.setCryptoSecret(cfg.CryptoSecret);
    cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
    fresh.setCryptoKeylen(cfg.iSndCryptoKeyLen);
    cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_CTR;
    ASSERT_TRUE(fresh.init(HSD_INITIATOR, cfg, true, false));
    ASSERT_NE(fresh.m_SndKmState, SRT_KM_S_SECURED);

    // A successful KMRSP carries back the same KM payload the agent had
    // sent. Feed the agent's own stored KM message back as the KMRSP body.
    const unsigned char* kmmsg = fresh.getKmMsg_data(0);
    const size_t km_len = fresh.getKmMsg_size(0);
    ASSERT_GT(km_len, 0u);

    std::array<uint32_t, 72> km_nworder;
    NtoHLA(km_nworder.data(), reinterpret_cast<const uint32_t*>(kmmsg), km_len);

    fresh.processSrtMsg_KMRSP(km_nworder.data(), km_len, SrtVersion(1, 5, 3));

    EXPECT_EQ(fresh.m_RcvKmState, SRT_KM_S_SECURED);
    EXPECT_EQ(fresh.m_SndKmState, SRT_KM_S_SECURED);
}

// Regression test: KMRSP carrying the UNSECURED peer-error code on a
// non-SECURED session must transition RX -> NOSECRET, SND -> UNSECURED.
// Mirror of the NOSECRET branch but with the opposite target mapping.
TEST_F(CryptoCtr, KmrspPeerUnsecuredOnNonSecured)
{
    using namespace srt;

    CCryptoControl fresh(/*socket id*/10);
    CSrtConfig cfg;
    memset(&cfg.CryptoSecret, 0, sizeof(cfg.CryptoSecret));
    cfg.CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
    cfg.CryptoSecret.len = (int)m_pwd.size();
    memcpy(cfg.CryptoSecret.str, m_pwd.c_str(), m_pwd.size());
    fresh.setCryptoSecret(cfg.CryptoSecret);
    cfg.iSndCryptoKeyLen = SrtHSRequest::SRT_PBKEYLEN_BITS::wrap(4);
    fresh.setCryptoKeylen(cfg.iSndCryptoKeyLen);
    cfg.iCryptoMode = CSrtConfig::CIPHER_MODE_AES_CTR;
    ASSERT_TRUE(fresh.init(HSD_INITIATOR, cfg, true, false));
    ASSERT_NE(fresh.m_RcvKmState, SRT_KM_S_SECURED);

    uint32_t wire = (uint32_t)SRT_KM_S_UNSECURED;
    uint32_t input = 0;
    NtoHLA(&input, &wire, 1);
    fresh.processSrtMsg_KMRSP(&input, sizeof(input), SrtVersion(1, 5, 3));

    // Per crypto.cpp KMRSP UNSECURED branch: RX -> NOSECRET, SND -> UNSECURED.
    EXPECT_EQ(fresh.m_RcvKmState, SRT_KM_S_NOSECRET);
    EXPECT_EQ(fresh.m_SndKmState, SRT_KM_S_UNSECURED);
}

#endif // AEAD

#endif //SRT_ENABLE_ENCRYPTION
