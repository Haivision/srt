/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


/*****************************************************************************
written by
   Haivision Systems Inc.

   2022-05-19 (jdube)
        CRYSPR2 adaptation
   2019-06-27 (jdube)
        MBedTLS CRYSPR/4SRT (CRYypto Service PRovider for SRT)
*****************************************************************************/

#include "hcrypt.h"

#include <string.h>

#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/entropy.h>

// Static members of cryspr::mbedtls class.
static mbedtls_ctr_drbg_context crysprMbedtls_ctr_drbg;
static mbedtls_entropy_context crysprMbedtls_entropy;

typedef struct tag_crysprMBedTLS_AES_cb {
        CRYSPR_cb       ccb;        /* CRYSPR control block */
        /* Add other cryptolib specific data here */
#ifdef CRYSPR2
        CRYSPR_AESCTX   aes_kek_buf;		/* Key Encrypting Key (KEK) */
        CRYSPR_AESCTX   aes_sek_buf[2];		/* even/odd Stream Encrypting Key (SEK) */
#endif
} crysprMbedtls_cb;


int crysprMbedtls_Prng(unsigned char *rn, int len)
{
    int ret = mbedtls_ctr_drbg_random( &crysprMbedtls_ctr_drbg, rn, len );
    if (ret != 0)
    {
        return -1;
    }

    return 0;
}

int crysprMbedtls_AES_SetKey(
        int cipher_type,            /* One of HCRYPT_CTX_MODE_[CLRTXT|AESECB|AESCTR] */
        bool bEncrypt,              /* true:encrypt key, false:decrypt key*/
        const unsigned char *kstr,  /* key string */
        size_t kstr_len,            /* kstr length in  bytes (16, 24, or 32 bytes, for AES128,AES192, or AES256) */
        CRYSPR_AESCTX *aes_key)     /* Cryptolib Specific AES key context */
{
    (void)cipher_type;

    if (!(kstr_len == 16 || kstr_len == 24 || kstr_len == 32)) {
        HCRYPT_LOG(LOG_ERR, "%s", "AES_set_encrypt_key(kek) bad length\n");
        return -1;
    }

    int ret;
#ifdef CRYSPR2
    (void)cipher_type;
#endif
    // mbedtls uses the "bits" convention (128, 192, 254), just like openssl.
    // kstr_len is in "bytes" convention (16, 24, 32).

    if (bEncrypt) {        /* Encrypt key */
        ret = mbedtls_aes_setkey_enc(aes_key, kstr, (unsigned int)kstr_len*8);
    } else {               /* Decrypt key */
        ret = mbedtls_aes_setkey_dec(aes_key, kstr, (unsigned int)kstr_len*8);
    }

    return ret == 0 ? 0 : -1;
}

int crysprMbedtls_AES_EcbCipher( /* AES Electronic Codebook cipher*/
    bool bEncrypt,              /* true:encrypt, false:decrypt */
    CRYSPR_AESCTX *aes_key,     /* CryptoLib AES context */
    const unsigned char *indata,/* src (clear text)*/
    size_t inlen,               /* length */
    unsigned char *out_txt,     /* dst (cipher text) */
    size_t *outlen)             /* dst len */
{
    int nblk = (int)(inlen/CRYSPR_AESBLKSZ);
    int nmore = (int)(inlen%CRYSPR_AESBLKSZ);
    int i;

    if (bEncrypt) {
        /* Encrypt packet payload, block by block, in output buffer */
        for (i = 0; i < nblk; i++) {
            // NOTE: CRYSPR_AESBLKSZ is implicitly the ONLY POSSIBLE
            // size of the block.
            mbedtls_aes_crypt_ecb(aes_key, MBEDTLS_AES_ENCRYPT,
                    &indata[(i*CRYSPR_AESBLKSZ)],
                    &out_txt[(i*CRYSPR_AESBLKSZ)]);
        }
        /* Encrypt last incomplete block */
        if (0 < nmore) {
            unsigned char intxt[CRYSPR_AESBLKSZ];

            memcpy(intxt, &indata[(nblk*CRYSPR_AESBLKSZ)], nmore);
            memset(intxt+nmore, 0, CRYSPR_AESBLKSZ-nmore);
            mbedtls_aes_crypt_ecb(aes_key, MBEDTLS_AES_ENCRYPT,
                    intxt,
                    &out_txt[(nblk*CRYSPR_AESBLKSZ)]);
            nblk++;
        }
        if (outlen != NULL) *outlen = nblk*CRYSPR_AESBLKSZ;
    } else { /* Decrypt */
        for (i=0; i<nblk; i++){
            mbedtls_aes_crypt_ecb(aes_key, MBEDTLS_AES_DECRYPT,
                    &indata[(i*CRYSPR_AESBLKSZ)],
                    &out_txt[(i*CRYSPR_AESBLKSZ)]);
        }
        /* Encrypt last incomplete block */
        if (0 < nmore) {
            //shall not happens in decrypt
        }
        if (outlen != NULL) *outlen = nblk*CRYSPR_AESBLKSZ;
    }
    return 0;
}

int crysprMbedtls_AES_CtrCipher( /* AES-CTR128 Encryption */
    bool bEncrypt,              /* true:encrypt, false:decrypt */
    CRYSPR_AESCTX *aes_key,     /* CryptoLib AES context */
    unsigned char *iv,          /* iv */
    const unsigned char *indata,/* src */
    size_t inlen,               /* src length */
    unsigned char *out_txt)     /* dest buffer[inlen] */
{
    unsigned char ctr[CRYSPR_AESBLKSZ];
    size_t blk_ofs = 0;
    (void)bEncrypt;             /* CTR mode encrypt for both encryption and decryption */

    memset(&ctr[0], 0, sizeof(ctr));

    mbedtls_aes_crypt_ctr(aes_key,
            inlen,
            &blk_ofs,
            iv,
            ctr,
            indata,
            out_txt);

    return 0;
}

#ifdef CRYSPR2
static CRYSPR_cb *crysprMbedtls_Open(CRYSPR_methods *cryspr, size_t max_len)
{
    crysprMbedtls_cb *aes_data;

    aes_data = (crysprMbedtls_cb *)crysprHelper_Open(cryspr, sizeof(crysprMbedtls_cb), max_len);
    if (NULL == aes_data) {
        HCRYPT_LOG(LOG_ERR, "crysprHelper_Open(%p, %zd, %zd) failed\n", cryspr, sizeof(crysprMbedtls_cb), max_len);
        return(NULL);
    }

    aes_data->ccb.aes_kek = &aes_data->aes_kek_buf; //key encrypting key
    aes_data->ccb.aes_sek[0] = &aes_data->aes_sek_buf[0]; //stream encrypting key
    aes_data->ccb.aes_sek[1] = &aes_data->aes_sek_buf[1]; //stream encrypting key

    return(&aes_data->ccb);
}

static int crysprMbedtls_Close(CRYSPR_cb *cryspr_cb)
{
    return(crysprHelper_Close(cryspr_cb));
}
#endif /* CRYSPR2 */

/*
* Password-based Key Derivation Function
*/
int crysprMbedtls_KmPbkdf2(
    CRYSPR_cb *cryspr_cb,
    char *passwd,           /* passphrase */
    size_t passwd_len,      /* passphrase len */
    unsigned char *salt,    /* salt */
    size_t salt_len,        /* salt_len */
    int itr,                /* iterations */
    size_t key_len,         /* key_len */
    unsigned char *out)     /* derived key buffer[key_len]*/
{
    (void)cryspr_cb;

    const mbedtls_md_info_t* ifo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if ( ifo == NULL ) {
        // XXX report error, log?
        return -1;
    }

    mbedtls_md_context_t mdctx;
    mbedtls_md_init(&mdctx);

    const int yes_use_hmac = 1;
    int ret;
    if ( (ret = mbedtls_md_setup(&mdctx, ifo, yes_use_hmac)) != 0 ) {
        mbedtls_md_free(&mdctx);

        // XXX report error, log?
        return ret;
    }

    ret = mbedtls_pkcs5_pbkdf2_hmac(&mdctx,
            (unsigned char*)passwd, passwd_len, salt, salt_len,
            itr, (uint32_t)key_len, out);

    mbedtls_md_free(&mdctx);

    if (ret == 0)
        return 0;

    // XXX report error, log?
    return -1;
}

static CRYSPR_methods crysprMbedtls_methods;

CRYSPR_methods *crysprMbedtls(void)
{
    if (crysprMbedtls_methods.open)
        return(&crysprMbedtls_methods);

    crysprInit(&crysprMbedtls_methods); /* Set default methods */

    /* CryptoLib Primitive API */
    crysprMbedtls_methods.prng           = crysprMbedtls_Prng;
    crysprMbedtls_methods.aes_set_key    = crysprMbedtls_AES_SetKey;
#if CRYSPR_HAS_AESCTR
    crysprMbedtls_methods.aes_ctr_cipher = crysprMbedtls_AES_CtrCipher;
#endif
#if !(CRYSPR_HAS_AESCTR && CRYSPR_HAS_AESKWRAP)
    /* AES-ECB only required if cryspr has no AES-CTR or no AES KeyWrap */
    crysprMbedtls_methods.aes_ecb_cipher = crysprMbedtls_AES_EcbCipher;
#endif
#if !CRYSPR_HAS_PBKDF2
    crysprMbedtls_methods.sha1_msg_digest= crysprMbedtls_SHA1_MsgDigest; //Onl required if using generic KmPbkdf2
#endif

    //--Crypto Session (Top API)
#ifdef CRYSPR2
        crysprMbedtls_methods.open     = crysprMbedtls_Open;
        crysprMbedtls_methods.close    = crysprMbedtls_Close;
#else
    //  crysprMbedtls_methods.open     =
    //  crysprMbedtls_methods.close    =
#endif
    //--Keying material (km) encryption
    crysprMbedtls_methods.km_pbkdf2  = crysprMbedtls_KmPbkdf2;
    //	crysprMbedtls_methods.km_setkey  =
    //  crysprMbedtls_methods.km_wrap    =
    //  crysprMbedtls_methods.km_unwrap  =
    //--Media stream (ms) encryption
    //  crysprMbedtls_methods.ms_setkey  =
    //	crysprMbedtls_methods.ms_encrypt =
    //	crysprMbedtls_methods.ms_decrypt =

    // Initialize extra static data
    mbedtls_entropy_init( &crysprMbedtls_entropy );
    mbedtls_ctr_drbg_init( &crysprMbedtls_ctr_drbg );

    int ret;
    if ( (ret = mbedtls_ctr_drbg_seed( &crysprMbedtls_ctr_drbg, mbedtls_entropy_func,
                    &crysprMbedtls_entropy, NULL, 0)) != 0 )
    {
        HCRYPT_LOG(LOG_CRIT, "crysprMbedtls: STATIC INIT FAILED on mbedtls_ctr_drbg_init: -0x%04x", -ret);
        return NULL;
    }

    return(&crysprMbedtls_methods);
}



