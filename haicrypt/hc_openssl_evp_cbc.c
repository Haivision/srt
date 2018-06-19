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

   2014-07-18 (jdube)
        HaiCrypt initial implementation.
*****************************************************************************/

#include "hcrypt.h"

#ifdef HAICRYPT_USE_OPENSSL_EVP_CBC

#include <string.h>
#include <openssl/evp.h>

#define HCRYPT_EVP_CBC_BLK_SZ		AES_BLOCK_SIZE

typedef struct tag_hcOpenSSL_EVP_CBC_data {
		EVP_CIPHER_CTX evp_ctx[2];

#define	HCRYPT_OPENSSL_EVP_CBC_OUTMSGMAX	6
		unsigned char * outbuf; 		/* output circle buffer */
		size_t          outbuf_ofs;		/* write offset in circle buffer */
		size_t          outbuf_siz;		/* circle buffer size */
}hcOpenSSL_EVP_CBC_data;

#if 0 //>>use engine
#include <openssl/engine.h>

static int hcOpenSSL_EnginesLoaded = 0;
static ENGINE *hcOpenSSL_Engine = NULL;

void hcOpenSSL_EnginesInit(void)
{
    if (!hcOpenSSL_EnginesLoaded) {  
#if 1
        ENGINE *e = NULL;
        ENGINE_load_cryptodev();

        if (NULL == (e = ENGINE_by_id("cryptodev"))) {
          	HCRYPT_LOG(LOG_ERR, "%s", "Cryptodev not available\n");
		} else if(!ENGINE_set_default(e, ENGINE_METHOD_ALL)) {
    	    HCRYPT_LOG(LOG_ERR, "%s", "Cannot use cryptodev\n");
        } else {
    	    HCRYPT_LOG(LOG_DEBUG, "Using \"%s\" engine\n", ENGINE_get_id(e));
            hcOpenSSL_Engine = e;
        }
        ENGINE_free(e);
#else
	    HCRYPT_LOG(LOG_DEBUG, "%s", "Loading engines\n");
        /* Load all bundled ENGINEs into memory and make them visible */
        ENGINE_load_builtin_engines();
        /* Register all of them for every algorithm they collectively implement */
        ENGINE_register_all_complete();
#endif
    }
    hcOpenSSL_EnginesLoaded++;
}

void hcOpenSSL_EnginesExit(void)
{
    if ((0 < hcOpenSSL_EnginesLoaded)
    &&  (0 == --hcOpenSSL_EnginesLoaded)) {
        ENGINE_cleanup();
      	HCRYPT_LOG(LOG_DEBUG, "%s", "Cleanup engines\n");
    }
}
#else
#define hcOpenSSL_Engine        (NULL)
#define hcOpenSSL_EnginesInit()
#define hcOpenSSL_EnginesExit()
#endif


static unsigned char *hcOpenSSL_EVP_CBC_GetOutbuf(hcOpenSSL_EVP_CBC_data *evp_data, size_t hdr_len, size_t out_len)
{
	unsigned char *out_buf;
    int nblks = (out_len + AES_BLOCK_SIZE -1) / AES_BLOCK_SIZE;
	if ((hdr_len + (nblks * AES_BLOCK_SIZE)) > (evp_data->outbuf_siz - evp_data->outbuf_ofs)) {
		/* Not enough room left, circle buffers */
		evp_data->outbuf_ofs = 0;
	}
	out_buf = &evp_data->outbuf[evp_data->outbuf_ofs];
	evp_data->outbuf_ofs += (hdr_len + (nblks * AES_BLOCK_SIZE));
	return(out_buf);
}

static int hcOpenSSL_EVP_CBC_CipherData(EVP_CIPHER_CTX *evp_ctx, 
	unsigned char *in_ptr, size_t in_len, unsigned char *iv, unsigned char *out_ptr, size_t *out_len_ptr)
{
	int c_len = 0;
    int f_len = 0;

	/* allows reusing of 'e' for multiple encryption cycles */
	if (!EVP_CipherInit_ex(evp_ctx, NULL, NULL, NULL, iv, -1)) {
		HCRYPT_LOG(LOG_ERR, "%s", "cipher init failed\n"); 
	} else if (!EVP_CipherUpdate(evp_ctx, out_ptr, &c_len, in_ptr, in_len)) {
		HCRYPT_LOG(LOG_ERR, "%s", "cipher init failed\n"); 
    } else if (!EVP_CipherFinal_ex(evp_ctx, &out_ptr[c_len], &f_len)) {
		HCRYPT_LOG(LOG_ERR, "incomplete block (%zd/%d,%d)\n", in_len, c_len, f_len); 
	}
	if (out_len_ptr) *out_len_ptr = c_len + f_len;
	return(0);
}


static hcrypt_CipherData *hcOpenSSL_EVP_CBC_Open(size_t max_len)
{
	hcOpenSSL_EVP_CBC_data *evp_data;
	unsigned char *membuf;
	size_t padded_len = hcryptMsg_PaddedLen(max_len, 128/8);
	size_t memsiz = sizeof(*evp_data)
		+ (HCRYPT_OPENSSL_EVP_CBC_OUTMSGMAX * padded_len);

    
	HCRYPT_LOG(LOG_DEBUG, "%s", "Using OpenSSL EVP-CBC\n");

	evp_data = malloc(memsiz);
	if (NULL == evp_data) {
		HCRYPT_LOG(LOG_ERR, "malloc(%zd) failed\n", memsiz);
		return(NULL);
	}
    hcOpenSSL_EnginesInit();

	membuf = (unsigned char *)evp_data;
	membuf += sizeof(*evp_data);

	evp_data->outbuf = membuf;
//	membuf += HCRYPT_OPENSSL_EVP_CBC_OUTMSGMAX * padded_len;
	evp_data->outbuf_siz = HCRYPT_OPENSSL_EVP_CBC_OUTMSGMAX * padded_len;
	evp_data->outbuf_ofs = 0;

	EVP_CIPHER_CTX_init(&evp_data->evp_ctx[0]);
//	EVP_CIPHER_CTX_set_padding(&evp_data->evp_ctx[0], 0);

	EVP_CIPHER_CTX_init(&evp_data->evp_ctx[1]);
//	EVP_CIPHER_CTX_set_padding(&evp_data->evp_ctx[1], 0);

	return((hcrypt_CipherData *)evp_data);
}

static int hcOpenSSL_EVP_CBC_Close(hcrypt_CipherData *cipher_data)
{
	hcOpenSSL_EVP_CBC_data *evp_data = (hcOpenSSL_EVP_CBC_data *)cipher_data;

	if (NULL != evp_data) {
		EVP_CIPHER_CTX_cleanup(&evp_data->evp_ctx[0]);
		EVP_CIPHER_CTX_cleanup(&evp_data->evp_ctx[1]);
		free(evp_data);
	}
    hcOpenSSL_EnginesExit();
	return(0);
}

static int hcOpenSSL_EVP_CBC_SetKey(hcrypt_CipherData *cipher_data, hcrypt_Ctx *ctx, unsigned char *key, size_t key_len)
{
	hcOpenSSL_EVP_CBC_data *evp_data = (hcOpenSSL_EVP_CBC_data *)cipher_data;
	EVP_CIPHER_CTX *evp_ctx = &evp_data->evp_ctx[hcryptCtx_GetKeyIndex(ctx)];
	int enc = (ctx->flags & HCRYPT_CTX_F_ENCRYPT);
	const EVP_CIPHER *cipher = NULL;

 	switch(key_len) {
	case 128/8:
		cipher = EVP_aes_128_cbc();
		break;
	case 192/8:
		cipher = EVP_aes_192_cbc();
		break;
	case 256/8:
		cipher = EVP_aes_256_cbc();
		break;
	default:
		HCRYPT_LOG(LOG_ERR, "invalid key length (%d). Expected: 16, 24, 32\n", (int)key_len);
		return(-1);
	}
	EVP_CipherInit_ex(evp_ctx, cipher, hcOpenSSL_Engine, key, NULL, enc);
	return(0);
}

static int hcOpenSSL_EVP_CBC_Crypt(hcrypt_CipherData *cipher_data, hcrypt_Ctx *ctx,
	hcrypt_DataDesc *in_data, int nbin, void *out_p[], size_t out_len_p[], int *nbout_p)
{
	hcOpenSSL_EVP_CBC_data *evp_data = (hcOpenSSL_EVP_CBC_data *)cipher_data;
	unsigned char iv[HCRYPT_EVP_CBC_BLK_SZ];
	hcrypt_Pki pki;
	unsigned char *out_msg;
	size_t pfx_len;
	size_t out_len;
	int iret;

	ASSERT(NULL != evp_data);
	ASSERT(NULL != ctx);
	ASSERT((NULL != in_data) && (1 == nbin));
	ASSERT((NULL == out_p) || ((NULL != out_p) && (NULL != out_len_p) && (NULL != nbout_p)));

	/* Room for prefix in output buffer only required for encryption */
	pfx_len = ctx->flags & HCRYPT_CTX_F_ENCRYPT ? ctx->msg_info->pfx_len : 0;

//>>set CBC eventually
	if (HCRYPT_CTX_MODE_AESCTR != ctx->mode) {
		HCRYPT_LOG(LOG_ERR, "invalid mode (%d) for cipher\n", ctx->mode);
		return(-1);
	}

	/* Compute IV */
	pki = hcryptMsg_GetPki(ctx->msg_info, in_data[0].pfx, 1);
	hcrypt_SetCtrIV(&pki, ctx->salt, iv); 

	/* Reserve output buffer for cipher */
	out_msg = hcOpenSSL_EVP_CBC_GetOutbuf(evp_data, pfx_len, in_data[0].len);

	/* Encrypt */
	iret = hcOpenSSL_EVP_CBC_CipherData(&evp_data->evp_ctx[hcryptCtx_GetKeyIndex(ctx)],
		in_data[0].payload, in_data[0].len, iv, &out_msg[pfx_len], &out_len);
	if (iret) {
		HCRYPT_LOG(LOG_ERR, "%s", "CBC_CipherData failed\n");
		return(iret);
	}

	/* Output cipher text */
	if (out_len > 0) {
		if (NULL == out_p) {
			/* Copy output data back in input buffer */
			memcpy(in_data[0].payload, &out_msg[pfx_len], out_len);
		} else {
			/* Copy header in output buffer if needed */
			if (pfx_len > 0) memcpy(out_msg, in_data[0].pfx, pfx_len);
			out_p[0] = out_msg;
			out_len_p[0] = pfx_len + out_len;
			*nbout_p = 1;
		}
		iret = (int)out_len;
	} else {
		if (NULL != nbout_p) *nbout_p = 0;
		iret = -1;
	}
	return(iret);
}

static hcrypt_Cipher hcOpenSSL_EVP_CBC_cipher;

HaiCrypt_Cipher HaiCryptCipher_OpenSSL_EVP_CBC(void)
{
	hcOpenSSL_EVP_CBC_cipher.open       = hcOpenSSL_EVP_CBC_Open;
	hcOpenSSL_EVP_CBC_cipher.close      = hcOpenSSL_EVP_CBC_Close;
	hcOpenSSL_EVP_CBC_cipher.setkey     = hcOpenSSL_EVP_CBC_SetKey;
	hcOpenSSL_EVP_CBC_cipher.encrypt    = hcOpenSSL_EVP_CBC_Crypt;
	hcOpenSSL_EVP_CBC_cipher.decrypt    = hcOpenSSL_EVP_CBC_Crypt;

	return((HaiCrypt_Cipher)&hcOpenSSL_EVP_CBC_cipher);
}
#endif /* HAICRYPT_USE_OPENSSL_EVP_CBC */
