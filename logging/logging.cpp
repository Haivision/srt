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

#include <algorithm>
#include <iterator>
#include <vector>
#include <map>
#include <string>
#include <cctype>
#include <stdexcept>

#include "logging_api.h"
#include "logging.h"
#include "hvu_threadname.h"
#include "hvu_compat.h"


#if __cplusplus > 201100L
#define HVU_LOG_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define HVU_LOG_STATIC_ASSERT(cond, msg)
#endif

// MSVC likes to pollute things
#undef min
#undef max

using namespace std;

namespace hvu
{
namespace logging
{

// NOTE: names are used to be assigned to names,
// but hvu::logging uses only some significant ones
// (here with -> assigned dispatcher symbols):
//
// - fatal/crit    -> Fatal;
// - error/err     -> Error;
// - warning/warn  -> Warn;
// - note/notice   -> Note;
// - debug         -> Debug;
//
// Special trick to initialize it also in C++03 mode.
struct LevelNamesWrapper
{
    map<string, int> names;
    LevelNamesWrapper();
};

LevelNamesWrapper::LevelNamesWrapper()
{
    // This is based on codes taken from <sys/syslog.h>
    // This is POSIX standard, so it's not going to change.
    // Haivision standard only adds one more severity below
    // DEBUG named DEBUG_TRACE to satisfy all possible needs.

    // Using only values replicated in LogLevel::type
    names.insert(make_pair("crit", LOG_CRIT ));
    names.insert(make_pair("debug", LOG_DEBUG ));
    names.insert(make_pair("err", LOG_ERR ));
    names.insert(make_pair("error", LOG_ERR ));
    names.insert(make_pair("fatal", LOG_CRIT ));
    names.insert(make_pair("notice", LOG_NOTICE ));
    names.insert(make_pair("note", LOG_NOTICE ));
    names.insert(make_pair("warn", LOG_WARNING ));
    names.insert(make_pair("warning", LOG_WARNING ));
}

LogLevel::type parse_level(const std::string& name)
{
    // Values can be of more resolution than hvu::logging uses,
    // but it only puts the highest level value. Log messages are
    // enabled only if they are on that level or below.
    static LevelNamesWrapper level;

    map<string, int>::iterator i = level.names.find(name);
    if (i == level.names.end())
        return LogLevel::invalid;
    return LogLevel::type(i->second);
}

std::set<int> parse_fa(const hvu::logging::LogConfig& config, std::string fa, std::set<std::string>* punknown)
{
    set<int> fas;

    // The split algo won't work on empty string.
    if ( fa == "" )
        return fas;

    // To enable all FAs, you can call enable_fa() with zero array size.
    // But for the APIs that require particular FA IDs for various operations
    // they need to get the actual numbers.
    if ( fa == "all" )
    {
        // Start from 1 as general is always on.
        for (size_t i = 1; i < config.size(); ++i)
            fas.insert(i);

        return fas;
    }

    int (*ToLower)(int) = &std::tolower;
    transform(fa.begin(), fa.end(), fa.begin(), ToLower);

    vector<string> xfas;
    size_t pos = 0, ppos = 0;
    for (;;)
    {
        if ( fa[pos] != ',' )
        {
            ++pos;
            if ( pos < fa.size() )
                continue;
        }
        size_t n = pos - ppos;
        if ( n != 0 )
            xfas.push_back(fa.substr(ppos, n));
        ++pos;
        if ( pos >= fa.size() )
            break;
        ppos = pos;
    }

    for (size_t i = 0; i < xfas.size(); ++i)
    {
        fa = xfas[i];
        int faid = config.find_id(fa);
        if (faid == -1)
        {
            if (punknown)
                punknown->insert(fa); // If requested, add it back silently
            else
                cerr << "ERROR: Invalid log functional area spec: '" << fa << "' - skipping\n";
            continue;
        }

        fas.insert(faid);
    }

    return fas;
}


// Note: subscribe() and unsubscribe() functions are being called
// in the global constructor and destructor only, as the
// Logger objects (and inside them also their LogDispatcher)
// are being created. It's not predicted that LogDispatcher
// object are going to be created any other way than as
// global objects. Therefore the construction and destruction
// of them happens always in the main thread.

void LogConfig::subscribe(LogDispatcher* lg)
{
    vector<LogDispatcher*>::iterator p = std::find(loggers.begin(), loggers.end(), lg);
    if (p != loggers.end())
        return; // Do not register twice

    loggers.push_back(lg);
}

void LogConfig::unsubscribe(LogDispatcher* lg)
{
    vector<LogDispatcher*>::iterator p = std::find(loggers.begin(), loggers.end(), lg);
    if (p != loggers.end())
    {
        loggers.erase(p);
    }
}

// This function doesn't have any protection on itself,
// however the API functions from which it is called, call
// it already under a mutex protection.
void LogConfig::updateLoggersState()
{
    for (vector<LogDispatcher*>::iterator p = loggers.begin();
            p != loggers.end(); ++p)
    {
        (*p)->Update();
    }
}

LogDispatcher::LogDispatcher(int functional_area, bool initially_enabled,
        LogConfig& config, LogLevel::type log_level,
        const char* level_pfx, const char* logger_pfx /*[[nullable]]*/):
    fa(functional_area),
    level(log_level),
    level_prefix(level_pfx),
    enabled(initially_enabled),
    src_config(&config)
{
    // The Logger object and the config must be defined in the same
    // file because otherwise otherwise the order of initialization cannot
    // be ensured. 

    // We need to keep the user prefix and level prefix in one table.
    // So let's copy initially the level prefix. This one is not
    // allowed to be NULL.

    prefix_len = strlen(level_pfx);
    memcpy(prefix, level_pfx, prefix_len+1);

    set_prefix(logger_pfx);

    config.subscribe(this);
    Update();
}

void LogDispatcher::set_prefix(const char* logger_pfx)
{
    size_t level_pfx_len = level_prefix ? strlen(level_prefix) : 0;
    const size_t logger_pfx_len = logger_pfx ? strlen(logger_pfx) : 0;

    if (logger_pfx && level_pfx_len + logger_pfx_len + 1 < MAX_PREFIX_SIZE)
    {
        memcpy(prefix, level_prefix, level_pfx_len);
        prefix[level_pfx_len] = ':';
        memcpy(prefix + level_pfx_len + 1, logger_pfx, logger_pfx_len);
        prefix_len = level_pfx_len + logger_pfx_len + 1;
        prefix[prefix_len] = '\0';
    }
    else if (level_prefix)
    {
        // Prefix too long, so copy only level_pfx and only
        // as much as it fits
        size_t copylen = std::min(+MAX_PREFIX_SIZE, level_pfx_len);
        memcpy(prefix, level_prefix, copylen);
        prefix[copylen] = '\0';
        prefix_len = copylen;
    }
    else
    {
        prefix[0] = '\0';
        prefix_len = 0;
    }
}

LogDispatcher::~LogDispatcher()
{
    src_config->unsubscribe(this);
}

void LogDispatcher::Update()
{
    bool enabled_in_fa = src_config->enabled_fa[fa];
    enabled = enabled_in_fa && level <= src_config->max_level;
}


// SendLogLine can be compiled normally. It's intermediately used by:
// - Proxy object, which is replaced by DummyProxy when !HVU_ENABLE_LOGGING
// - PrintLogLine, which has empty body when !HVU_ENABLE_LOGGING
void LogDispatcher::SendLogLine(const char* file, int line, const std::string& area, const std::string& msg)
{
    src_config->lock();
    if ( src_config->loghandler_fn )
    {
        (*src_config->loghandler_fn)(src_config->loghandler_opaque, int(level), file, line, area.c_str(), msg.c_str());
    }
    else if ( src_config->log_stream )
    {
        src_config->log_stream->write(msg.data(), msg.size());
        src_config->log_stream->flush();
    }
    src_config->unlock();
}


#if HVU_ENABLE_LOGGING

LogDispatcher::Proxy::Proxy(LogDispatcher& guy)
    : that(guy)
    , i_file("")
    , i_line(0)
    , flags(that.src_config->flags)
{
    if (that.IsEnabled())
    {
        // Create logger prefix
        that.CreateLogLinePrefix(os);
    }
}

LogDispatcher::Proxy::Proxy(LogDispatcher& guy, const char* f, int l, const std::string& a)
    : that(guy)
    , i_file(f)
    , i_line(l)
    , flags(that.src_config->flags)
{
    if (that.IsEnabled())
    {
        area = a;
        // Create logger prefix
        that.CreateLogLinePrefix(os);
    }
}

LogDispatcher::Proxy& LogDispatcher::Proxy::vform(const char* fmts, va_list ap)
{
    static const int BUFLEN = 512;
    char buf[BUFLEN];

#if defined(_MSC_VER) && _MSC_VER < 1900
    int wlen = _vsnprintf(buf, BUFLEN - 1, fmts, ap);
#else
    int wlen = vsnprintf(buf, BUFLEN, fmts, ap);
#endif

    if (wlen < 1) // catch both 0 and -1
    {
        // ERROR when formatting
        const char msg[] = "<ERROR>";
        os.write(msg, sizeof (msg));
        return *this;
    }

    // vsnprintf returns the number of characters printed,
    // or the size of required buffer, if the buffer was too small
    // and it resulted in a truncated string. Ignore truncation,
    // just make sure that terminating 0 was properly specified.
    size_t len = wlen >= BUFLEN ? BUFLEN - 1 : wlen;
    if ( buf[len-1] == '\n' )
    {
        // Remove EOL character, should it happen to be at the end.
        // The EOL will be added at the end anyway.
        --len;
    }

    os.write(buf, len);
    return *this;
}

void LogDispatcher::CreateLogLinePrefix(hvu::ofmtbufstream& serr)
{
    using namespace std;
    using namespace hvu;

    HVU_LOG_STATIC_ASSERT(hvu::ThreadName::BUFSIZE >= sizeof("hh:mm:ss.") * 2, // multiply 2 for some margin
                      "ThreadName::BUFSIZE is too small to be used for strftime");
    char tmp_buf[ThreadName::BUFSIZE];
    if (!isset(HVU_LOGF_DISABLE_TIME))
    {
        // Not necessary if sending through the queue.
        timeval tv;
        gettimeofday(&tv, NULL);
        struct tm tm = hvu::SysLocalTime((time_t) tv.tv_sec);

        if (strftime(tmp_buf, sizeof(tmp_buf), "%X.", &tm))
        {
            serr << tmp_buf << fmt(tv.tv_usec, fmtc().fillzero().width(6));
        }
    }

    // Note: ThreadName::get needs a buffer of size min. ThreadName::BUFSIZE
    if (!isset(HVU_LOGF_DISABLE_THREADNAME) && ThreadName::get(tmp_buf))
    {
        serr << OFMT_RAWSTR("/") << tmp_buf;
    }

    if (!isset(HVU_LOGF_DISABLE_SEVERITY))
    {
        serr.write(prefix, prefix_len); // include terminal 0
    }

    serr << OFMT_RAWSTR(": ");
}

#undef HVU_LOG_STATIC_ASSERT

std::string LogDispatcher::Proxy::ExtractName(std::string pretty_function)
{
    if ( pretty_function == "" )
        return "";
    size_t pos = pretty_function.find('(');
    if ( pos == std::string::npos )
        return pretty_function; // return unchanged.

    pretty_function = pretty_function.substr(0, pos);

    // There are also template instantiations where the instantiating
    // parameters are encrypted inside. Therefore, search for the first
    // open < and if found, search for symmetric >.

    int depth = 1;
    pos = pretty_function.find('<');
    if ( pos != std::string::npos )
    {
        size_t end = pos+1;
        for(;;)
        {
            ++pos;
            if ( pos == pretty_function.size() )
            {
                --pos;
                break;
            }
            if ( pretty_function[pos] == '<' )
            {
                ++depth;
                continue;
            }

            if ( pretty_function[pos] == '>' )
            {
                --depth;
                if ( depth <= 0 )
                    break;
                continue;
            }
        }

        std::string afterpart = pretty_function.substr(pos+1);
        pretty_function = pretty_function.substr(0, end) + ">" + afterpart;
    }

    // Now see how many :: can be found in the name.
    // If this occurs more than once, take the last two.
    pos = pretty_function.rfind("::");

    if ( pos == std::string::npos || pos < 2 )
        return pretty_function; // return whatever this is. No scope name.

    // Find the next occurrence of :: - if found, copy up to it. If not,
    // return whatever is found.
    pos -= 2;
    pos = pretty_function.rfind("::", pos);
    if ( pos == std::string::npos )
        return pretty_function; // nothing to cut

    return pretty_function.substr(pos+2);
}
#endif

}} // (end namespace hvu::logging)

