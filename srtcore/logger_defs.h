

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
 *****************************************************************************/

 /*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl
  DO NOT MODIFY.
 */


#ifndef INC_SRT_LOGGER_DEFS_H
#define INC_SRT_LOGGER_DEFS_H

#include "srt.h"
#include "logging.h"

namespace srt_logging
{
    struct AllFaOn
    {
        LogConfig::fa_bitset_t allfa;
        AllFaOn();
    };

    
    extern Logger glog;

    extern Logger mglog;
    extern Logger dlog;
    extern Logger tslog;
    extern Logger rxlog;

    extern Logger cclog;


} // namespace srt_logging

#endif


