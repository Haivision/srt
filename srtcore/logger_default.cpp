/*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
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
