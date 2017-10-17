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

#include "cryptographic-internal.h"

int
AES_set_encrypt_key (const uint8_t *key,
                     unsigned bits GNUC_UNUSED,
                     AES_KEY *aeskey)
{
  aes128_set_encrypt_key(aeskey, key);
  return 0;
}

int
AES_set_decrypt_key (const uint8_t *key,
                     unsigned bits GNUC_UNUSED,
                     AES_KEY *aeskey)
{
  aes128_set_decrypt_key(aeskey, key);
  return 0;
}
