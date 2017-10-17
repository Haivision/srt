/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 *   Author: Justin Kim <justin.kim@collabora.com>
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

#ifndef __CRYPTOGRAPHIC_INTERNAL_H__
#define __CRYPTOGRAPHIC_INTERNAL_H__

#if !defined(USE_NETTLE)

/* By default, SRT uses OpenSSL */
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/evp.h>	/* PKCS5_xxx() */
#include <openssl/modes.h>	/* CRYPTO_xxx() */
#include <openssl/opensslv.h>   /* OPENSSL_VERSION_NUMBER  */
#include <openssl/rand.h>       /* RAND_bytes */

#if (OPENSSL_VERSION_NUMBER < 0x0090808fL) //0.9.8h
#include <openssl/bio.h>
#endif

#else // !defined(USE_NETTLE)

#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>

#include <nettle/aes.h>
#include <nettle/ctr.h>
#include <nettle/hmac.h>
#include <nettle/pbkdf2.h>

#undef OPENSSL_VERSION_NUMBER
#define OPENSSL_VERSION_NUMBER 0L

#if     __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
#define GNUC_UNUSED                           \
  __attribute__((__unused__))
#else
#define GNUC_UNUSED
#endif

typedef struct aes128_ctx AES_KEY;

int AES_set_encrypt_key(const uint8_t *key, unsigned bits, AES_KEY *aeskey);

int AES_set_decrypt_key(const uint8_t *key, unsigned bits, AES_KEY *aeskey);

typedef void (*block128_f) (struct aes_ctx *ctx, unsigned length, uint8_t *dst, const uint8_t *src);

#define OPENSSL_cleanse(p,l)
#define RAND_bytes(d,s) gnutls_rnd(GNUTLS_RND_KEY,(d),(s))
#define PKCS5_PBKDF2_HMAC(p,p_l,sa,sa_l,iter,digest,k_l,d)


void AES_ecb_encrypt(const unsigned char *in, unsigned char *out, const AES_KEY *key, const int enc);

#define AES_ecb_encrypt(s,d,key,enc)


#endif // !defined(USE_NETTLE)
#endif // __CRYPTOGRAPHIC_INTERNAL_H__
