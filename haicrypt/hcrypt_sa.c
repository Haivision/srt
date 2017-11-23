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
*****************************************************************************/

/*
 * For now: 
 *  Pre-shared or password derived KEK (Key Encrypting Key)
 * Future:
 *  Certificate-based association
 */
#include <string.h>				/* memcpy */
#include "hcrypt.h"

int hcryptCtx_SetSecret(hcrypt_Session *crypto, hcrypt_Ctx *ctx, const HaiCrypt_Secret *secret)
{
	int iret;
	(void)crypto;

	switch(secret->typ) {
	case HAICRYPT_SECTYP_PRESHARED:
		ASSERT(secret->len <= HAICRYPT_KEY_MAX_SZ);
		ctx->cfg.pwd_len = 0;
		/* KEK: Key Encrypting Key */
		if (HCRYPT_CTX_F_ENCRYPT & ctx->flags) {
			iret = hcrypt_aes_set_encrypt_key(secret->str, secret->len * 8, &ctx->aes_kek);
		} else {
			iret = hcrypt_aes_set_decrypt_key(secret->str, secret->len * 8, &ctx->aes_kek);
		}			
		if (0 > iret) {
		HCRYPT_LOG(LOG_ERR, "AES_set_%s_key(kek[%zd]) failed (rc=%d)\n", 
				HCRYPT_CTX_F_ENCRYPT & ctx->flags ? "encrypt" : "decrypt",
				secret->len, iret);
			return(-1);
		}
		ctx->status = HCRYPT_CTX_S_SARDY;
		break;

	case HAICRYPT_SECTYP_PASSPHRASE:
		ASSERT(secret->len <= sizeof(ctx->cfg.pwd));
		memcpy(ctx->cfg.pwd, secret->str, secret->len);
		ctx->cfg.pwd_len = secret->len;
		/* KEK will be derived from password with Salt */
		ctx->status = HCRYPT_CTX_S_SARDY;
		break;

	default:
	    HCRYPT_LOG(LOG_ERR, "Unknown secret type  %d\n", 
			secret->typ);
		return(-1);
	}
	return(0);
}

int hcryptCtx_GenSecret(hcrypt_Session *crypto, hcrypt_Ctx *ctx)
{
	/*
	 * KEK need same length as the key it protects (SEK)
	 * KEK = PBKDF2(Pwd, LSB(64, Salt), Iter, Klen)
	 */
	unsigned char kek[HAICRYPT_KEY_MAX_SZ];
	size_t kek_len = ctx->sek_len;
	size_t pbkdf_salt_len = (ctx->salt_len >= HAICRYPT_PBKDF2_SALT_LEN
		? HAICRYPT_PBKDF2_SALT_LEN 
		: ctx->salt_len);
	int iret;
	(void)crypto;

	hcrypt_pbkdf2_hmac_sha1(ctx->cfg.pwd, ctx->cfg.pwd_len, 
		&ctx->salt[ctx->salt_len - pbkdf_salt_len], pbkdf_salt_len, 
		HAICRYPT_PBKDF2_ITER_CNT, kek_len, kek);

	HCRYPT_PRINTKEY(ctx->cfg.pwd, ctx->cfg.pwd_len, "pwd");
	HCRYPT_PRINTKEY(kek, kek_len, "kek");
	
	/* KEK: Key Encrypting Key */
	if (HCRYPT_CTX_F_ENCRYPT & ctx->flags) {
		if (0 > (iret = hcrypt_aes_set_encrypt_key(kek, kek_len * 8, &ctx->aes_kek))) {
		HCRYPT_LOG(LOG_ERR, "AES_set_encrypt_key(pdkek[%zd]) failed (rc=%d)\n", kek_len, iret);
			return(-1);
		}
	} else {
		if (0 > (iret = hcrypt_aes_set_decrypt_key(kek, kek_len * 8, &ctx->aes_kek))) {
		HCRYPT_LOG(LOG_ERR, "AES_set_decrypt_key(pdkek[%zd]) failed (rc=%d)\n", kek_len, iret);
			return(-1);
		}
	}
	return(0);
}

