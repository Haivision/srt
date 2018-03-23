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

#include <stdio.h>	/* snprintf */
#include <stdlib.h>	/* NULL, malloc, free */
#include <string.h>	/* memcpy, memset */
#ifdef WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/time.h>	/* timerclear */
#endif

#include "hcrypt.h"

int HaiCrypt_Create(HaiCrypt_Cfg *cfg, HaiCrypt_Handle *phhc)
{
	hcrypt_Session *crypto;
	hcrypt_Cipher *cipher;
	unsigned char *mem_buf;
	size_t mem_siz, inbuf_siz;
	int tx;

	*phhc = NULL;

	ASSERT(NULL != cfg);

    HCRYPT_LOG_INIT();
    //Test log
    HCRYPT_LOG(LOG_INFO, "creating crypto context(flags=0x%x)\n", cfg->flags);

	if (!(HAICRYPT_CFG_F_CRYPTO & cfg->flags)) {
		HCRYPT_LOG(LOG_INFO, "no supported flags set (0x%x)\n", cfg->flags);
		return(-1);
	} else 
	if ((16 != cfg->key_len)	/* SEK length */
	&&  (24 != cfg->key_len)
	&&  (32 != cfg->key_len)) {
		HCRYPT_LOG(LOG_ERR, "invalid key length (%zd)\n", cfg->key_len);
		return(-1);
	} else 
	if ((HAICRYPT_SECTYP_PASSPHRASE == cfg->secret.typ)
	&&  ((0 == cfg->secret.len) || (sizeof(cfg->secret.str) < cfg->secret.len))) { /* KEK length */
		HCRYPT_LOG(LOG_ERR, "invalid secret passphrase length (%zd)\n", cfg->secret.len);
		return(-1);
	} else
	if ((HAICRYPT_SECTYP_PRESHARED == cfg->secret.typ)
	&&  (16 != cfg->key_len)	/* SEK length */
	&&  (24 != cfg->key_len)
	&&  (32 != cfg->key_len)) {
		HCRYPT_LOG(LOG_ERR, "invalid pre-shared secret length (%zd)\n", cfg->secret.len);
		return(-1);
	} else
	if ((HAICRYPT_SECTYP_PRESHARED == cfg->secret.typ)
	&&  (cfg->key_len > cfg->secret.len)) {
		HCRYPT_LOG(LOG_ERR, "preshared secret length (%zd) smaller than key length (%zd)\n", 
			cfg->secret.len, cfg->key_len);
		return(-1);
	} else
	if (NULL == cfg->cipher) {
		HCRYPT_LOG(LOG_ERR, "%s\n", "no cipher specified");
		return(-1);
	} else
	if (0 == cfg->data_max_len) {
		HCRYPT_LOG(LOG_ERR, "%s\n", "no data_max_len specified");
		return(-1);
	}

	cipher = (hcrypt_Cipher *)cfg->cipher;
	tx = HAICRYPT_CFG_F_TX & cfg->flags;
	HCRYPT_PRINTKEY(cfg->secret.str, cfg->secret.len, "cfgkey");
		
	/* 
	 * If cipher has no special input buffer alignment requirement,
	 * handle it in the crypto session.
	 */
	inbuf_siz = 0;	
	if (NULL == cipher->getinbuf) {
		inbuf_siz = hcryptMsg_PaddedLen(cfg->data_max_len, 128/8);
	}

	/* Allocate crypto session control struct */
	mem_siz = sizeof(hcrypt_Session)	// structure
			+ inbuf_siz;

	crypto = malloc(mem_siz);
	if (NULL == crypto){	
		HCRYPT_LOG(LOG_ERR, "%s\n", "malloc failed");
		return(-1);
	}
	mem_buf = (unsigned char *)crypto;
	mem_buf += sizeof(*crypto);
	memset(crypto, 0, sizeof(*crypto));

	if (inbuf_siz) {
		crypto->inbuf = mem_buf;
		crypto->inbuf_siz = inbuf_siz;
	}

	crypto->cipher = cfg->cipher;

	/* Setup transport packet info */
	switch (cfg->xport) {
	case HAICRYPT_XPT_STANDALONE:
		crypto->se = HCRYPT_SE_TSUDP;
		crypto->msg_info = hcryptMsg_STA_MsgInfo();
		break;
	case HAICRYPT_XPT_SRT:
		crypto->se = HCRYPT_SE_TSSRT;
		crypto->msg_info = hcryptMsg_SRT_MsgInfo();
		break;
	default:
		HCRYPT_LOG(LOG_ERR, "invalid xport: %d\n", cfg->xport);
		free(crypto);
		return(-1);
	}

	timerclear(&crypto->km.tx_last);
	crypto->km.tx_period.tv_sec  = cfg->km_tx_period_ms / 1000;
	crypto->km.tx_period.tv_usec = (cfg->km_tx_period_ms % 1000) * 1000;

	crypto->km.refresh_rate = cfg->km_refresh_rate_pkt;
	crypto->km.pre_announce = cfg->km_pre_announce_pkt;

	/* Indentify each context */
	crypto->ctx_pair[0].flags = HCRYPT_MSG_F_eSEK | (tx ? HCRYPT_CTX_F_ENCRYPT : 0);
	crypto->ctx_pair[1].flags = HCRYPT_MSG_F_oSEK | (tx ? HCRYPT_CTX_F_ENCRYPT : 0);
	/* Point to each other */
	crypto->ctx_pair[0].alt = &crypto->ctx_pair[1];
	crypto->ctx_pair[1].alt = &crypto->ctx_pair[0];

	crypto->cipher_data = crypto->cipher->open(cfg->data_max_len);
	if (NULL == crypto->cipher_data) {
		free(crypto);
		return(-1);
	}
	if (tx) { /* Encoder */
		/* Configure initial context */
		if (hcryptCtx_Tx_Init(crypto, &crypto->ctx_pair[0], cfg)
		||  hcryptCtx_Tx_Init(crypto, &crypto->ctx_pair[1], cfg)) {
			free(crypto);
			return(-1);
		}			
		/* Generate keys for first (default) context */
		if (hcryptCtx_Tx_Rekey(crypto, &crypto->ctx_pair[0])) {
			free(crypto);
			return(-1);
		}
		crypto->ctx = &crypto->ctx_pair[0];
		crypto->ctx->flags |= (HCRYPT_CTX_F_ANNOUNCE | HCRYPT_CTX_F_TTSEND);
		crypto->ctx->status = HCRYPT_CTX_S_ACTIVE;
	} else { /* Decoder */
		/* Configure contexts */
		if (hcryptCtx_Rx_Init(crypto, &crypto->ctx_pair[0], cfg)
		||  hcryptCtx_Rx_Init(crypto, &crypto->ctx_pair[1], cfg)) {
			free(crypto);
			return(-1);
		}
	}

	*phhc = (void *)crypto;
	return(0);
}

int HaiCrypt_Close(HaiCrypt_Handle hhc)
{
	hcrypt_Session *crypto = (hcrypt_Session *)hhc;
	int rc = -1;

	if (crypto) {
		if (crypto->cipher && crypto->cipher->close) crypto->cipher->close(crypto->cipher_data);
		free(crypto);
		rc = 0;
	}
	HCRYPT_LOG_EXIT();
	return rc;
}
