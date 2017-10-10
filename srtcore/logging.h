/*****************************************************************************
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>
 * 
 *****************************************************************************/

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC__SRT_LOGGING_H
#define INC__SRT_LOGGING_H


#include <iostream>
#include <iomanip>
#include <set>
#include <sstream>
#include <cstdarg>
#ifdef WIN32
#include "win/wintime.h"
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif
#include <pthread.h>
#if HAVE_CXX11
#include <mutex>
#endif

#include "srt.h"
#include "utilities.h"
#include "threadname.h"
#include "logging_api.h"
#include "srt_compat.h"

#ifdef __GNUC__
#define PRINTF_LIKE __attribute__((format(printf,2,3)))
#else
#define PRINTF_LIKE 
#endif

// Usage: LOGC(mglog.Debug) << param1 << param2 << param3;
#define LOGC(logdes) logdes().setloc(__FILE__, __LINE__, __FUNCTION__)
// LOGP is C++11 only OR with only one string argument.
// Usage: LOGP(mglog.Debug, param1, param2, param3);
#define LOGP(logdes, ...) logdes.printloc(__FILE__, __LINE__, __FUNCTION__,##__VA_ARGS__)

namespace logging
{

struct LogConfig
{
    std::set<int> enabled_fa;
    LogLevel::type max_level;
    std::ostream* log_stream;
    SRT_LOG_HANDLER_FN* loghandler_fn;
    void* loghandler_opaque;
    pthread_mutex_t mutex;
    int flags;

    LogConfig(const std::set<int>& initial_fa):
        enabled_fa(initial_fa),
        max_level(LogLevel::warning),
        log_stream(&std::cerr)
    {
        pthread_mutex_init(&mutex, 0);
    }
    LogConfig(const std::set<int> efa, LogLevel::type l, std::ostream* ls):
        enabled_fa(efa), max_level(l), log_stream(ls)
    {
        pthread_mutex_init(&mutex, 0);
    }

    ~LogConfig()
    {
        pthread_mutex_destroy(&mutex);
    }

    void lock() { pthread_mutex_lock(&mutex); }
    void unlock() { pthread_mutex_unlock(&mutex); }
};


struct SRT_API LogDispatcher
{
    int fa;
    LogLevel::type level;
    std::string prefix;
    bool enabled;
    LogConfig* src_config;
    int flags; // copy of config flags as this must be accessed once.
    pthread_mutex_t mutex;

    LogDispatcher(int functional_area, LogLevel::type log_level, const std::string& pfx, LogConfig* config):
        fa(functional_area),
        level(log_level),
        prefix(pfx),
        enabled(false),
        src_config(config)
    {
        pthread_mutex_init(&mutex, 0);
    }

    ~LogDispatcher()
    {
        pthread_mutex_destroy(&mutex);
    }

    bool CheckEnabled();
    LogDispatcher(bool v): enabled(v) {}

    void CreateLogLinePrefix(std::ostringstream&);
    void SendLogLine(const char* file, int line, const std::string& area, const std::string& sl);

    // log.Debug("This is the ", nth, " time");  <--- C++11 only.
    // log.Debug() << "This is the " << nth << " time";  <--- C++03 available.

#if HAVE_CXX11

    template <class... Args>
    void PrintLogLine(const char* file, int line, const std::string& area, Args&&... args);

    template<class Arg1, class... Args>
    void operator()(Arg1&& arg1, Args&&... args)
    {
        if ( CheckEnabled() )
        {
            PrintLogLine("UNKNOWN.c++", 0, "UNKNOWN", arg1, args...);
        }
    }

    template<class Arg1, class... Args>
    void printloc(const char* file, int line, const std::string& area, Arg1&& arg1, Args&&... args)
    {
        if ( CheckEnabled() )
        {
            PrintLogLine(file, line, area, arg1, args...);
        }
    }
#else
    template <class Arg>
    void PrintLogLine(const char* file, int line, const std::string& area, const Arg& arg);

    // For old C++ standard provide only with one argument.
    template <class Arg>
    void operator()(const Arg& arg)
    {
        if ( CheckEnabled() )
        {
            PrintLogLine("UNKNOWN.c++", 0, "UNKNOWN", arg);
        }
    }

    void printloc(const char* file, int line, const std::string& area, const std::string& arg1)
    {
        if ( CheckEnabled() )
        {
            PrintLogLine(file, line, area, arg1);
        }
    }
#endif

#if ENABLE_LOGGING

    struct Proxy;

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

        DummyProxy& form(const char*, ...)
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

    std::ostringstream os;

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
        flags = that.flags;
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

    ~Proxy()
    {
        if ( that_enabled )
        {
            if ( (flags & SRT_LOGF_DISABLE_EOL) == 0 )
                os << std::endl;
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

        char buf[512];
        va_list ap;
        va_start(ap, fmts);
        vsprintf(buf, fmts, ap);
        va_end(ap);
        size_t len = strlen(buf);
        if ( buf[len-1] == '\n' )
        {
            // Remove EOL character, should it happen to be at the end.
            // The EOL will be added at the end anyway.
            buf[len-1] = '\0';
        }

        os << buf;
        return *this;
    }
};


#endif

class Logger
{
    std::string m_prefix;
    int m_fa;
    //bool enabled = false;
    LogConfig* m_config;

public:

    LogDispatcher Debug;
    LogDispatcher Note;
    LogDispatcher Warn;
    LogDispatcher Error;
    LogDispatcher Fatal;

    Logger(int functional_area, LogConfig* config, std::string globprefix = std::string()):
        m_prefix( globprefix == "" ? globprefix : ": " + globprefix),
        m_fa(functional_area),
        m_config(config),
        Debug ( m_fa, LogLevel::debug, " D" + m_prefix, m_config ),
        Note  ( m_fa, LogLevel::note,  ".N" + m_prefix, m_config ),
        Warn  ( m_fa, LogLevel::warning, "!W" + m_prefix, m_config ),
        Error ( m_fa, LogLevel::error, "*E" + m_prefix, m_config ),
        Fatal ( m_fa, LogLevel::fatal, "!!FATAL!!" + m_prefix, m_config )
    {
    }

};

inline bool LogDispatcher::CheckEnabled()
{
    // Don't use enabler caching. Check enabled state every time.
    bool enabled = false;
    src_config->lock();

        // If the thread is interrupted during any of this process, in worst case 
        // we'll just overwrite the already set values with the same.
    enabled = src_config->enabled_fa.count(fa) && level <= src_config->max_level;
    flags = src_config->flags;
    
    src_config->unlock();

    return enabled;
}

SRT_API std::string FormatTime(uint64_t time);

#if HAVE_CXX11

//extern std::mutex Debug_mutex;

inline void PrintArgs(std::ostream&) {}

template <class Arg1, class... Args>
inline void PrintArgs(std::ostream& serr, Arg1&& arg1, Args&&... args)
{
    serr << arg1;
    PrintArgs(serr, args...);
}

template <class... Args>
inline void LogDispatcher::PrintLogLine(const char* file ATR_UNUSED, int line ATR_UNUSED, const std::string& area ATR_UNUSED, Args&&... args ATR_UNUSED)
{
#ifdef ENABLE_LOGGING
    std::ostringstream serr;
    CreateLogLinePrefix(serr);
    PrintArgs(serr, args...);

    if ( (flags & SRT_LOGF_DISABLE_EOL) == 0 )
        serr << std::endl;

    // Not sure, but it wasn't ever used.
    SendLogLine(file, line, area, serr.str());
#endif
}

#else

template <class Arg>
inline void LogDispatcher::PrintLogLine(const char* file ATR_UNUSED, int line ATR_UNUSED, const std::string& area ATR_UNUSED, const Arg& arg ATR_UNUSED)
{
#ifdef ENABLE_LOGGING
    std::ostringstream serr;
    CreateLogLinePrefix(serr);
    serr << arg;

    if ( (flags & SRT_LOGF_DISABLE_EOL) == 0 )
        serr << std::endl;

    // Not sure, but it wasn't ever used.
    SendLogLine(file, line, area, serr.str());
#endif
}

#endif

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
        (*src_config->log_stream) << msg;
    }
    src_config->unlock();
}

}

#endif
