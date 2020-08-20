/*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
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
