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


#include "logsupport.hpp"

LogFANames::LogFANames()
{
    Install("GENERAL", SRT_LOGFA_GENERAL);

    Install("CONTROL", SRT_LOGFA_CONTROL);
    Install("DATA", SRT_LOGFA_DATA);
    Install("TSBPD", SRT_LOGFA_TSBPD);
    Install("REXMIT", SRT_LOGFA_REXMIT);

    Install("CONGEST", SRT_LOGFA_CONGEST);
    Install("HAICRYPT", SRT_LOGFA_HAICRYPT);
}
