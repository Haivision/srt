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
