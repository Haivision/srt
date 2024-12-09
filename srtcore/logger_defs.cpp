/*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */


#include "srt.h"
#include "logging.h"
#include "logger_defs.h"

// We need it outside the namespace to preserve the global name.
// It's a part of "hidden API" (used by applications)
SRT_API srt::logging::LogConfig srt_logger_config(SRT_LOGFA_LASTNONE+1);

namespace srt::logging
{
    Logger gglog(SRT_LOGFA_GENERAL, true, srt_logger_config, "SRT.gg");
    Logger smlog(SRT_LOGFA_SOCKMGMT, true, srt_logger_config, "SRT.sm");
    Logger cnlog(SRT_LOGFA_CONN, true, srt_logger_config, "SRT.cn");
    Logger xtlog(SRT_LOGFA_XTIMER, true, srt_logger_config, "SRT.xt");
    Logger tslog(SRT_LOGFA_TSBPD, true, srt_logger_config, "SRT.ts");
    Logger rslog(SRT_LOGFA_RSRC, true, srt_logger_config, "SRT.rs");

    Logger cclog(SRT_LOGFA_CONGEST, true, srt_logger_config, "SRT.cc");
    Logger pflog(SRT_LOGFA_PFILTER, true, srt_logger_config, "SRT.pf");

    Logger aclog(SRT_LOGFA_API_CTRL, true, srt_logger_config, "SRT.ac");

    Logger qclog(SRT_LOGFA_QUE_CTRL, true, srt_logger_config, "SRT.qc");

    Logger eilog(SRT_LOGFA_EPOLL_UPD, true, srt_logger_config, "SRT.ei");

    Logger arlog(SRT_LOGFA_API_RECV, true, srt_logger_config, "SRT.ar");
    Logger brlog(SRT_LOGFA_BUF_RECV, true, srt_logger_config, "SRT.br");
    Logger qrlog(SRT_LOGFA_QUE_RECV, true, srt_logger_config, "SRT.qr");
    Logger krlog(SRT_LOGFA_CHN_RECV, true, srt_logger_config, "SRT.kr");
    Logger grlog(SRT_LOGFA_GRP_RECV, true, srt_logger_config, "SRT.gr");

    Logger aslog(SRT_LOGFA_API_SEND, true, srt_logger_config, "SRT.as");
    Logger bslog(SRT_LOGFA_BUF_SEND, true, srt_logger_config, "SRT.bs");
    Logger qslog(SRT_LOGFA_QUE_SEND, true, srt_logger_config, "SRT.qs");
    Logger kslog(SRT_LOGFA_CHN_SEND, true, srt_logger_config, "SRT.ks");
    Logger gslog(SRT_LOGFA_GRP_SEND, true, srt_logger_config, "SRT.gs");

    Logger inlog(SRT_LOGFA_INTERNAL, true, srt_logger_config, "SRT.in");

    Logger qmlog(SRT_LOGFA_QUE_MGMT, true, srt_logger_config, "SRT.qm");
    Logger kmlog(SRT_LOGFA_CHN_MGMT, true, srt_logger_config, "SRT.km");
    Logger gmlog(SRT_LOGFA_GRP_MGMT, true, srt_logger_config, "SRT.gm");
    Logger ealog(SRT_LOGFA_EPOLL_API, true, srt_logger_config, "SRT.ea");
} // namespace srt::logging
