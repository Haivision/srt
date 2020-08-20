/*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
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
