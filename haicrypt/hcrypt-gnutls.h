/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*
 * Author(s):
 *     Justin Kim <justin.kim@collabora.com>
 */ 

#ifndef __HCRYPT_GNUTLS_H__
#define __HCRYPT_GNUTLS_H__

#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>

#include <nettle/aes.h>
#include <nettle/ctr.h>
#include <nettle/pbkdf2.h>

typedef struct aes_ctx AES_KEY;

#define hcrypt_Prng(rn,len) (gnutls_rnd(GNUTLS_RND_KEY,(rn),(len)) < 0 ? -1 : 0)

#define hcrypt_pbkdf2_hmac_sha1(p,p_len,sa,sa_len,itr,out_len,out) \
   pbkdf2_hmac_sha1(p_len,(const uint8_t *)p,itr,sa_len,sa,out_len,out)

int hcrypt_aes_set_encrypt_key (const uint8_t *key, unsigned bits, struct aes_ctx *ctx);
int hcrypt_aes_set_decrypt_key (const uint8_t *key, unsigned bits, struct aes_ctx *ctx);

int hcrypt_WrapKey (AES_KEY *key, unsigned char *out,
                    const unsigned char *in, unsigned int inlen);

int hcrypt_UnwrapKey (AES_KEY *key, unsigned char *out,
                      const unsigned char *in, unsigned int inlen);

#endif /* __HCRYPT_GNUTLS_H__ */

