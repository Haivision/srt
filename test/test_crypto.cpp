#include <array>
#include <numeric>
#include <vector>

#include "gtest/gtest.h"

#include "crypto.h"
#include "handshake.h"

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

    // Non-word-aligned: silently drops bytes and risks misinterpretation.
    EXPECT_EQ(crypt.processSrtMsg_KMRSP(garbage.data(), 7, srtv), srt::SRT_CMD_NONE);

    // Empty / under-a-word: HtoNLA writes nothing and downstream code would read
    // uninitialised stack from srtd[].
    EXPECT_EQ(crypt.processSrtMsg_KMRSP(garbage.data(), 0, srtv), srt::SRT_CMD_NONE);
    EXPECT_EQ(crypt.processSrtMsg_KMRSP(garbage.data(), 3, srtv), srt::SRT_CMD_NONE);
}

#if defined(SRT_ENABLE_ENCRYPTION) && defined(ENABLE_AEAD_API_PREVIEW)
#include "hcrypt.h" // Imports the CRYSPR_HAS_AESGCM definition.
#include "socketconfig.h"

namespace srt
{

    class Crypto
        : public ::testing::Test
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
        // SetUp() is run immediately before a test starts.
        void SetUp() override
        {
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

        void TearDown() override
        {
        }

    protected:

        srt::CCryptoControl m_crypt;
        const std::string m_pwd = "abcdefghijk";
    };


    // Check that destroying the buffer also frees memory units.
    TEST_F(Crypto, GCM)
    {
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

} // namespace srt

#endif //SRT_ENABLE_ENCRYPTION && ENABLE_AEAD_API_PREVIEW
