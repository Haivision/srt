/*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */


#include "srt.h"
#include "logging.h"
#include "logger_defs.h"

namespace srt::logging
{
    AllFaOn::AllFaOn()
    {
        allfa[SRT_LOGFA_GENERAL] = true;
        allfa[SRT_LOGFA_SOCKMGMT] = true;
        allfa[SRT_LOGFA_CONN] = true;
        allfa[SRT_LOGFA_XTIMER] = true;
        allfa[SRT_LOGFA_TSBPD] = true;
        allfa[SRT_LOGFA_RSRC] = true;

        allfa[SRT_LOGFA_CONGEST] = true;
        allfa[SRT_LOGFA_PFILTER] = true;

        allfa[SRT_LOGFA_API_CTRL] = true;

        allfa[SRT_LOGFA_QUE_CTRL] = true;

        allfa[SRT_LOGFA_EPOLL_UPD] = true;

        allfa[SRT_LOGFA_API_RECV] = true;
        allfa[SRT_LOGFA_BUF_RECV] = true;
        allfa[SRT_LOGFA_QUE_RECV] = true;
        allfa[SRT_LOGFA_CHN_RECV] = true;
        allfa[SRT_LOGFA_GRP_RECV] = true;

        allfa[SRT_LOGFA_API_SEND] = true;
        allfa[SRT_LOGFA_BUF_SEND] = true;
        allfa[SRT_LOGFA_QUE_SEND] = true;
        allfa[SRT_LOGFA_CHN_SEND] = true;
        allfa[SRT_LOGFA_GRP_SEND] = true;

        allfa[SRT_LOGFA_INTERNAL] = true;

        allfa[SRT_LOGFA_QUE_MGMT] = true;
        allfa[SRT_LOGFA_CHN_MGMT] = true;
        allfa[SRT_LOGFA_GRP_MGMT] = true;
        allfa[SRT_LOGFA_EPOLL_API] = true;
    }
} // namespace srt::logging
