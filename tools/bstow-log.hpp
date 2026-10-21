#ifndef INC_BSTOW_LOG_H
#define INC_BSTOW_LOG_H

#include <string>

#include "ofmt_iostream.h"

namespace bstow
{
std::string ExtractFunctionName(const char* fnspec);
const int LL_ERROR = 0, LL_WARN = 1, LL_DEBUG = 2;
extern std::ostream* g_logstream;
extern int g_loglevel;

template<class... Args>
inline void LogLine(int lev, const std::string& func, const Args&... args)
{
    if (lev <= g_loglevel)
        hvu::ofprintl(*g_logstream, func, ": ", args...);
}
}


#if defined(BSLOG_ENABLED) && (BSLOG_ENABLED == 1)
#define BSLOG(level_suf, ...) bstow::LogLine(bstow::LL_##level_suf, bstow::ExtractFunctionName(__PRETTY_FUNCTION__), __VA_ARGS__)
#else
#define BSLOG(...) (void)0
#endif

#endif
