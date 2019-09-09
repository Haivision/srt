#include <stdio.h>
#include <string.h>
#include "gtest/gtest.h"
#include "common.h"
#include "hcrypt.h"
#include "version.h"


#if (CRYSPR_VERSION_NUMBER >= 0x010100)
#define WITH_FIPSMODE 1 /* 1: openssl-evp branch */
#endif

#define UT_PKT_MAXLEN 1500

CRYSPR_methods *g_cryspr_m = NULL;   /* methods */
CRYSPR_methods cryspr_fb, *cryspr_fbm = NULL; /* fall back methods */
CRYSPR_cb *cryspr_cb = NULL;       /* Control block */

void *nullPtr = NULL;

/* cryspr_meth: Test presense of required cryspr methods */

TEST(cryspr_meth, init)
{
    g_cryspr_m = cryspr4SRT();
    cryspr_fbm = crysprInit(&cryspr_fb);

    EXPECT_NE(g_cryspr_m, nullPtr);
}

#if WITH_FIPSMODE
TEST(cryspr_meth, fipsmode)
{
    EXPECT_NE(g_cryspr_m, nullPtr);
    if(g_cryspr_m->fips_mode_set == NULL || g_cryspr_m->fips_mode_set == cryspr_fbm->fips_mode_set ) {
#if defined(CRYSPR_FIPSMODE)    //undef: not supported, 0: supported and Off by default, 1: enabled by default
    EXPECT_NE(g_cryspr_m, cryspr_fbm->fips_mode_set); //Fallback method cannot set FIPS mode
    EXPECT_EQ(g_cryspr_m->fips_mode_set(CRYSPR_FIPSMODE ? 0 : 1), CRYSPR_FIPSMODE);
    EXPECT_EQ(g_cryspr_m->fips_mode_set(CRYSPR_FIPSMODE), (CRYSPR_FIPSMODE? 0 : 1));
#endif /* CRYSPR_FIPSMODE */
    }
}
#endif /* WITH_FIPSMODE */

TEST(cryspr_meth, open)
{
    EXPECT_NE(g_cryspr_m, nullPtr);
    EXPECT_NE(g_cryspr_m->open, nullPtr);
}

TEST(cryspr_meth, close)
{
    EXPECT_NE(g_cryspr_m, nullPtr);
    EXPECT_NE(g_cryspr_m->close, nullPtr);
}

TEST(cryspr_meth, prng)
{
    EXPECT_NE(g_cryspr_m, nullPtr);
    EXPECT_NE(g_cryspr_m->prng, nullPtr);
}
TEST(cryspr_meth, aes_set)
{
    EXPECT_NE(g_cryspr_m, nullPtr);
    EXPECT_NE(g_cryspr_m->aes_set_key, nullPtr);
}
TEST(cryspr_meth, ecb)
{
    EXPECT_NE(g_cryspr_m, nullPtr);
    if( g_cryspr_m->km_wrap == NULL || g_cryspr_m->km_wrap == cryspr_fbm->km_wrap) {
        /* fallback KM_WRAP method used
         * it requires the AES-ECB method
         */
        EXPECT_NE(g_cryspr_m->aes_ecb_cipher, nullPtr);
        EXPECT_NE(g_cryspr_m->aes_ecb_cipher, cryspr_fbm->aes_ecb_cipher);
    }
}
TEST(cryspr_meth, ctr)
{
    EXPECT_NE(g_cryspr_m, nullPtr);
    EXPECT_NE(g_cryspr_m->aes_ctr_cipher, nullPtr);
}

TEST(cryspr_meth, sha1)
{
    EXPECT_NE(g_cryspr_m, nullPtr);
    if(g_cryspr_m->sha1_msg_digest == NULL || g_cryspr_m->sha1_msg_digest == cryspr_fbm->sha1_msg_digest ) {
        /* NULL or fallback SHA1 method
         * Error if fallback PBKDF2 is needed
         */
        EXPECT_NE(g_cryspr_m->km_pbkdf2, cryspr_fbm->km_pbkdf2);
    }
}



/* CRYSPR control block test */

TEST(cryspr_cb, open)
{
    EXPECT_NE(g_cryspr_m, nullPtr);
    EXPECT_NE(g_cryspr_m->open, nullPtr);

    cryspr_cb = g_cryspr_m->open(g_cryspr_m, UT_PKT_MAXLEN);
    EXPECT_NE(cryspr_cb, nullPtr);
    EXPECT_EQ(g_cryspr_m, cryspr_cb->cryspr); //methods set in control block
}

TEST(cryspr_cb, close)
{
    EXPECT_NE(g_cryspr_m, nullPtr);
    EXPECT_NE(g_cryspr_m->close, nullPtr);

    EXPECT_EQ(g_cryspr_m->close(cryspr_cb), 0);
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

void test_pbkdf2(struct UTVcryspr_pbkdf2 *tv)
{
    unsigned char kek[256/8];

    EXPECT_NE(g_cryspr_m, nullPtr);
    cryspr_cb = g_cryspr_m->open(g_cryspr_m, UT_PKT_MAXLEN);
    EXPECT_NE(cryspr_cb, nullPtr);

    g_cryspr_m->km_pbkdf2(
        cryspr_cb,
        (char *)tv->passwd,         /* passphrase */
        strnlen(tv->passwd, 80),    /* passphrase len */
        (unsigned char *)tv->salt,  /* salt */
        strnlen(tv->salt, 80),      /* salt_len */
        tv->itr,                    /* iterations */
        tv->keklen,                 /* desired key len {(}16,24,32}*/
        kek);                       /* derived key */

     EXPECT_EQ(memcmp(kek, tv->kek, tv->keklen),0);
     EXPECT_EQ(g_cryspr_m->close(cryspr_cb), 0);
}

/* PBKDF2 test vectors */
struct UTVcryspr_pbkdf2 pbkdf2_tv[] = {
    {//0
        /* testname */  "PBKDF2 tv1.128",
        /* passwd */    "000000000000",
        /* salt */      "00000000",
        /* iteration */ 2048,
        /* keklen */    128/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79}
    },
    {//1
        /* testname */  "PBKDF2 tv1.192",
        /* passwd */    "000000000000",
        /* salt */      "00000000",
        /* iteration */ 2048,
        /* keklen */    192/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79,
                         0x90,0xab,0xca,0x6e,0xf0,0x02,0xf1,0xad}
    },
    {//2
        /* testname */  "PBKDF2 tv1.256",
        /* passwd */    "000000000000",
        /* salt */      "00000000",
        /* iteration */ 2048,
        /* keklen */    256/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79,
                         0x90,0xab,0xca,0x6e,0xf0,0x02,0xf1,0xad,0x19,0x59,0xcf,0x18,0xac,0x91,0x53,0x3d}
    },
    {//3
        /* testname */  "PBKDF2 tv2.1",
        /* passwd */    "password",
        /* salt */      "salt",
        /* iteration */ 1,
        /* keklen */    20,
        /* kek */       {0x0c,0x60,0xc8,0x0f,0x96,0x1f,0x0e,0x71,0xf3,0xa9,0xb5,0x24,0xaf,0x60,0x12,0x06,
                         0x2f,0xe0,0x37,0xa6}
    },
    {//4
        /* testname */  "PBKDF2 tv2.20",
        /* passwd */    "password",
        /* salt */      "salt",
        /* iteration */ 2,
        /* keklen */    20,
        /* kek */       {0xea,0x6c,0x01,0x4d,0xc7,0x2d,0x6f,0x8c,0xcd,0x1e,0xd9,0x2a,0xce,0x1d,0x41,0xf0,
                         0xd8,0xde,0x89,0x57}
    },
    {//5
        /* testname */  "PBKDF2 tv2.4096",
        /* passwd */    "password",
        /* salt */      "salt",
        /* iteration */ 4096,
        /* keklen */    20,
        /* kek */       {0x4b,0x00,0x79,0x01,0xb7,0x65,0x48,0x9a,0xbe,0xad,0x49,0xd9,0x26,0xf7,0x21,0xd0,
                         0x65,0xa4,0x29,0xc1}
    },
    {//6
        /* testname */  "PBKDF2 tv3.0",
        /* passwd */    "passwordPASSWORDpassword",
        /* salt */      "saltSALTsaltSALTsaltSALTsaltSALTsalt",
        /* iteration */ 4096,
        /* keklen */    25,
        /* kek */       {0x3d,0x2e,0xec,0x4f,0xe4,0x1c,0x84,0x9b,0x80,0xc8,0xd8,0x36,0x62,0xc0,0xe4,0x4a,
                         0x8b,0x29,0x1a,0x96,0x4c,0xf2,0xf0,0x70,0x38}
    },
};

TEST(PBKDF2, tv1_k128)
{
    test_pbkdf2(&pbkdf2_tv[0]);
}

TEST(PBKDF2, tv1_k192)
{
    test_pbkdf2(&pbkdf2_tv[1]);
}

TEST(PBKDF2, tv1_k256)
{
    test_pbkdf2(&pbkdf2_tv[2]);
}

TEST(PBKDF2, tv2_i1)
{
    test_pbkdf2(&pbkdf2_tv[3]);
}

TEST(PBKDF2, tv2_i20)
{
    test_pbkdf2(&pbkdf2_tv[4]);
}

TEST(PBKDF2, tv2_i4096)
{
    test_pbkdf2(&pbkdf2_tv[5]);
}

TEST(PBKDF2, tv3_0)
{
    test_pbkdf2(&pbkdf2_tv[6]);
}

/*AES KeyWrap -----------------------------------------------------------------------------------*/

struct UTVcryspr_km_wrap {
    const char *name;
    unsigned char sek[256/8];       /* key to wrap (unwrap result)*/
    size_t seklen;
    unsigned char kek[256/8];
    unsigned char wrap[8+256/8];    /* wrapped sek (wrap result) */
};

void test_kmwrap(struct UTVcryspr_km_wrap *tv)
{
    unsigned char wrap[HAICRYPT_WRAPKEY_SIGN_SZ+256/8];
    size_t wraplen=HAICRYPT_WRAPKEY_SIGN_SZ+tv->seklen;
    int rc1,rc2;

    EXPECT_NE(g_cryspr_m, nullPtr);
    cryspr_cb = g_cryspr_m->open(g_cryspr_m, UT_PKT_MAXLEN);
    EXPECT_NE(cryspr_cb, nullPtr);


    rc1 = g_cryspr_m->km_setkey(
        cryspr_cb,
        true,       //Wrap
        tv->kek,
        tv->seklen);

    rc2 = g_cryspr_m->km_wrap(
        cryspr_cb,
        wrap,
        tv->sek,
        tv->seklen);

    EXPECT_EQ(rc1, 0);
    EXPECT_EQ(rc2, 0);
    EXPECT_EQ(memcmp(tv->wrap, wrap, wraplen), 0);
    EXPECT_EQ(g_cryspr_m->close(cryspr_cb), 0);
}

void test_kmunwrap(struct UTVcryspr_km_wrap *tv)
{
    CRYSPR_cb *cryspr_cb;       /* Control block */
    unsigned char sek[256/8];
    size_t wraplen=HAICRYPT_WRAPKEY_SIGN_SZ+tv->seklen;
    int rc1,rc2;

    EXPECT_NE(g_cryspr_m, nullPtr);
    cryspr_cb = g_cryspr_m->open(g_cryspr_m, UT_PKT_MAXLEN);
    EXPECT_NE(cryspr_cb, nullPtr);


    rc1 = g_cryspr_m->km_setkey(
        cryspr_cb,
        false,       //Unwrap
        tv->kek,
        tv->seklen);

    rc2 = g_cryspr_m->km_unwrap(
        cryspr_cb,
        sek,
        tv->wrap,
        wraplen);

    EXPECT_EQ(rc1, 0);
    EXPECT_EQ(rc2, 0);
    EXPECT_EQ(memcmp(tv->sek, sek, tv->seklen), 0);
    EXPECT_EQ(g_cryspr_m->close(cryspr_cb), 0);
}

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
        /*name */       "tv1.128b",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    128/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79},
        /* wrap */      {0xF8,0xB6,0x12,0x1B,0xF2,0x03,0x62,0x40,0x80,0x32,0x60,0x8D,0xED,0x0B,0x8E,0x4B,
                         0x29,0x7E,0x80,0x17,0x4E,0x89,0x68,0xF1}
    },
    {//[2]
        /*name */       "tv1.192",
        /* sek */       {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        /* seklen */    192/8,
        /* kek */       {0xb6,0xbf,0x5f,0x0c,0xdd,0x25,0xe8,0x58,0x23,0xfd,0x84,0x7a,0xb2,0xb6,0x7f,0x79,
                         0x90,0xab,0xca,0x6e,0xf0,0x02,0xf1,0xad},
        /* wrap */      {0xC1,0xA6,0x58,0x9E,0xC0,0x52,0x6D,0x37,0x84,0x3C,0xBD,0x3B,0x02,0xDD,0x79,0x3F,
                         0xE6,0x14,0x2D,0x81,0x69,0x4B,0x8E,0x07,0x26,0x4F,0xCD,0x86,0xD6,0x6A,0x70,0x62},
    },
    {//[3]
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

TEST(KMWRAP, tv1_128)
{
    test_kmwrap(&UTV_cryspr_km_wrap[0]);
}
TEST(KMWRAP, tv1_128b)
{
    test_kmwrap(&UTV_cryspr_km_wrap[1]);
}
TEST(KMWRAP, tv1_192)
{
    test_kmwrap(&UTV_cryspr_km_wrap[2]);
}
TEST(KMWRAP, tv1_256)
{
    test_kmwrap(&UTV_cryspr_km_wrap[3]);
}

TEST(KMUNWRAP, tv1_128)
{
    test_kmunwrap(&UTV_cryspr_km_wrap[0]);
}
TEST(KMUNWRAP, tv1_128b)
{
    test_kmunwrap(&UTV_cryspr_km_wrap[1]);
}
TEST(KMUNWRAP, tv1_192)
{
    test_kmunwrap(&UTV_cryspr_km_wrap[2]);
}
TEST(KMUNWRAP, tv1_256)
{
    test_kmunwrap(&UTV_cryspr_km_wrap[3]);
}


/*AES CTR -----------------------------------------------------------------------------------*/

struct UTVcryspr_aes_ctr {
    const char *name;
    unsigned char sek[256/8];       /* Stream Encrypting Key*/
    size_t seklen;
    unsigned char iv[CRYSPR_AESBLKSZ];/* initial vector */
    const char *cleartxt;           /* clear text (decrypt result0 */
    unsigned char ciphertxt[24];    /* cipher text (encrypt result) */
};

void test_AESctr(struct UTVcryspr_aes_ctr *tv, bool bEncrypt)
{
    CRYSPR_cb *cryspr_cb = NULL;
    unsigned char result[100];
    unsigned char ivec[CRYSPR_AESBLKSZ];
    size_t txtlen=strnlen((const char *)tv->cleartxt, 100);
    unsigned char *intxt;
    unsigned char *outtxt;
    int rc1,rc2;

    EXPECT_NE(g_cryspr_m, nullPtr);
    cryspr_cb = g_cryspr_m->open(g_cryspr_m, UT_PKT_MAXLEN);
    EXPECT_NE(cryspr_cb, nullPtr);

    rc1 = g_cryspr_m->aes_set_key(
        true,       //For CTR, Encrypt key is used for both encryption and decryption
        tv->sek,    /* Stream encrypting Key */
        tv->seklen,
#if WITH_FIPSMODE
        cryspr_cb->aes_sek[0]);
#else
        &cryspr_cb->aes_sek[0]);
#endif
    if(bEncrypt) {
        intxt=(unsigned char *)tv->cleartxt;
        outtxt=(unsigned char *)tv->ciphertxt;
    }else{
        intxt=(unsigned char *)tv->ciphertxt;
        outtxt=(unsigned char *)tv->cleartxt;
    }

    memcpy(ivec, tv->iv, sizeof(ivec)); //cipher ivec not const
    rc2 = g_cryspr_m->aes_ctr_cipher(
        bEncrypt,                   /* true:encrypt, false:decrypt */
#if WITH_FIPSMODE
        cryspr_cb->aes_sek[0],      /* CRYpto Service PRovider AES Key context */
#else
        &cryspr_cb->aes_sek[0],      /* CRYpto Service PRovider AES Key context */
#endif
        ivec,                       /* iv */
        intxt,                      /* src */
        txtlen,                     /* length */
        result);                    /* dest */

    EXPECT_EQ(rc1, 0);
    EXPECT_EQ(rc2, 0);
    EXPECT_EQ(memcmp(outtxt, result, txtlen), 0);
    EXPECT_EQ(g_cryspr_m->close(cryspr_cb), 0);
}

/* PBKDF2 test vectors */
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

#define ENCRYPT true
#define DECRYPT false

TEST(EncryptAESctr, tv1_128)
{
    test_AESctr(&UTV_cryspr_aes_ctr[0], ENCRYPT);
}
TEST(EncryptAESctr, tv1_192)
{
    test_AESctr(&UTV_cryspr_aes_ctr[1], ENCRYPT);
}
TEST(EncryptAESctr, tv1_256)
{
    test_AESctr(&UTV_cryspr_aes_ctr[2], ENCRYPT);
}
TEST(DecryptAESctr, tv1_128)
{
    test_AESctr(&UTV_cryspr_aes_ctr[0], DECRYPT);
}
TEST(DecryptAESctr, tv1_192)
{
    test_AESctr(&UTV_cryspr_aes_ctr[1], DECRYPT);
}
TEST(DecryptAESctr, tv1_256)
{
    test_AESctr(&UTV_cryspr_aes_ctr[2], DECRYPT);
}

