/*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */


#include "logsupport.hpp"

LogFANames::LogFANames()
{
    Install("GENERAL", SRT_LOGFA_GENERAL);
    Install("SOCKMGMT", SRT_LOGFA_SOCKMGMT);
    Install("CONN", SRT_LOGFA_CONN);
    Install("XTIMER", SRT_LOGFA_XTIMER);
    Install("TSBPD", SRT_LOGFA_TSBPD);
    Install("RSRC", SRT_LOGFA_RSRC);

    Install("CONGEST", SRT_LOGFA_CONGEST);
    Install("PFILTER", SRT_LOGFA_PFILTER);

    Install("API_CTRL", SRT_LOGFA_API_CTRL);

    Install("QUE_CTRL", SRT_LOGFA_QUE_CTRL);

    Install("EPOLL_UPD", SRT_LOGFA_EPOLL_UPD);

    Install("API_RECV", SRT_LOGFA_API_RECV);
    Install("BUF_RECV", SRT_LOGFA_BUF_RECV);
    Install("QUE_RECV", SRT_LOGFA_QUE_RECV);
    Install("CHN_RECV", SRT_LOGFA_CHN_RECV);
    Install("GRP_RECV", SRT_LOGFA_GRP_RECV);

    Install("API_SEND", SRT_LOGFA_API_SEND);
    Install("BUF_SEND", SRT_LOGFA_BUF_SEND);
    Install("QUE_SEND", SRT_LOGFA_QUE_SEND);
    Install("CHN_SEND", SRT_LOGFA_CHN_SEND);
    Install("GRP_SEND", SRT_LOGFA_GRP_SEND);

    Install("INTERNAL", SRT_LOGFA_INTERNAL);

    Install("QUE_MGMT", SRT_LOGFA_QUE_MGMT);
    Install("CHN_MGMT", SRT_LOGFA_CHN_MGMT);
    Install("GRP_MGMT", SRT_LOGFA_GRP_MGMT);
    Install("EPOLL_API", SRT_LOGFA_EPOLL_API);
    Install("HAICRYPT", SRT_LOGFA_HAICRYPT);
    Install("APPLOG", SRT_LOGFA_APPLOG);
}
