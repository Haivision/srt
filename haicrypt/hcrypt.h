/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>
 */

/*****************************************************************************
written by
   Haivision Systems Inc.

   2011-06-23 (jdube)
        HaiCrypt initial implementation.
   2014-03-11 (jdube)
        Adaptation for SRT.
   2014-03-26 (jsantiago)
        OS-X Build.
   2014-03-27 (jdube)
        Remove dependency on internal Crypto API.
   2016-07-22 (jsantiago)
        MINGW-W64 Build.
*****************************************************************************/

#ifndef HCRYPT_H
#define HCRYPT_H

#include <sys/types.h>

#ifdef WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #if defined(_MSC_VER)
      #pragma warning(disable:4267)
      #pragma warning(disable:4018)
   #endif
#else
   #include <sys/time.h>
#endif

#ifdef __GNUC__
#define ATR_UNUSED __attribute__((unused))
#else
#define ATR_UNUSED
#endif

#include "haicrypt.h"
#include "hcrypt_msg.h"
#include "hcrypt_ctx.h"

//#define HCRYPT_DEV 1  /* Development: should not be defined in committed code */

#ifdef HAICRYPT_SUPPORT_CRYPTO_API
/* See CRYPTOFEC_OBJECT in session structure */
#define CRYPTO_API_SERVER 1 /* Enable handler's structures */
#include "crypto_api.h"
#endif /* HAICRYPT_SUPPORT_CRYPTO_API */

typedef struct {
#ifdef HAICRYPT_SUPPORT_CRYPTO_API
        /* 
         * Resv matches internal upper layer handle (crypto_api)
         * They are not used in HaiCrypt.
         * This make 3 layers using the same handle.
         * To get rid of this dependency for a portable HaiCrypt,
         * revise caller (crypto_hc.c) to allocate its own buffer.
         */
        CRYPTOFEC_OBJECT    resv;           /* See above comment */
#endif /* HAICRYPT_SUPPORT_CRYPTO_API */

        hcrypt_Ctx          ctx_pair[2];    /* Even(0)/Odd(1) crypto contexts */
        hcrypt_Ctx *        ctx;            /* Current context */

        hcrypt_Cipher *     cipher;
        hcrypt_CipherData * cipher_data;

        unsigned char *     inbuf;          /* allocated if cipher has no getinbuf() func */
        size_t              inbuf_siz;

        int                 se;             /* Stream Encapsulation (HCRYPT_SE_xxx) */
        hcrypt_MsgInfo *    msg_info;

        struct {
            size_t          data_max_len;
        }cfg;

        struct {
            struct timeval  tx_period;      /* Keying Material tx period (milliseconds) */  
            struct timeval  tx_last;        /* Keying Material last tx time */
            unsigned int    refresh_rate;   /* SEK use period */
            unsigned int    pre_announce;   /* Pre/Post next/old SEK announce */
        }km;
} hcrypt_Session;


#define HCRYPT_LOG_INIT()
#define HCRYPT_LOG_EXIT()
#define HCRYPT_LOG(lvl, fmt, ...)

#ifdef  HCRYPT_DEV
#define HCRYPT_PRINTKEY(key, len, tag) HCRYPT_LOG(LOG_DEBUG, \
            "%s[%d]=0x%02x%02x..%02x%02x\n", tag, len, \
            (key)[0], (key)[1], (key)[(len)-2], (key)[(len)-1])
#else   /* HCRYPT_DEV */
#define HCRYPT_PRINTKEY(key,len,tag)
#endif  /* HCRYPT_DEV */

#ifndef ASSERT
#include <assert.h>
#define ASSERT(c)   assert(c)
#endif

#ifdef HAICRYPT_USE_OPENSSL_AES
#include <openssl/opensslv.h>   /* OPENSSL_VERSION_NUMBER  */
#include <openssl/evp.h>        /* PKCS5_ */
#include <openssl/rand.h>       /* RAND_bytes */
#include <openssl/aes.h>        /* AES_ */

#define hcrypt_Prng(rn, len)    (RAND_bytes(rn, len) <= 0 ? -1 : 0)

#if     (OPENSSL_VERSION_NUMBER < 0x0090808fL) //0.9.8h
        /*
        * AES_wrap_key()/AES_unwrap_key() introduced in openssl 0.9.8h
        * Use internal implementation (in hc_openssl_aes.c) for earlier versions
        */
int     AES_wrap_key(AES_KEY *key, const unsigned char *iv, unsigned char *out,
            const unsigned char *in, unsigned int inlen);
int     AES_unwrap_key(AES_KEY *key, const unsigned char *iv, unsigned char *out,
            const unsigned char *in, unsigned int inlen);
#endif  /* OPENSSL_VERSION_NUMBER */

#define hcrypt_WrapKey(kek, wrap, key, keylen) (((int)(keylen + HAICRYPT_WRAPKEY_SIGN_SZ) \
        == AES_wrap_key(kek, NULL, wrap, key, keylen)) ? 0 : -1)
#define hcrypt_UnwrapKey(kek, key, wrap, wraplen)   (((int)(wraplen - HAICRYPT_WRAPKEY_SIGN_SZ) \
        == AES_unwrap_key(kek, NULL, key, wrap, wraplen)) ? 0 : -1)

#else   /* HAICRYPT_USE_OPENSSL_AES */
#error  No Prng and key wrapper defined

#endif  /* HAICRYPT_USE_OPENSSL_AES */


#ifdef  HAICRYPT_USE_OPENSSL_EVP
#include <openssl/opensslv.h>   // OPENSSL_VERSION_NUMBER 

#define HAICRYPT_USE_OPENSSL_EVP_CTR 1
        /*
        * CTR mode is the default mode for HaiCrypt (standalone and SRT)
        */
#ifdef  HAICRYPT_USE_OPENSSL_EVP_CTR
#if     (OPENSSL_VERSION_NUMBER < 0x10001000L)
        /*
        * CTR mode for EVP API introduced in openssl 1.0.1
        * Implement it using ECB mode for earlier versions
        */
#define HAICRYPT_USE_OPENSSL_EVP_ECB4CTR 1  
#endif
        HaiCrypt_Cipher HaiCryptCipher_OpenSSL_EVP_CTR(void);
#endif

//undef HAICRYPT_USE_OPENSSL_EVP_CBC 1  /* CBC mode (for crypto engine tests) */
        /*
        * CBC mode for crypto engine tests
        * Default CTR mode not supported on Linux cryptodev (API to hardware crypto engines)
        * Not officially support nor interoperable with any haicrypt peer
        */
#ifdef  HAICRYPT_USE_OPENSSL_EVP_CBC
        HaiCrypt_Cipher HaiCryptCipher_OpenSSL_EVP_CBC(void);
#endif /* HAICRYPT_USE_OPENSSL_EVP_CBC */

#endif /* HAICRYPT_USE_OPENSSL_EVP */


/* HaiCrypt-TP CTR mode IV (128-bit):
 *    0   1   2   3   4   5  6   7   8   9   10  11  12  13  14  15
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * |                   0s                  |      pki      |  ctr  |
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *                            XOR                         
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * |                         nonce                         +
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * pki    (32-bit): packet index
 * ctr    (16-bit): block counter
 * nonce (112-bit): number used once (salt)
 */
#define hcrypt_SetCtrIV(pki, nonce, iv) do { \
            memset(&(iv)[0], 0, 128/8); \
            memcpy(&(iv)[10], (pki), HCRYPT_PKI_SZ); \
            hcrypt_XorStream(&(iv)[0], (nonce), 112/8); \
        } while(0)

#define hcrypt_XorStream(dst, strm, len) do { \
            int __XORSTREAMi; \
            for (__XORSTREAMi = 0 \
                ;__XORSTREAMi < (int)(len) \
                ;__XORSTREAMi += 1) { \
                (dst)[__XORSTREAMi] ^= (strm)[__XORSTREAMi]; \
            } \
        } while(0)


int hcryptCtx_SetSecret(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Secret *secret);
int hcryptCtx_GenSecret(hcrypt_Session *crypto, hcrypt_Ctx *ctx);

int hcryptCtx_Tx_Init(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Cfg *cfg);
int hcryptCtx_Tx_Rekey(hcrypt_Session *crypto, hcrypt_Ctx *ctx);
int hcryptCtx_Tx_Refresh(hcrypt_Session *crypto);
int hcryptCtx_Tx_PreSwitch(hcrypt_Session *crypto);
int hcryptCtx_Tx_Switch(hcrypt_Session *crypto);
int hcryptCtx_Tx_PostSwitch(hcrypt_Session *crypto);
int hcryptCtx_Tx_AsmKM(hcrypt_Session *crypto, hcrypt_Ctx *ctx, unsigned char *alt_sek);
int hcryptCtx_Tx_ManageKM(hcrypt_Session *crypto);
int hcryptCtx_Tx_InjectKM(hcrypt_Session *crypto, void *out_p[], size_t out_len_p[], int maxout);

int hcryptCtx_Rx_Init(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Cfg *cfg);
int hcryptCtx_Rx_ParseKM(hcrypt_Session *crypto, unsigned char *msg, size_t msg_len);

#endif /* HCRYPT_H */
