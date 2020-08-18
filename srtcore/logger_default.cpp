

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


#include "srt.h"
#include "logging.h"
#include "logger_defs.h"

namespace srt_logging
{
    AllFaOn::AllFaOn()
    {
        
        allfa.set(SRT_LOGFA_GENERAL, true);

        allfa.set(SRT_LOGFA_CONTROL, true);
        allfa.set(SRT_LOGFA_DATA, true);
        allfa.set(SRT_LOGFA_TSBPD, true);
        allfa.set(SRT_LOGFA_REXMIT, true);

        allfa.set(SRT_LOGFA_CONGEST, true);

    }
} // namespace srt_logging



