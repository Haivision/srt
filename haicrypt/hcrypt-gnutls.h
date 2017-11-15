/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 *     Author: Justin Kim <justin.kim@collabora.com>
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

