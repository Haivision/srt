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
*****************************************************************************/

#ifndef HCRYPT_CTX_H
#define HCRYPT_CTX_H

#include <sys/types.h>
#include "hcrypt.h"

#if !defined(HAISRT_VERSION_INT)
#include "haicrypt.h"
#include "hcrypt_msg.h"
#else
// Included by haisrt.h or similar
#include "haisrt/haicrypt.h"
#include "haisrt/hcrypt_msg.h"
#endif

typedef struct {
        unsigned char *pfx; //Prefix described by transport msg info (in ctx)
        unsigned char *payload;
        size_t len; //Payload size
}hcrypt_DataDesc;


typedef struct tag_hcrypt_Ctx {
        struct tag_hcrypt_Ctx * alt;    /* Alternative ctx (even/odd) */

#define HCRYPT_CTX_F_MSG        0x00FF  /* Aligned wiht message header flags */		
#define HCRYPT_CTX_F_eSEK       HCRYPT_MSG_F_eSEK
#define HCRYPT_CTX_F_oSEK       HCRYPT_MSG_F_oSEK
#define HCRYPT_CTX_F_xSEK       HCRYPT_MSG_F_xSEK

#define HCRYPT_CTX_F_ENCRYPT    0x0100  /* 0:decrypt 1:encrypt */
#define HCRYPT_CTX_F_ANNOUNCE   0x0200  /* Announce KM */
#define HCRYPT_CTX_F_TTSEND     0x0400  /* time to send */
        unsigned         flags;

#define HCRYPT_CTX_S_INIT       1
#define HCRYPT_CTX_S_SARDY      2   /* Security Association (KEK) ready */
#define HCRYPT_CTX_S_KEYED      3   /* Media Stream Encrypting Key (SEK) ready */
#define HCRYPT_CTX_S_ACTIVE     4   /* Announced and in use */
#define HCRYPT_CTX_S_DEPRECATED 5   /* Still announced but no longer used */
        unsigned         status;

#define HCRYPT_CTX_MODE_CLRTXT  0   /* NULL cipher (for tests) */
#define HCRYPT_CTX_MODE_AESECB  1   /* Electronic Code Book mode */
#define HCRYPT_CTX_MODE_AESCTR  2   /* Counter mode */
#define HCRYPT_CTX_MODE_AESCBC  3   /* Cipher-block chaining mode */
        unsigned         mode;

        struct {
            size_t       key_len;
            size_t       pwd_len;
            char         pwd[HAICRYPT_PWD_MAX_SZ];
        } cfg;

        size_t           salt_len;
        unsigned char    salt[HAICRYPT_SALT_SZ];

        size_t           sek_len;
        unsigned char    sek[HAICRYPT_KEY_MAX_SZ];

        AES_KEY          aes_kek;

        hcrypt_MsgInfo * msg_info;  /* Transport message handler */
        unsigned         pkt_cnt;   /* Key usage counter */

#define HCRYPT_CTX_MAX_KM_PFX_SZ   16
        size_t           KMmsg_len;
        unsigned char    KMmsg_cache[HCRYPT_CTX_MAX_KM_PFX_SZ + HCRYPT_MSG_KM_MAX_SZ];

#define HCRYPT_CTX_MAX_MS_PFX_SZ   16
        unsigned char    MSpfx_cache[HCRYPT_CTX_MAX_MS_PFX_SZ];
} hcrypt_Ctx;

typedef void *hcrypt_CipherData;

typedef struct {
        /*
        * open:
        * Create a cipher instance
        * Allocate output buffers 
        */
        hcrypt_CipherData *(*open)(
            size_t max_len);                                /* Maximum packet length that will be encrypted/decrypted */

        /*
        * close:
        * Release any cipher resources
        */
        int     (*close)(
            hcrypt_CipherData *crypto_data);                /* Cipher handle, internal data */

        /*
        * setkey:
        * Set the Odd or Even, Encryption or Decryption key.
        * Context (ctx) tells if it's for Odd or Even key (hcryptCtx_GetKeyIndex(ctx))
        * A Context flags (ctx->flags) also tells if this is an encryption or decryption context (HCRYPT_CTX_F_ENCRYPT)
        */
        int (*setkey)(
            hcrypt_CipherData *crypto_data,                 /* Cipher handle, internal data */
            hcrypt_Ctx *ctx,                                /* HaiCrypt Context (cipher, keys, Odd/Even, etc..) */
            unsigned char *key, size_t key_len);            /* New Key */

        /*
        * encrypt:
        * Submit a list of nbin clear transport packets (hcrypt_DataDesc *in_data) to encryption
        * returns *nbout encrypted data packets of length out_len_p[] into out_p[]
        *
        * If cipher implements deferred encryption (co-processor, async encryption),
        * it may return no encrypted packets, or encrypted packets for clear text packets of a previous call.  
        */
        int (*encrypt)(
            hcrypt_CipherData *crypto_data,                 /* Cipher handle, internal data */
            hcrypt_Ctx *ctx,                                /* HaiCrypt Context (cipher, keys, Odd/Even, etc..) */
            hcrypt_DataDesc *in_data, int nbin,             /* Clear text transport packets: header and payload */
            void *out_p[], size_t out_len_p[], int *nbout); /* Encrypted packets */

        /*
        * decrypt:
        * Submit a list of nbin encrypted transport packets (hcrypt_DataDesc *in_data) to decryption
        * returns *nbout clear text data packets of length out_len_p[] into out_p[]
        *
        * If cipher implements deferred decryption (co-processor, async encryption),
        * it may return no decrypted packets, or decrypted packets for encrypted packets of a previous call.
        */
        int (*decrypt)(
            hcrypt_CipherData *crypto_data,                 /* Cipher handle, internal data */
            hcrypt_Ctx *ctx,                                /* HaiCrypt Context (cipher, keys, Odd/Even, etc..) */
            hcrypt_DataDesc *in_data, int nbin,             /* Clear text transport packets: header and payload */
            void *out_p[], size_t out_len_p[], int *nbout); /* Encrypted packets */

        int     (*getinbuf)(hcrypt_CipherData *crypto_data, size_t hdr_len, size_t in_len, unsigned int pad_factor, unsigned char **in_pp);
} hcrypt_Cipher;

#define hcryptCtx_GetKeyFlags(ctx)      ((ctx)->flags & HCRYPT_CTX_F_xSEK)
#define hcryptCtx_GetKeyIndex(ctx)      (((ctx)->flags & HCRYPT_CTX_F_xSEK)>>1)

#endif /* HCRYPT_CTX_H */
