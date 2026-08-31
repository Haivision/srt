/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2027 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_HVU_DEBUG_H
#define INC_HVU_DEBUG_H

#if defined(__x86_64__) || defined(__i386__)
    #define HVU_DEBUG_BREAK() __asm__ volatile("int $3")
#elif defined(__aarch64__) || defined(__arm__)
    #define HVU_DEBUG_BREAK() __asm__ volatile("brk #0")
#else
    #include <signal.h>
    #define HVU_DEBUG_BREAK() raise(SIGTRAP) // Fallback for other architectures
#endif

#endif
