/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */
#ifndef __HCRYPT_OPENSSL_H__
#define __HCRYPT_OPENSSL_H__

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

#define hcrypt_pbkdf2_hmac_sha1(p,p_len,sa,sa_len,itr,out_len,out) \
   PKCS5_PBKDF2_HMAC_SHA1(p,p_len,sa,sa_len,itr,out_len,out)
#define hcrypt_aes_set_encrypt_key AES_set_encrypt_key
#define hcrypt_aes_set_decrypt_key AES_set_decrypt_key

#endif /* __HCRYPT_OPENSSL_H__ */
