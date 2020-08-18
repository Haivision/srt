#include "logging.h"

namespace srt_logging
{

struct AllFaOn
{
    LogConfig::fa_bitset_t allfa;

    AllFaOn()
    {
        //        allfa.set(SRT_LOGFA_BSTATS, true);
        allfa.set(SRT_LOGFA_CONTROL, true);
        allfa.set(SRT_LOGFA_DATA, true);
        allfa.set(SRT_LOGFA_TSBPD, true);
        allfa.set(SRT_LOGFA_REXMIT, true);
        allfa.set(SRT_LOGFA_CONGEST, true);
#if ENABLE_HAICRYPT_LOGGING
        allfa.set(SRT_LOGFA_HAICRYPT, true);
#endif
    }
} logger_fa_all;

} // namespace srt_logging

// We need it outside the namespace to preserve the global name.
// It's a part of "hidden API" (used by applications)
SRT_API srt_logging::LogConfig srt_logger_config(srt_logging::logger_fa_all.allfa);

namespace srt_logging
{

Logger glog(SRT_LOGFA_GENERAL, srt_logger_config, "SRT.g");
// Unused. If not found useful, maybe reuse for another FA.
// Logger blog(SRT_LOGFA_BSTATS, srt_logger_config, "SRT.b");
Logger mglog(SRT_LOGFA_CONTROL, srt_logger_config, "SRT.c");
Logger dlog(SRT_LOGFA_DATA, srt_logger_config, "SRT.d");
Logger tslog(SRT_LOGFA_TSBPD, srt_logger_config, "SRT.t");
Logger rxlog(SRT_LOGFA_REXMIT, srt_logger_config, "SRT.r");
Logger cclog(SRT_LOGFA_CONGEST, srt_logger_config, "SRT.cc");

} // namespace srt_logging