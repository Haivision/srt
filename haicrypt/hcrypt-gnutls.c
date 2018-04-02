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

#include "hcrypt-gnutls.h"

int
hcrypt_aes_set_encrypt_key (const uint8_t *key,
                            unsigned bits,
                            struct aes_ctx *ctx)
{
  size_t key_length = bits / 8;

  if (!(key_length == 16 || key_length == 24 || key_length == 32)) {
    return -1;
  }

  aes_set_encrypt_key (ctx, bits / 8, key);
  return 0;
}

int
hcrypt_aes_set_decrypt_key (const uint8_t *key,
                            unsigned bits,
                            struct aes_ctx *ctx)
{
  size_t key_length = bits / 8;

  if (!(key_length == 16 || key_length == 24 || key_length == 32)) {
    return -1;
  }

  aes_set_decrypt_key (ctx, bits / 8, key);
  return 0;
}
