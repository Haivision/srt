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

#ifndef INC_SRT_LOGGING_H
#define INC_SRT_LOGGING_H


#include <iostream>
#include <iomanip>
#include <set>
#include <cstdarg>
#ifdef _WIN32
#include "win/wintime.h"
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif

#include "srt.h"
#include "utilities.h"
#include "threadname.h"
#include "logging_api.h"
#include "sync.h"

#ifdef __GNUC__
#define PRINTF_LIKE __attribute__((format(printf,2,3)))
#else
#define PRINTF_LIKE 
#endif

#if ENABLE_LOGGING

// GENERAL NOTE: All logger functions ADD THEIR OWN \n (EOL). Don't add any your own EOL character.
// The logging system may not add the EOL character, if appropriate flag was set in log settings.
// Anyway, treat the whole contents of eventually formatted message as exactly one line.

// LOGC uses an iostream-like syntax, using the special 'log' symbol.
// This symbol isn't visible outside the log macro parameters.
// Usage: LOGC(gglog.Debug, log << param1 << param2 << param3);
#define LOGC(logdes, args) if (logdes.CheckEnabled()) \
{ \
    srt_logging::LogDispatcher::Proxy log(logdes); \
    log.setloc(__FILE__, __LINE__, __FUNCTION__); \
    { (void)(const srt_logging::LogDispatcher::Proxy&)(args); } \
}

// LOGF uses printf-like style formatting.
// Usage: LOGF(gglog.Debug, "%s: %d", param1.c_str(), int(param2));
// NOTE: LOGF is deprecated and should not be used
#define LOGF(logdes, ...) if (logdes.CheckEnabled()) logdes().setloc(__FILE__, __LINE__, __FUNCTION__).form(__VA_ARGS__)

// LOGP is C++11 only OR with only one string argument.
// Usage: LOGP(gglog.Debug, param1, param2, param3);
#define LOGP(logdes, ...) if (logdes.CheckEnabled()) logdes.printloc(__FILE__, __LINE__, __FUNCTION__,##__VA_ARGS__)

#define IF_LOGGING(instr) instr

#if ENABLE_HEAVY_LOGGING

#define HLOGC LOGC
#define HLOGP LOGP
#define HLOGF LOGF

#define IF_HEAVY_LOGGING(instr,...) instr,##__VA_ARGS__

#else

#define HLOGC(...)
#define HLOGF(...)
#define HLOGP(...)

#define IF_HEAVY_LOGGING(instr) (void)0

#endif

#else

#define LOGC(...)
#define LOGF(...)
#define LOGP(...)

#define HLOGC(...)
#define HLOGF(...)
#define HLOGP(...)

#define IF_HEAVY_LOGGING(instr) (void)0
#define IF_LOGGING(instr) (void)0

#endif

namespace srt_logging
{

struct LogConfig
{
    typedef std::bitset<SRT_LOGFA_LASTNONE+1> fa_bitset_t;
    fa_bitset_t enabled_fa;   // NOTE: assumed atomic reading
    LogLevel::type max_level; // NOTE: assumed atomic reading
    std::ostream* log_stream;
    SRT_LOG_HANDLER_FN* loghandler_fn;
    void* loghandler_opaque;
    srt::sync::Mutex mutex;
    int flags;

    LogConfig(const fa_bitset_t& efa,
            LogLevel::type l = LogLevel::warning,
            std::ostream* ls = &std::cerr)
        : enabled_fa(efa)
        , max_level(l)
        , log_stream(ls)
        , loghandler_fn()
        , loghandler_opaque()
        , flags()
    {
    }

    ~LogConfig()
    {
    }

    SRT_ATTR_ACQUIRE(mutex)
    void lock() { mutex.lock(); }

    SRT_ATTR_RELEASE(mutex)
    void unlock() { mutex.unlock(); }
};

// The LogDispatcher class represents the object that is responsible for
// a decision whether to log something or not, and if so, print the log.
struct SRT_API LogDispatcher
{
private:
    int fa;
    LogLevel::type level;
    static const size_t MAX_PREFIX_SIZE = 32;
    char prefix[MAX_PREFIX_SIZE+1];
    size_t prefix_len;
    LogConfig* src_config;

    bool isset(int flg) { return (src_config->flags & flg) != 0; }

public:

    LogDispatcher(int functional_area, LogLevel::type log_level, const char* your_pfx,
            const char* logger_pfx /*[[nullable]]*/, LogConfig& config):
        fa(functional_area),
        level(log_level),
        src_config(&config)
    {
        size_t your_pfx_len = your_pfx ? strlen(your_pfx) : 0;
        size_t logger_pfx_len = logger_pfx ? strlen(logger_pfx) : 0;

        if (logger_pfx && your_pfx_len + logger_pfx_len + 1 < MAX_PREFIX_SIZE)
        {
            memcpy(prefix, your_pfx, your_pfx_len);
            prefix[your_pfx_len] = ':';
            memcpy(prefix + your_pfx_len + 1, logger_pfx, logger_pfx_len);
            prefix[your_pfx_len + logger_pfx_len + 1] = '\0';
            prefix_len = your_pfx_len + logger_pfx_len + 1;
        }
        else if (your_pfx)
        {
            // Prefix too long, so copy only your_pfx and only
            // as much as it fits
            size_t copylen = std::min(+MAX_PREFIX_SIZE, your_pfx_len);
            memcpy(prefix, your_pfx, copylen);
            prefix[copylen] = '\0';
            prefix_len = copylen;
        }
        else
        {
            prefix[0] = '\0';
            prefix_len = 0;
        }
    }

    ~LogDispatcher()
    {
    }

    bool CheckEnabled();

    void CreateLogLinePrefix(srt::ofmtstream&);
    void SendLogLine(const char* file, int line, const std::string& area, const std::string& sl);

    // log.Debug("This is the ", nth, " time");  <--- C++11 only.
    // log.Debug() << "This is the " << nth << " time";  <--- C++03 available.

#if HAVE_CXX11

    template <class... Args>
    void PrintLogLine(const char* file, int line, const std::string& area, Args&&... args);

    template<class... Args>
    void operator()(Args&&... args)
    {
        PrintLogLine("UNKNOWN.c++", 0, "UNKNOWN", args...);
    }

    template<class... Args>
    void printloc(const char* file, int line, const std::string& area, Args&&... args)
    {
        PrintLogLine(file, line, area, args...);
    }
#else
    template <class Arg>
    void PrintLogLine(const char* file, int line, const std::string& area, const Arg& arg);

    // For C++03 (older) standard provide only with one argument.
    template <class Arg>
    void operator()(const Arg& arg)
    {
        PrintLogLine("UNKNOWN.c++", 0, "UNKNOWN", arg);
    }

    void printloc(const char* file, int line, const std::string& area, const std::string& arg1)
    {
        PrintLogLine(file, line, area, arg1);
    }
#endif

#if ENABLE_LOGGING

    struct Proxy;
    friend struct Proxy;

    Proxy operator()();
#else

    // Dummy proxy that does nothing
    struct DummyProxy
    {
        DummyProxy(LogDispatcher&)
        {
        }

        template <class T>
        DummyProxy& operator<<(const T& ) // predicted for temporary objects
        {
            return *this;
        }

        // DEPRECATED: DO NOT use LOGF/HLOGF macros anymore.
        // Use iostream-style formatting with LOGC or a direct argument with LOGP.
        SRT_ATR_DEPRECATED_PX DummyProxy& form(const char*, ...) SRT_ATR_DEPRECATED
        {
            return *this;
        }

        DummyProxy& vform(const char*, va_list)
        {
            return *this;
        }

        DummyProxy& setloc(const char* , int , std::string)
        {
            return *this;
        }
    };

    DummyProxy operator()()
    {
        return DummyProxy(*this);
    }

#endif

};

#if ENABLE_LOGGING

struct LogDispatcher::Proxy
{
    LogDispatcher& that;

    srt::ofmtstream os;

    // Cache the 'enabled' state in the beginning. If the logging
    // becomes enabled or disabled in the middle of the log, we don't
    // want it to be partially printed anyway.
    bool that_enabled;
    int flags;

    // CACHE!!!
    const char* i_file;
    int i_line;
    std::string area;

    Proxy& setloc(const char* f, int l, std::string a)
    {
        i_file = f;
        i_line = l;
        area = a;
        return *this;
    }

    // Left for future. Not sure if it's more convenient
    // to use this to translate __PRETTY_FUNCTION__ to
    // something short, or just let's leave __FUNCTION__
    // or better __func__.
    std::string ExtractName(std::string pretty_function);

    Proxy(LogDispatcher& guy);

    // Copy constructor is needed due to noncopyable ostringstream.
    // This is used only in creation of the default object, so just
    // use the default values, just copy the location cache.
    Proxy(const Proxy& p): that(p.that), area(p.area)
    {
        i_file = p.i_file;
        i_line = p.i_line;
        that_enabled = false;
        flags = p.flags;
    }


    template <class T>
    Proxy& operator<<(const T& arg) // predicted for temporary objects
    {
        if ( that_enabled )
        {
            os << arg;
        }
        return *this;
    }

    // Provide explicit overloads for const char* and string
    // so that printing them bypasses the formatting facility

    // Special case for atomics, as passing them to snprintf() call
    // requires unpacking the real underlying value.
    template <class T>
    Proxy& operator<<(const srt::sync::atomic<T>& arg)
    {
        if (that_enabled)
        {
            os << arg.load();
        }
        return *this;
    }


#if HAVE_CXX11

    void dispatch() {}

    template<typename Arg1, typename... Args>
    void dispatch(const Arg1& a1, const Args&... others)
    {
        *this << a1;
        dispatch(others...);
    }

    // Special dispatching for atomics must be provided here.
    // By some reason, "*this << a1" expression gets dispatched
    // to the general version of operator<<, not the overload for
    // atomic. Even though the compiler shows Arg1 type as atomic.
    template<typename Arg1, typename... Args>
    void dispatch(const srt::sync::atomic<Arg1>& a1, const Args&... others)
    {
        *this << a1.load();
        dispatch(others...);
    }

#endif

    ~Proxy()
    {
        if (that_enabled)
        {
            if ((flags & SRT_LOGF_DISABLE_EOL) == 0)
                //os << std::endl;
                os << OFMT_RAWSTR("\n");
            that.SendLogLine(i_file, i_line, area, os.str());
        }
        // Needed in destructor?
        //os.clear();
        //os.str("");
    }

    Proxy& form(const char* fmts, ...) PRINTF_LIKE
    {
        if ( !that_enabled )
            return *this;

        if ( !fmts || fmts[0] == '\0' )
            return *this;

        va_list ap;
        va_start(ap, fmts);
        vform(fmts, ap);
        va_end(ap);
        return *this;
    }

    Proxy& vform(const char* fmts, va_list ap)
    {
        char buf[512];

#if defined(_MSC_VER) && _MSC_VER < 1900
        _vsnprintf(buf, sizeof(buf) - 1, fmts, ap);
#else
        vsnprintf(buf, sizeof(buf), fmts, ap);
#endif
        size_t len = strlen(buf);
        if ( buf[len-1] == '\n' )
        {
            // Remove EOL character, should it happen to be at the end.
            // The EOL will be added at the end anyway.
            buf[len-1] = '\0';
        }

        os.write(buf, len);
        return *this;
    }
};


#endif

class Logger
{
    int m_fa;
    LogConfig& m_config;

public:

    LogDispatcher Debug;
    LogDispatcher Note;
    LogDispatcher Warn;
    LogDispatcher Error;
    LogDispatcher Fatal;

    Logger(int functional_area, LogConfig& config, const char* logger_pfx = NULL):
        m_fa(functional_area),
        m_config(config),
        Debug ( m_fa, LogLevel::debug, " D", logger_pfx, m_config ),
        Note  ( m_fa, LogLevel::note,  ".N", logger_pfx, m_config ),
        Warn  ( m_fa, LogLevel::warning, "!W", logger_pfx, m_config ),
        Error ( m_fa, LogLevel::error, "*E", logger_pfx, m_config ),
        Fatal ( m_fa, LogLevel::fatal, "!!FATAL!!", logger_pfx, m_config )
    {
    }
};

inline bool LogDispatcher::CheckEnabled()
{
    // Don't use enabler caching. Check enabled state every time.

    // These assume to be atomically read, so the lock is not needed
    // (note that writing to this field is still mutex-protected).
    // It's also no problem if the level was changed at the moment
    // when the enabler check is tested here. Worst case, the log
    // will be printed just a moment after it was turned off.
    const LogConfig* config = src_config; // to enforce using const operator[]
    int configured_enabled_fa = config->enabled_fa[fa];
    int configured_maxlevel = config->max_level;

    return configured_enabled_fa && level <= configured_maxlevel;
}


#if HAVE_CXX11

//extern std::mutex Debug_mutex;

inline void PrintArgs(std::ostringstream&) {}

template <class Arg1, class... Args>
inline void PrintArgs(std::ostringstream& serr, Arg1&& arg1, Args&&... args)
{
    serr << std::forward<Arg1>(arg1);
    PrintArgs(serr, args...);
}

// Add exceptional handling for sync::atomic
template <class Arg1, class... Args>
inline void PrintArgs(std::ostringstream& serr, const srt::sync::atomic<Arg1>& arg1, Args&&... args)
{
    serr << arg1.load();
    PrintArgs(serr, args...);
}

template <class... Args>
inline void LogDispatcher::PrintLogLine(const char* file SRT_ATR_UNUSED, int line SRT_ATR_UNUSED, const std::string& area SRT_ATR_UNUSED, Args&&... args SRT_ATR_UNUSED)
{
#ifdef ENABLE_LOGGING
    Proxy(*this).dispatch(args...);
#endif
}

#else // !HAVE_CXX11

template <class Arg>
inline void LogDispatcher::PrintLogLine(const char* file SRT_ATR_UNUSED, int line SRT_ATR_UNUSED, const std::string& area SRT_ATR_UNUSED, const Arg& arg SRT_ATR_UNUSED)
{
#ifdef ENABLE_LOGGING
    Proxy(*this) << arg;
#endif
}

#endif // HAVE_CXX11

// SendLogLine can be compiled normally. It's intermediately used by:
// - Proxy object, which is replaced by DummyProxy when !ENABLE_LOGGING
// - PrintLogLine, which has empty body when !ENABLE_LOGGING
inline void LogDispatcher::SendLogLine(const char* file, int line, const std::string& area, const std::string& msg)
{
    src_config->lock();
    if ( src_config->loghandler_fn )
    {
        (*src_config->loghandler_fn)(src_config->loghandler_opaque, int(level), file, line, area.c_str(), msg.c_str());
    }
    else if ( src_config->log_stream )
    {
        src_config->log_stream->write(msg.data(), msg.size());
        (*src_config->log_stream).flush();
    }
    src_config->unlock();
}

}

#endif // INC_SRT_LOGGING_H
