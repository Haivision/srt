/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
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

#ifndef INC_SRT_HCRYPT_H
#define INC_SRT_HCRYPT_H

#include <sys/types.h>

#ifdef _WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
#else
   #include <sys/time.h>
#endif

#ifdef __GNUC__
#define ATR_UNUSED __attribute__((unused))
#else
#define ATR_UNUSED
#endif

#include <string.h> // memcpy

#include "haicrypt.h"
#include "hcrypt_msg.h"
#include "hcrypt_ctx.h"
#include "cryspr.h"

//#define HCRYPT_DEV 1  /* Development: should not be defined in committed code */

#ifdef HAICRYPT_SUPPORT_CRYPTO_API
/* See CRYPTOFEC_OBJECT in session structure */
#define CRYPTO_API_SERVER 1 /* Enable handler's structures */
#include "crypto_api.h"
#endif /* HAICRYPT_SUPPORT_CRYPTO_API */

typedef struct hcrypt_Session_str {
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

        CRYSPR_methods *    cryspr;
        CRYSPR_cb *         cryspr_cb;

        unsigned char *     inbuf;          /* allocated if cipher has no getinbuf() func */
        size_t              inbuf_siz;

        int                 se;             /* Stream Encapsulation (HCRYPT_SE_xxx) */
        const hcrypt_MsgInfo * msg_info;

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

#if ENABLE_HAICRYPT_LOGGING
#include "haicrypt_log.h"
#else

#define HCRYPT_LOG_INIT()
#define HCRYPT_LOG_EXIT()
#define HCRYPT_LOG(lvl, fmt, ...)

#endif

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

inline static void hcrypt_XorStream(unsigned char* dst, const unsigned char* strm, size_t len)
{
    size_t i;
    for (i = 0; i < len; i += 1)
    {
        dst[i] ^= strm[i];
    }
}

typedef struct {
    size_t iv_size;
    size_t nonce_size;
    size_t pki_offset;
} hcrypt_IVLayout;

typedef enum {
    hcrypt_IV_Ctr = 0,
    hcrypt_IV_Gcm = 1,

    hcrypt_IVType_E_SIZE
} hcrypt_IVType;

inline static void hcrypt_SetIV(unsigned char* out_iv, hcrypt_Pki pki,
        const unsigned char* nonce, unsigned char role, hcrypt_IVType type)
{
    static const hcrypt_IVLayout layout[hcrypt_IVType_E_SIZE] = {
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
        {16, 14, 10},
/* HaiCrypt-TP GCM mode IV (96-bit) - SRT 1.5.4:
 *    0   1   2   3   4   5  6   7   8   9   10  11 
 * +---+---+---+---+---+---+---+---+---+---+---+---+
 * |                   0s          |      pki      |
 * +---+---+---+---+---+---+---+---+---+---+---+---+
 *                         XOR                   
 * +---+---+---+---+---+---+---+---+---+---+---+---+
 * |                      nonce                    +
 * +---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * pki   (32-bit): packet index
 * nonce (96-bit): number used once (salt)
 */
        {12, 12, 8}
    };
    memset(out_iv, 0, layout[type].iv_size);
    memcpy(out_iv + layout[type].pki_offset, &pki, sizeof pki);
    out_iv[0] = role;
    hcrypt_XorStream(out_iv, nonce, layout[type].nonce_size);
}

int hcryptCtx_SetSecret(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Secret *secret);
int hcryptCtx_GenSecret(hcrypt_Session *crypto, hcrypt_Ctx *ctx);

int hcryptCtx_Tx_Init(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Cfg *cfg);
int hcryptCtx_Tx_Rekey(hcrypt_Session *crypto, hcrypt_Ctx *ctx);
int hcryptCtx_Tx_CloneKey(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const hcrypt_Session* cryptoSrc);
int hcryptCtx_Tx_Refresh(hcrypt_Session *crypto);
int hcryptCtx_Tx_PreSwitch(hcrypt_Session *crypto);
int hcryptCtx_Tx_Switch(hcrypt_Session *crypto);
int hcryptCtx_Tx_PostSwitch(hcrypt_Session *crypto);
int hcryptCtx_Tx_AsmKM(hcrypt_Session *crypto, hcrypt_Ctx *ctx, unsigned char *alt_sek);
int hcryptCtx_Tx_ManageKM(hcrypt_Session *crypto);
int hcryptCtx_Tx_InjectKM(hcrypt_Session *crypto, void *out_p[], size_t out_len_p[], int maxout);

/// @brief Initialize receiving crypto context.
/// @param crypto library instance handle.
/// @param ctx additional crypto context.
/// @param cfg crypto configuration.
/// @return -1 on error, 0 otherwise.
int hcryptCtx_Rx_Init(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Cfg *cfg);

/// @brief Parse an incoming message related to cryptography module.
/// @param crypto library instance handle.
/// @param msg a message to parse.
/// @param msg_len length of the message in bytes.
/// @return 0 on success; -3 on cipher mode mismatch; -2 on unmatched shared secret; -1 on other failures.
int hcryptCtx_Rx_ParseKM(hcrypt_Session *crypto, unsigned char *msg, size_t msg_len);

#endif /* HCRYPT_H */
