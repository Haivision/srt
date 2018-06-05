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
   2014-07-15 (jdube)
        Adaptation for EVP CTR mode
*****************************************************************************/

#include "hcrypt.h"

#ifdef HAICRYPT_USE_OPENSSL_EVP_CTR

#include <string.h>
#include <openssl/evp.h>

#define HCRYPT_EVP_CTR_BLK_SZ       AES_BLOCK_SIZE

typedef struct tag_hcOpenSSL_EVP_CTR_data {
		EVP_CIPHER_CTX *evp_ctx[2];

#ifdef HAICRYPT_USE_OPENSSL_EVP_ECB4CTR
#define HCRYPT_EVP_CTR_STREAM_SZ	2048
		unsigned char * ctr_stream;
		size_t          ctr_stream_len; /* Content size */
		size_t          ctr_stream_siz; /* Allocated length */
#endif /* HAICRYPT_USE_OPENSSL_EVP_ECB4CTR */

#define	HCRYPT_OPENSSL_EVP_CTR_OUTMSGMAX	6
		unsigned char * outbuf;         /* output circle buffer */
		size_t          outbuf_ofs;     /* write offset in circle buffer */
		size_t          outbuf_siz;     /* circle buffer size */
}hcOpenSSL_EVP_CTR_data;

#ifdef HAICRYPT_USE_OPENSSL_EVP_ECB4CTR
static int hcOpenSSL_EVP_CTR_SetCtrStream(hcOpenSSL_EVP_CTR_data *aes_data, hcrypt_Ctx *ctx, size_t len, unsigned char *iv)
{	
	/* Counter stream:
	 *   0   1   2   3   4   5     nblk
	 * +---+---+---+---+---+---+---+---+
	 * |blk|blk|blk|blk|blk|blk|...|blk|
	 * +---+---+---+---+---+---+---+---+
	 */

	/* IV (128-bit):
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
	unsigned char ctr[HCRYPT_EVP_CTR_BLK_SZ];
	unsigned nblk;

	ASSERT(NULL != aes_data);
	ASSERT(NULL != ctx);

	memcpy(ctr, iv, HCRYPT_EVP_CTR_BLK_SZ); 

	nblk = (len + (HCRYPT_EVP_CTR_BLK_SZ-1))/HCRYPT_EVP_CTR_BLK_SZ;
	if ((nblk * HCRYPT_EVP_CTR_BLK_SZ) <= aes_data->ctr_stream_siz) {
		unsigned blk;
		unsigned char *csp = &aes_data->ctr_stream[0];

		for(blk = 0; blk < nblk; blk++) {
			memcpy(csp, ctr, HCRYPT_EVP_CTR_BLK_SZ);
			csp += HCRYPT_EVP_CTR_BLK_SZ;
			if (0 == ++(ctr[HCRYPT_EVP_CTR_BLK_SZ-1])) ++(ctr[HCRYPT_EVP_CTR_BLK_SZ-2]);
		}
		aes_data->ctr_stream_len = nblk * HCRYPT_EVP_CTR_BLK_SZ;
	} else {
		HCRYPT_LOG(LOG_ERR, "packet too long(%zd)\n", len);
		return(-1);
	}
	return(0);
}
#endif /* HAICRYPT_USE_OPENSSL_EVP_ECB4CTR */

static unsigned char *hcOpenSSL_EVP_CTR_GetOutbuf(hcOpenSSL_EVP_CTR_data *evp_data, size_t hdr_len, size_t out_len)
{
	unsigned char *out_buf;

	if ((hdr_len + out_len) > (evp_data->outbuf_siz - evp_data->outbuf_ofs)) {
		/* Not enough room left, circle buffers */
		evp_data->outbuf_ofs = 0;
	}
	out_buf = &evp_data->outbuf[evp_data->outbuf_ofs];
	evp_data->outbuf_ofs += (hdr_len + out_len);
	return(out_buf);
}

static int hcOpenSSL_EVP_CTR_CipherData(EVP_CIPHER_CTX *evp_ctx, 
	unsigned char *in_ptr, size_t in_len, unsigned char *iv, unsigned char *out_ptr, size_t *out_len_ptr)
{
	int c_len, f_len;

	/* allows reusing of 'e' for multiple encryption cycles */
	EVP_CipherInit_ex(evp_ctx, NULL, NULL, NULL, iv, -1);
	EVP_CIPHER_CTX_set_padding(evp_ctx, 0);

	/* update ciphertext, c_len is filled with the length of ciphertext generated,
	 * cryptoPtr->cipher_in_len is the size of plain/cipher text in bytes 
	 */
	EVP_CipherUpdate(evp_ctx, out_ptr, &c_len, in_ptr, in_len);

	/* update ciphertext with the final remaining bytes */
	/* Useless with pre-padding */
	f_len = 0;
	if (0 == EVP_CipherFinal_ex(evp_ctx, &out_ptr[c_len], &f_len)) {
		HCRYPT_LOG(LOG_ERR, "incomplete block (%d/%zd)\n", c_len, in_len); 
	}
	if (out_len_ptr) *out_len_ptr = c_len + f_len;
	return(0);
}


static hcrypt_CipherData *hcOpenSSL_EVP_CTR_Open(size_t max_len)
{
	hcOpenSSL_EVP_CTR_data *evp_data;
	unsigned char *membuf;
	size_t padded_len = hcryptMsg_PaddedLen(max_len, 128/8);
	size_t memsiz = sizeof(*evp_data)
#ifdef HAICRYPT_USE_OPENSSL_EVP_ECB4CTR
		+ HCRYPT_EVP_CTR_STREAM_SZ
#endif /* HAICRYPT_USE_OPENSSL_EVP_ECB4CTR */
		+ (HCRYPT_OPENSSL_EVP_CTR_OUTMSGMAX * padded_len);

	HCRYPT_LOG(LOG_DEBUG, "%s", "Using OpenSSL EVP-CTR\n");

	evp_data = malloc(memsiz);
	if (NULL == evp_data) {
		HCRYPT_LOG(LOG_ERR, "malloc(%zd) failed\n", memsiz);
		return(NULL);
	}
	membuf = (unsigned char *)evp_data;
	membuf += sizeof(*evp_data);

#ifdef HAICRYPT_USE_OPENSSL_EVP_ECB4CTR
	evp_data->ctr_stream = membuf;
	membuf += HCRYPT_EVP_CTR_STREAM_SZ;
	evp_data->ctr_stream_siz = HCRYPT_EVP_CTR_STREAM_SZ;
	evp_data->ctr_stream_len = 0;
#endif /* HAICRYPT_USE_OPENSSL_EVP_ECB4CTR */

	evp_data->outbuf = membuf;
	evp_data->outbuf_siz = HCRYPT_OPENSSL_EVP_CTR_OUTMSGMAX * padded_len;
	evp_data->outbuf_ofs = 0;

	evp_data->evp_ctx[0] = EVP_CIPHER_CTX_new();
	EVP_CIPHER_CTX_set_padding(evp_data->evp_ctx[0], 0);

	evp_data->evp_ctx[1] = EVP_CIPHER_CTX_new();
	EVP_CIPHER_CTX_set_padding(evp_data->evp_ctx[1], 0);

	return((hcrypt_CipherData *)evp_data);
}

static int hcOpenSSL_EVP_CTR_Close(hcrypt_CipherData *cipher_data)
{
	hcOpenSSL_EVP_CTR_data *evp_data = (hcOpenSSL_EVP_CTR_data *)cipher_data;

	if (NULL != evp_data) {
		EVP_CIPHER_CTX_free(evp_data->evp_ctx[0]);
		EVP_CIPHER_CTX_free(evp_data->evp_ctx[1]);
		free(evp_data);
	}
	return(0);
}

static int hcOpenSSL_EVP_CTR_SetKey(hcrypt_CipherData *cipher_data, hcrypt_Ctx *ctx, unsigned char *key, size_t key_len)
{
	hcOpenSSL_EVP_CTR_data *evp_data = (hcOpenSSL_EVP_CTR_data *)cipher_data;
	EVP_CIPHER_CTX *evp_ctx = evp_data->evp_ctx[hcryptCtx_GetKeyIndex(ctx)];
	int enc = ((ctx->flags & HCRYPT_CTX_F_ENCRYPT) || (ctx->mode == HCRYPT_CTX_MODE_AESCTR));
	const EVP_CIPHER *cipher = NULL;

	switch(key_len) {
#ifdef HAICRYPT_USE_OPENSSL_EVP_ECB4CTR
	case 128/8:
		cipher = EVP_aes_128_ecb();
		break;
	case 192/8:
		cipher = EVP_aes_192_ecb();
		break;
	case 256/8:
		cipher = EVP_aes_256_ecb();
		break;
#else /* HAICRYPT_USE_OPENSSL_EVP_ECB4CTR */
	case 128/8:
		cipher = EVP_aes_128_ctr();
		break;
	case 192/8:
		cipher = EVP_aes_192_ctr();
		break;
	case 256/8:
		cipher = EVP_aes_256_ctr();
		break;
#endif /* HAICRYPT_USE_OPENSSL_EVP_ECB4CTR */
	default:
		HCRYPT_LOG(LOG_ERR, "invalid key length (%d). Expected: 16, 24, 32\n", (int)key_len);
		return(-1);
	}

	EVP_CipherInit_ex(evp_ctx, cipher, NULL, key, NULL, enc);
	return(0);
}

static int hcOpenSSL_EVP_CTR_Crypt(hcrypt_CipherData *cipher_data, hcrypt_Ctx *ctx,
	hcrypt_DataDesc *in_data, int nbin ATR_UNUSED, void *out_p[], size_t out_len_p[], int *nbout_p)
{
	hcOpenSSL_EVP_CTR_data *evp_data = (hcOpenSSL_EVP_CTR_data *)cipher_data;
	unsigned char iv[HCRYPT_EVP_CTR_BLK_SZ];
	hcrypt_Pki pki;
	unsigned char *out_msg;
	size_t pfx_len;
	size_t out_len;
	int iret;

	ASSERT(NULL != evp_data);
	ASSERT(NULL != ctx);
	ASSERT((NULL != in_data) && (1 == nbin));

	/* Room for prefix in output buffer only required for encryption */
	pfx_len = ctx->flags & HCRYPT_CTX_F_ENCRYPT ? ctx->msg_info->pfx_len : 0;

	if (HCRYPT_CTX_MODE_AESCTR != ctx->mode) {
		HCRYPT_LOG(LOG_ERR, "invalid mode (%d) for cipher\n", ctx->mode);
		return(-1);
	}

	/* Compute IV */
	pki = hcryptMsg_GetPki(ctx->msg_info, in_data[0].pfx, 1);
	hcrypt_SetCtrIV(&pki, ctx->salt, iv); 

	#ifdef HAICRYPT_USE_OPENSSL_EVP_ECB4CTR
		/* Create CtrStream. May be longer than in_len (next cipher block size boundary) */
		iret = hcOpenSSL_EVP_CTR_SetCtrStream(evp_data, ctx, in_data[0].len, iv);
		if (iret) {
			return(iret);
		}
		/* Reserve output buffer for cipher */
		out_msg = hcOpenSSL_EVP_CTR_GetOutbuf(evp_data, pfx_len, evp_data->ctr_stream_len);

		/* Create KeyStream (encrypt CtrStream) */
		iret = hcOpenSSL_EVP_CTR_CipherData(evp_data->evp_ctx[hcryptCtx_GetKeyIndex(ctx)],
			evp_data->ctr_stream, evp_data->ctr_stream_len, NULL, 
			&out_msg[pfx_len], &out_len);
		if (iret) {
			HCRYPT_LOG(LOG_ERR, "%s", "CRYPTOEVP_CipherData failed\n");
			return(iret);
		}

	#else /* HAICRYPT_USE_OPENSSL_EVP_ECB4CTR */
		/* Reserve output buffer for cipher */
		out_msg = hcOpenSSL_EVP_CTR_GetOutbuf(evp_data, pfx_len, in_data[0].len);

		/* Encrypt */
		iret = hcOpenSSL_EVP_CTR_CipherData(evp_data->evp_ctx[hcryptCtx_GetKeyIndex(ctx)],
			in_data[0].payload, in_data[0].len, iv, 
			&out_msg[pfx_len], &out_len);
		if (iret) {
			HCRYPT_LOG(LOG_ERR, "%s", "CTR_CipherData failed\n");
			return(iret);
		}
	#endif /* HAICRYPT_USE_OPENSSL_EVP_ECB4CTR */

	/* Output clear or cipher text */
	if (out_len > 0) {
		if (NULL == out_p) {
			#ifdef HAICRYPT_USE_OPENSSL_EVP_ECB4CTR
				/* XOR KeyStream with input text directly in input buffer */
				hcrypt_XorStream(in_data[0].payload, &out_msg[pfx_len], out_len);
			#else /* HAICRYPT_USE_OPENSSL_EVP_ECB4CTR */
				/* Copy output data back in input buffer */
				memcpy(in_data[0].payload, &out_msg[pfx_len], out_len);
			#endif /* HAICRYPT_USE_OPENSSL_EVP_ECB4CTR */
		} else {
			/* Copy header in output buffer if needed */
			if (pfx_len > 0) memcpy(out_msg, in_data[0].pfx, pfx_len);
			#ifdef HAICRYPT_USE_OPENSSL_EVP_ECB4CTR
				hcrypt_XorStream(&out_msg[pfx_len], in_data[0].payload, out_len);
			#endif /* HAICRYPT_USE_OPENSSL_EVP_ECB4CTR */
			out_p[0] = out_msg;
			out_len_p[0] = pfx_len + out_len;
			*nbout_p = 1;
		}
		iret = 0;
	} else {
		if (NULL != nbout_p) *nbout_p = 0;
		iret = -1;
	}
	return(iret);
}

static hcrypt_Cipher hcOpenSSL_EVP_CTR_cipher;

HaiCrypt_Cipher HaiCryptCipher_OpenSSL_EVP_CTR(void)
{
	hcOpenSSL_EVP_CTR_cipher.open       = hcOpenSSL_EVP_CTR_Open;
	hcOpenSSL_EVP_CTR_cipher.close      = hcOpenSSL_EVP_CTR_Close;
	hcOpenSSL_EVP_CTR_cipher.setkey     = hcOpenSSL_EVP_CTR_SetKey;
	hcOpenSSL_EVP_CTR_cipher.encrypt    = hcOpenSSL_EVP_CTR_Crypt; // Counter Mode encrypt and
	hcOpenSSL_EVP_CTR_cipher.decrypt    = hcOpenSSL_EVP_CTR_Crypt; // ...decrypt are the same

	return((HaiCrypt_Cipher)&hcOpenSSL_EVP_CTR_cipher);
}

/* Backward compatible call when only CTR was available */
HaiCrypt_Cipher HaiCryptCipher_OpenSSL_EVP(void) 
{
	return(HaiCryptCipher_OpenSSL_EVP_CTR());
}

HaiCrypt_Cipher HaiCryptCipher_Get_Instance(void) 
{
	return(HaiCryptCipher_OpenSSL_EVP_CTR());
}

#endif /* HAICRYPT_USE_OPENSSL_EVP_CTR */
