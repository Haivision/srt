#include <stdio.h>
#include <string.h>
#include "gtest/gtest.h"

#ifdef SRT_ENABLE_ENCRYPTION
#include "common.h"
#include "hcrypt.h"
#include "version.h"


#if (CRYSPR_VERSION_NUMBER >= 0x010100)
#define WITH_FIPSMODE 1 /* 1: openssl-evp branch */
#endif

#define UT_PKT_MAXLEN 1500

const void *nullPtr = NULL;

/* TestCRYSPRmethods: Test presense of required cryspr methods */

class TestCRYSPRmethods
    : public ::testing::Test
{
protected:
    TestCRYSPRmethods()
    {
        // initialization code here
        cryspr_m = NULL;
        cryspr_fbm = NULL;
    }

    ~TestCRYSPRmethods()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

    // SetUp() is run immediately before a test starts.
#if defined(__GNUC__) && (__GNUC___ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
//  override only supported for GCC>=4.7
    void SetUp() override {
#else
    void SetUp() {
#endif
        cryspr_m = cryspr4SRT();
        cryspr_fbm = crysprInit(&cryspr_fb);

        ASSERT_NE(cryspr_m, nullPtr);
        ASSERT_NE(cryspr_fbm, nullPtr);
        ASSERT_EQ(cryspr_fbm, &cryspr_fb);
    }

#if defined(__GNUC__) && (__GNUC___ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
    void TearDown() override {
#else
    void TearDown() {
#endif
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
    }

protected:

    CRYSPR_methods *cryspr_m;   /* methods */
    CRYSPR_methods cryspr_fb, *cryspr_fbm; /* fall back methods */
//    CRYSPR_cb *cryspr_cb;       /* Control block */
};


TEST_F(TestCRYSPRmethods, MethodOpen)
{
    EXPECT_NE(cryspr_m, nullPtr);
    EXPECT_NE(cryspr_m->open, nullPtr);
}


TEST_F(TestCRYSPRmethods, init)
{
    ASSERT_NE(cryspr_m, nullPtr);
}

#if WITH_FIPSMODE
TEST_F(TestCRYSPRmethods, fipsmode)
{
    if(cryspr_m->fips_mode_set == NULL || cryspr_m->fips_mode_set == cryspr_fbm->fips_mode_set ) {
#if defined(CRYSPR_FIPSMODE)    //undef: not supported, 0: supported and Off by default, 1: enabled by default
        EXPECT_NE(cryspr_m->fips_mode_set, cryspr_fbm->fips_mode_set); //Fallback method cannot set FIPS mode
        EXPECT_EQ(cryspr_m->fips_mode_set(CRYSPR_FIPSMODE ? 0 : 1), CRYSPR_FIPSMODE);
        EXPECT_EQ(cryspr_m->fips_mode_set(CRYSPR_FIPSMODE), (CRYSPR_FIPSMODE? 0 : 1));
#endif /* CRYSPR_FIPSMODE */
    }
}
#endif /* WITH_FIPSMODE */

TEST_F(TestCRYSPRmethods, open)
{
    EXPECT_NE(cryspr_m->open, nullPtr);
}

TEST_F(TestCRYSPRmethods, close)
{
    EXPECT_NE(cryspr_m->close, nullPtr);
}

TEST_F(TestCRYSPRmethods, prng)
{
    EXPECT_NE(cryspr_m->prng, nullPtr);
}

TEST_F(TestCRYSPRmethods, aes_set_key)
{
    EXPECT_NE(cryspr_m->aes_set_key, nullPtr);
}

TEST_F(TestCRYSPRmethods, AESecb)
{
    if(cryspr_m->km_wrap == cryspr_fbm->km_wrap) {
        /* fallback KM_WRAP method used
         * AES-ECB method then required
         */
        EXPECT_NE(cryspr_m->aes_ecb_cipher, nullPtr);
        EXPECT_NE(cryspr_m->aes_ecb_cipher, cryspr_fbm->aes_ecb_cipher);
    }
}
TEST_F(TestCRYSPRmethods, AESctr)
{
    EXPECT_NE(cryspr_m->aes_ctr_cipher, nullPtr);
}

TEST_F(TestCRYSPRmethods, SHA1)
{
    if(cryspr_m->sha1_msg_digest == NULL || cryspr_m->km_pbkdf2 == cryspr_fbm->km_pbkdf2 ) {
        /* fallback PBKDF2 used
         * then sha1 method required.
         */
        EXPECT_NE(cryspr_m->sha1_msg_digest, nullPtr);
        EXPECT_NE(cryspr_m->sha1_msg_digest, cryspr_fbm->sha1_msg_digest);
    }
}


/* CRYSPR control block test */
class TestCRYSPRcrypto
    : public ::testing::Test
{
protected:
    TestCRYSPRcrypto()
    {
        // initialization code here
        cryspr_m = NULL;
        cryspr_fbm = NULL;
        cryspr_cb = NULL;
    }

    ~TestCRYSPRcrypto()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

    // SetUp() is run immediately before a test starts.
#if defined(__GNUC__) && (__GNUC___ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
//  override only supported for GCC>=4.7
    void SetUp() override {
#else
    void SetUp() {
#endif
        cryspr_m = cryspr4SRT();
        cryspr_fbm = crysprInit(&cryspr_fb);

        ASSERT_NE(cryspr_m, nullPtr);
        ASSERT_NE(cryspr_fbm, nullPtr);
        ASSERT_EQ(cryspr_fbm, &cryspr_fb);
        cryspr_cb = cryspr_m->open(cryspr_m, UT_PKT_MAXLEN);
        ASSERT_NE(cryspr_cb, nullPtr);
    }

#if defined(__GNUC__) && (__GNUC___ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
    void TearDown() override {
#else
    void TearDown() {
#endif
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        if (cryspr_m && cryspr_cb) {
            EXPECT_EQ(cryspr_m->close(cryspr_cb), 0);
        }
    }

protected:

    CRYSPR_methods *cryspr_m;   /* methods */
    CRYSPR_methods cryspr_fb, *cryspr_fbm; /* fall back methods */
    CRYSPR_cb *cryspr_cb;       /* Control block */
};

TEST_F(TestCRYSPRcrypto, CtrlBlock)
{
    EXPECT_EQ(cryspr_m, cryspr_cb->cryspr); //methods set in control block
}

/*PBKDF2-----------------------------------------------------------------------------------------*/

/* See https://asecuritysite.com/encryption/PBKDF2z
   to generate "known good" PBKDF2 hash
*/
/* Test Vector 1.1 to 1.3 */

struct UTVcryspr_pbkdf2 {
    const char *name;
    const char *passwd;
    const char *salt;
    int itr;
    size_t keklen;
    unsigned char kek[256/8];
};

/* PBKDF2 test vectors */
struct UTVcryspr_pbkdf2 pbkdf2_tv[] = {
    {//[0]
        /* testname */  "PBKDF2 tv1.128",
        /* passwd */    "000000000000",
        /* salt */      "00000000",
        /* iteration */ 2048,
        /* keklen */    128/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79}
    },
    {//[1]
        /* testname */  "PBKDF2 tv1.192",
        /* passwd */    "000000000000",
        /* salt */      "00000000",
        /* iteration */ 2048,
        /* keklen */    192/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79,
                         0x90,0xab,0xca,0x6e,0xf0,0x02,0xf1,0xad}
    },
    {//[2]
        /* testname */  "PBKDF2 tv1.256",
        /* passwd */    "000000000000",
        /* salt */      "00000000",
        /* iteration */ 2048,
        /* keklen */    256/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79,
                         0x90,0xab,0xca,0x6e,0xf0,0x02,0xf1,0xad,0x19,0x59,0xcf,0x18,0xac,0x91,0x53,0x3d}
    },
    {//[3]
        /* testname */  "PBKDF2 tv2.1",
        /* passwd */    "password",
        /* salt */      "salt",
        /* iteration */ 1,
        /* keklen */    20,
        /* kek */       {0x0c,0x60,0xc8,0x0f,0x96,0x1f,0x0e,0x71,0xf3,0xa9,0xb5,0x24,0xaf,0x60,0x12,0x06,
                         0x2f,0xe0,0x37,0xa6}
    },
    {//[4]
        /* testname */  "PBKDF2 tv2.20",
        /* passwd */    "password",
        /* salt */      "salt",
        /* iteration */ 2,
        /* keklen */    20,
        /* kek */       {0xea,0x6c,0x01,0x4d,0xc7,0x2d,0x6f,0x8c,0xcd,0x1e,0xd9,0x2a,0xce,0x1d,0x41,0xf0,
                         0xd8,0xde,0x89,0x57}
    },
    {//[5]
        /* testname */  "PBKDF2 tv2.4096",
        /* passwd */    "password",
        /* salt */      "salt",
        /* iteration */ 4096,
        /* keklen */    20,
        /* kek */       {0x4b,0x00,0x79,0x01,0xb7,0x65,0x48,0x9a,0xbe,0xad,0x49,0xd9,0x26,0xf7,0x21,0xd0,
                         0x65,0xa4,0x29,0xc1}
    },
    {//[6]
        /* testname */  "PBKDF2 tv3.0",
        /* passwd */    "passwordPASSWORDpassword",
        /* salt */      "saltSALTsaltSALTsaltSALTsaltSALTsalt",
        /* iteration */ 4096,
        /* keklen */    25,
        /* kek */       {0x3d,0x2e,0xec,0x4f,0xe4,0x1c,0x84,0x9b,0x80,0xc8,0xd8,0x36,0x62,0xc0,0xe4,0x4a,
                         0x8b,0x29,0x1a,0x96,0x4c,0xf2,0xf0,0x70,0x38}
    },
};

void test_pbkdf2(
    CRYSPR_methods *cryspr_m,
    CRYSPR_cb *cryspr_cb,
    size_t tvi) //test vector index
{
    unsigned char kek[256/8];

    if(tvi < sizeof(pbkdf2_tv)/sizeof(pbkdf2_tv[0])) {
        struct UTVcryspr_pbkdf2 *tv = &pbkdf2_tv[tvi];

        ASSERT_NE(cryspr_m->km_pbkdf2, nullPtr);

        cryspr_m->km_pbkdf2(
            cryspr_cb,
            (char *)tv->passwd,         /* passphrase */
            strnlen(tv->passwd, 80),    /* passphrase len */
            (unsigned char *)tv->salt,  /* salt */
            strnlen(tv->salt, 80),      /* salt_len */
            tv->itr,                    /* iterations */
            tv->keklen,                 /* desired key len {(}16,24,32}*/
            kek);                       /* derived key */

        EXPECT_EQ(memcmp(kek, tv->kek, tv->keklen),0);
    }
}


TEST_F(TestCRYSPRcrypto, PBKDF2_tv1_k128)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 0);
}

TEST_F(TestCRYSPRcrypto, PBKDF2_tv1_k192)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 1);
}

TEST_F(TestCRYSPRcrypto, PBKDF2_tv1_k256)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 2);
}

TEST_F(TestCRYSPRcrypto, PBKDF2_tv2_i1)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 3);
}

TEST_F(TestCRYSPRcrypto, PBKDF2_tv2_i20)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 4);
}

TEST_F(TestCRYSPRcrypto, PBKDF2_tv2_i4096)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 5);
}

TEST_F(TestCRYSPRcrypto, PBKDF2_tv3_0)
{
    test_pbkdf2(cryspr_m, cryspr_cb, 6);
}

/*AES KeyWrap -----------------------------------------------------------------------------------*/

struct UTVcryspr_km_wrap {
    const char *name;
    unsigned char sek[256/8];       /* key to wrap (unwrap result)*/
    size_t seklen;
    unsigned char kek[256/8];
    unsigned char wrap[8+256/8];    /* wrapped sek (wrap result) */
};

/* KMWRAP/KMUNWRAP test vectors */
struct UTVcryspr_km_wrap UTV_cryspr_km_wrap[] = {
    {//[0]
        /*name */       "tv1.128",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    128/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79},
        /* wrap */      {0xF8,0xB6,0x12,0x1B,0xF2,0x03,0x62,0x40,0x80,0x32,0x60,0x8D,0xED,0x0B,0x8E,0x4B,
                         0x29,0x7E,0x80,0x17,0x4E,0x89,0x68,0xF1}
    },
    {//[1]
        /*name */       "tv1.192",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    192/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79,
                         0x90,0xab,0xca,0x6e,0xf0,0x02,0xf1,0xad},
        /* wrap */      {0xC1,0xA6,0x58,0x9E,0xC0,0x52,0x6D,0x37,0x84,0x3C,0xBD,0x3B,0x02,0xDD,0x79,0x3F,
                         0xE6,0x14,0x2D,0x81,0x69,0x4B,0x8E,0x07,0x26,0x4F,0xCD,0x86,0xD6,0x6A,0x70,0x62},
    },
    {//[2]
        /*name */       "tv1.256",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    256/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79,
                         0x90,0xab,0xca,0x6e,0xf0,0x02,0xf1,0xad,0x19,0x59,0xcf,0x18,0xac,0x91,0x53,0x3d},
        /* wrap */      {0x94,0xBE,0x9C,0xA6,0x7A,0x27,0x20,0x56,0xED,0xEA,0xA0,0x8F,0x71,0xB1,0xF1,0x85,
                         0xF6,0xC5,0x67,0xF4,0xA9,0xC2,0x1E,0x78,0x49,0x36,0xA5,0xAE,0x60,0xD0,0x1C,0x30,
                         0x68,0x27,0x4F,0x66,0x56,0x5A,0x55,0xAA},
    },
};

void test_kmwrap(
    CRYSPR_methods *cryspr_m,
    CRYSPR_cb *cryspr_cb,
    size_t tvi) //Test vector index
{
    unsigned char wrap[HAICRYPT_WRAPKEY_SIGN_SZ+256/8];
    int rc1,rc2;

    if (tvi < sizeof(UTV_cryspr_km_wrap)/sizeof(UTV_cryspr_km_wrap[0]))
    {
        struct UTVcryspr_km_wrap *tv = &UTV_cryspr_km_wrap[tvi];
        size_t wraplen=HAICRYPT_WRAPKEY_SIGN_SZ+tv->seklen;

        if(cryspr_m && cryspr_cb) {
            ASSERT_NE(cryspr_m->km_setkey, nullPtr);
            ASSERT_NE(cryspr_m->km_wrap, nullPtr);

            rc1 = cryspr_m->km_setkey(
                cryspr_cb,
                true,       //Wrap
                tv->kek,
                tv->seklen);

            rc2 = cryspr_m->km_wrap(
                cryspr_cb,
                wrap,
                tv->sek,
                tv->seklen);

            ASSERT_EQ(rc1, 0);
            ASSERT_EQ(rc2, 0);
            EXPECT_EQ(memcmp(tv->wrap, wrap, wraplen), 0);
        }
    }
}

void test_kmunwrap(
    CRYSPR_methods *cryspr_m,
    CRYSPR_cb *cryspr_cb,
    size_t tvi) //Test vector index
{
    unsigned char sek[256/8];
    int rc1,rc2;

    if(tvi < sizeof(UTV_cryspr_km_wrap)/sizeof(UTV_cryspr_km_wrap[0]))
    {
        struct UTVcryspr_km_wrap *tv = &UTV_cryspr_km_wrap[tvi];
        size_t wraplen=HAICRYPT_WRAPKEY_SIGN_SZ+tv->seklen;

        if(cryspr_m && cryspr_cb) {
            ASSERT_NE(cryspr_m->km_setkey, nullPtr);
            ASSERT_NE(cryspr_m->km_unwrap, nullPtr);

            rc1 = cryspr_m->km_setkey(
                cryspr_cb,
                false,       //Unwrap
                tv->kek,
                tv->seklen);

            rc2 = cryspr_m->km_unwrap(
                cryspr_cb,
                sek,
                tv->wrap,
                wraplen);

            ASSERT_EQ(rc1, 0);
            ASSERT_EQ(rc2, 0);
            EXPECT_EQ(memcmp(tv->sek, sek, tv->seklen), 0);
        }
    }
}


TEST_F(TestCRYSPRcrypto, KMWRAP_tv1_k128)
{
    test_kmwrap(cryspr_m, cryspr_cb, 0);
}
TEST_F(TestCRYSPRcrypto, KMWRAP_tv1_k192)
{
    test_kmwrap(cryspr_m, cryspr_cb, 1);
}
TEST_F(TestCRYSPRcrypto, KMWRAP_tv1_k256)
{
    test_kmwrap(cryspr_m, cryspr_cb, 2);
}

TEST_F(TestCRYSPRcrypto, KMUNWRAP_tv1_k128)
{
    test_kmunwrap(cryspr_m, cryspr_cb, 0);
}
TEST_F(TestCRYSPRcrypto, KMUNWRAP_tv1_k192)
{
    test_kmunwrap(cryspr_m, cryspr_cb, 1);
}
TEST_F(TestCRYSPRcrypto, KMUNWRAP_tv1_k256)
{
    test_kmunwrap(cryspr_m, cryspr_cb, 2);
}

/*AES ECB -----------------------------------------------------------------------------------*/
#if !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP)
/* AES-ECB test vectors */
struct UTVcryspr_aes_ecb {
    const char *name;
    unsigned char sek[256/8];       /* Stream Encrypting Key*/
    size_t seklen;
    const char *cleartxt;           /* clear text (decrypt result0 */
    unsigned char ciphertxt[32];    /* cipher text (encrypt result) */
    size_t outlen;
};

struct UTVcryspr_aes_ecb UTV_cryspr_aes_ecb[] = {
    {//[0]
        /*name */       "tv1.128",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    128/8,
        /* cleartxt */  "0000000000000000",
        /* ciphertxt */ {0xE0,0x86,0x82,0xBE,0x5F,0x2B,0x18,0xA6,0xE8,0x43,0x7A,0x15,0xB1,0x10,0xD4,0x18},
        /* cipherlen */ 16,
    },
    {//[1]
        /*name */       "tv1.192",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    192/8,
        /* cleartxt */  "0000000000000000",
        /* ciphertxt */ {0xCC,0xFE,0xD9,0x9E,0x38,0xE9,0x60,0xF5,0xD7,0xE1,0xC5,0x9F,0x56,0x3A,0x49,0x9D},
        /* cipherlen */ 16,
    },
    {//[2]
        /*name */       "tv1.256",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    256/8,
        /* cleartxt */  "0000000000000000",
        /* ciphertxt */ {0x94,0xB1,0x3A,0x9F,0x4C,0x09,0xD4,0xD7,0x00,0x2C,0x3F,0x11,0x7D,0xB1,0x7C,0x8B},
        /* cipherlen */ 16,
    },
    {//[3]
        /*name */       "tv2.128",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    128/8,
        /* cleartxt */  "00000000000000000000000000000000",
        /* ciphertxt */ {0xE0,0x86,0x82,0xBE,0x5F,0x2B,0x18,0xA6,0xE8,0x43,0x7A,0x15,0xB1,0x10,0xD4,0x18,
                         0xE0,0x86,0x82,0xBE,0x5F,0x2B,0x18,0xA6,0xE8,0x43,0x7A,0x15,0xB1,0x10,0xD4,0x18},
        /* cipherlen */ 32,
    },
    {//[4]
        /*name */       "tv2.192",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    192/8,
        /* cleartxt */  "00000000000000000000000000000000",
        /* ciphertxt */ {0xCC,0xFE,0xD9,0x9E,0x38,0xE9,0x60,0xF5,0xD7,0xE1,0xC5,0x9F,0x56,0x3A,0x49,0x9D,
                         0xCC,0xFE,0xD9,0x9E,0x38,0xE9,0x60,0xF5,0xD7,0xE1,0xC5,0x9F,0x56,0x3A,0x49,0x9D},
        /* cipherlen */ 32,
    },
    {//[5]
        /*name */       "tv2.256",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    256/8,
        /* cleartxt */  "00000000000000000000000000000000",
        /* ciphertxt */ {0x94,0xB1,0x3A,0x9F,0x4C,0x09,0xD4,0xD7,0x00,0x2C,0x3F,0x11,0x7D,0xB1,0x7C,0x8B,
                         0x94,0xB1,0x3A,0x9F,0x4C,0x09,0xD4,0xD7,0x00,0x2C,0x3F,0x11,0x7D,0xB1,0x7C,0x8B},
        /* cipherlen */ 32,
    },
};

void test_AESecb(
    CRYSPR_methods *cryspr_m,
    CRYSPR_cb *cryspr_cb,
    size_t tvi,
    bool bEncrypt)
{
    unsigned char result[128];
    unsigned char *intxt;
    unsigned char *outtxt;
    int rc1,rc2;

    if(tvi < sizeof(UTV_cryspr_aes_ecb)/sizeof(UTV_cryspr_aes_ecb[0]))
    {
        struct UTVcryspr_aes_ecb *tv = &UTV_cryspr_aes_ecb[tvi];
        size_t txtlen=strnlen((const char *)tv->cleartxt, 100);
        size_t outlen=sizeof(result);

        ASSERT_NE(cryspr_m->aes_set_key, nullPtr);
        ASSERT_NE(cryspr_m->aes_ecb_cipher, nullPtr);

        rc1 = cryspr_m->aes_set_key(
            HCRYPT_CTX_MODE_AESECB,
            bEncrypt,
            tv->sek,    /* Stream encrypting Key */
            tv->seklen,
            CRYSPR_GETSEK(cryspr_cb, 0));
        if(bEncrypt) {
            intxt=(unsigned char *)tv->cleartxt;
            outtxt=(unsigned char *)tv->ciphertxt;
        }else{
            intxt=(unsigned char *)tv->ciphertxt;
            outtxt=(unsigned char *)tv->cleartxt;
        }

        rc2 = cryspr_m->aes_ecb_cipher(
            bEncrypt,                   /* true:encrypt, false:decrypt */
            CRYSPR_GETSEK(cryspr_cb, 0),/* CRYpto Service PRovider AES Key context */
            intxt,                      /* src */
            txtlen,                     /* length */
            result,                     /* dest */
            &outlen);                   /* dest length */

        ASSERT_EQ(rc1, 0);
        ASSERT_EQ(rc2, 0);
        ASSERT_EQ(outlen, ((txtlen+(CRYSPR_AESBLKSZ-1))/CRYSPR_AESBLKSZ)*CRYSPR_AESBLKSZ);
        EXPECT_EQ(memcmp(outtxt, result, txtlen), 0);
    }
}


#define ENCRYPT true
#define DECRYPT false

TEST_F(TestCRYSPRcrypto, EncryptAESecb_tv1_128)
{
    test_AESecb(cryspr_m, cryspr_cb, 0, ENCRYPT);
}
TEST_F(TestCRYSPRcrypto, EncryptAESecb_tv1_192)
{
    test_AESecb(cryspr_m, cryspr_cb, 1, ENCRYPT);
}
TEST_F(TestCRYSPRcrypto, EncryptAESecb_tv1_256)
{
    test_AESecb(cryspr_m, cryspr_cb, 2, ENCRYPT);
}
TEST_F(TestCRYSPRcrypto, EncryptAESecb_tv2_128)
{
    test_AESecb(cryspr_m, cryspr_cb, 3, ENCRYPT);
}
TEST_F(TestCRYSPRcrypto, EncryptAESecb_tv2_192)
{
    test_AESecb(cryspr_m, cryspr_cb, 4, ENCRYPT);
}
TEST_F(TestCRYSPRcrypto, EncryptAESecb_tv2_256)
{
    test_AESecb(cryspr_m, cryspr_cb, 5, ENCRYPT);
}
TEST_F(TestCRYSPRcrypto, DecryptAESecb_tv1_128)
{
    test_AESecb(cryspr_m, cryspr_cb, 0, DECRYPT);
}
TEST_F(TestCRYSPRcrypto, DecryptAESecb_tv1_192)
{
    test_AESecb(cryspr_m, cryspr_cb, 1, DECRYPT);
}
TEST_F(TestCRYSPRcrypto, DecryptAESecb_tv1_256)
{
    test_AESecb(cryspr_m, cryspr_cb, 2, DECRYPT);
}
TEST_F(TestCRYSPRcrypto, DecryptAESecb_tv2_128)
{
    test_AESecb(cryspr_m, cryspr_cb, 3, DECRYPT);
}
TEST_F(TestCRYSPRcrypto, DecryptAESecb_tv2_192)
{
    test_AESecb(cryspr_m, cryspr_cb, 4, DECRYPT);
}
TEST_F(TestCRYSPRcrypto, DecryptAESecb_tv2_256)
{
    test_AESecb(cryspr_m, cryspr_cb, 5, DECRYPT);
}
#endif /* !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP) */

/*AES CTR -----------------------------------------------------------------------------------*/
#if CRYSPR_HAS_AESCTR

struct UTVcryspr_aes_ctr {
    const char *name;
    unsigned char sek[256/8];       /* Stream Encrypting Key*/
    size_t seklen;
    unsigned char iv[CRYSPR_AESBLKSZ];/* initial vector */
    const char *cleartxt;           /* clear text (decrypt result0 */
    unsigned char ciphertxt[24];    /* cipher text (encrypt result) */
};

/* AES-CTR test vectors */
struct UTVcryspr_aes_ctr UTV_cryspr_aes_ctr[] = {
    {//[0]
        /*name */       "tv1.128",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    128/8,
        /* iv */        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* cleartxt */  "000000000000000000000000",
        /* ciphertxt */ {0x56,0xD9,0x7B,0xE4,0xDF,0xBA,0x1C,0x0B,0xB8,0x7C,0xCA,0x69,0xFA,0x04,0x1B,0x1E,
                         0x68,0xD2,0xCC,0xFE,0xCA,0x4E,0x00,0x51},
    },
    {//[1]
        /*name */       "tv1.192",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    192/8,
        /* iv */        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* cleartxt */  "000000000000000000000000",
        /* ciphertxt */ {0x9A,0xD0,0x59,0xA2,0x9C,0x8F,0x62,0x93,0xD8,0xC4,0x99,0x5E,0xF9,0x00,0x3B,0xE7,
                         0xFD,0x03,0x82,0xBA,0xF7,0x43,0xC7,0x7B},
    },
    {//[2]
        /*name */       "tv1.256",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    256/8,
        /* iv */        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* cleartxt */  "000000000000000000000000",
        /* ciphertxt */ {0xEC,0xA5,0xF0,0x48,0x92,0x70,0xB9,0xB9,0x9D,0x78,0x92,0x24,0xA2,0xB4,0x10,0xB7,
                         0x63,0x3F,0xBA,0xCB,0xF7,0x75,0x06,0x89}
    },
};

void test_AESctr(
    CRYSPR_methods *cryspr_m,
    CRYSPR_cb *cryspr_cb,
    size_t tvi,
    bool bEncrypt)
{
    unsigned char result[100];
    unsigned char ivec[CRYSPR_AESBLKSZ];
    unsigned char *intxt;
    unsigned char *outtxt;
    int rc1,rc2;

    if(tvi < sizeof(UTV_cryspr_aes_ctr)/sizeof(UTV_cryspr_aes_ctr[0]))
    {
        struct UTVcryspr_aes_ctr *tv = &UTV_cryspr_aes_ctr[tvi];
        size_t txtlen=strnlen((const char *)tv->cleartxt, 100);

        ASSERT_NE(cryspr_m->aes_set_key, nullPtr);
        ASSERT_NE(cryspr_m->aes_ctr_cipher, nullPtr);

        rc1 = cryspr_m->aes_set_key(
            HCRYPT_CTX_MODE_AESCTR,
            true,       //For CTR, Encrypt key is used for both encryption and decryption
            tv->sek,    /* Stream encrypting Key */
            tv->seklen,
            CRYSPR_GETSEK(cryspr_cb, 0));
        if(bEncrypt) {
            intxt=(unsigned char *)tv->cleartxt;
            outtxt=(unsigned char *)tv->ciphertxt;
        }else{
            intxt=(unsigned char *)tv->ciphertxt;
            outtxt=(unsigned char *)tv->cleartxt;
        }

        memcpy(ivec, tv->iv, sizeof(ivec)); //cipher ivec not const
        rc2 = cryspr_m->aes_ctr_cipher(
            bEncrypt,                   /* true:encrypt, false:decrypt */
            CRYSPR_GETSEK(cryspr_cb, 0),/* CRYpto Service PRovider AES Key context */
            ivec,                       /* iv */
            intxt,                      /* src */
            txtlen,                     /* length */
            result);                    /* dest */

        ASSERT_EQ(rc1, 0);
        ASSERT_EQ(rc2, 0);
        EXPECT_EQ(memcmp(outtxt, result, txtlen), 0);
    }
}


#define ENCRYPT true
#define DECRYPT false

TEST_F(TestCRYSPRcrypto, EncryptAESctr_tv1_128)
{
    test_AESctr(cryspr_m, cryspr_cb, 0, ENCRYPT);
}
TEST_F(TestCRYSPRcrypto, EncryptAESctr_tv1_192)
{
    test_AESctr(cryspr_m, cryspr_cb, 1, ENCRYPT);
}
TEST_F(TestCRYSPRcrypto, EncryptAESctr_tv1_256)
{
    test_AESctr(cryspr_m, cryspr_cb, 2, ENCRYPT);
}
TEST_F(TestCRYSPRcrypto, DecryptAESctr_tv1_128)
{
    test_AESctr(cryspr_m, cryspr_cb, 0, DECRYPT);
}
TEST_F(TestCRYSPRcrypto, DecryptAESctr_tv1_192)
{
    test_AESctr(cryspr_m, cryspr_cb, 1, DECRYPT);
}
TEST_F(TestCRYSPRcrypto, DecryptAESctr_tv1_256)
{
    test_AESctr(cryspr_m, cryspr_cb, 2, DECRYPT);
}
#endif /* CRYSPR_HAS_AESCTR */

#endif /* SRT_ENABLE_ENCRYPTION */
