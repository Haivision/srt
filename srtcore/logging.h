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


struct LogDispatcher
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

    // XXX this is C++03 solution only. Use unique_ptr in C++11.
    // It must be done with dynamic ostringstream because ostringstream
    // is not copyable.
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
    std::string ExtractName(std::string pretty_function)
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

    Proxy(LogDispatcher& guy): that(guy), that_enabled(that.CheckEnabled())
    {
        flags = that.flags;
        if ( that_enabled )
        {
            // Create logger prefix
            that.CreateLogLinePrefix(os);
        }
    }

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

inline LogDispatcher::Proxy LogDispatcher::operator()()
{
    LogDispatcher& that = *this;

    Proxy proxy = that;
    return proxy;
}

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

inline std::string FormatTime(uint64_t time)
{
    using namespace std;

    time_t sec = time/1000000;
    time_t usec = time%1000000;

    time_t tt = sec;
    struct tm tm = LocalTime(tt);

    char tmp_buf[512];
#ifdef WIN32
    strftime(tmp_buf, 512, "%Y-%m-%d.", &tm);
#else
    strftime(tmp_buf, 512, "%T.", &tm);
#endif
    ostringstream out;
    out << tmp_buf << setfill('0') << setw(6) << usec;
    return out.str();
}

inline void LogDispatcher::CreateLogLinePrefix(std::ostringstream& serr)
{
    using namespace std;

    char tmp_buf[512];
    if ( (flags & SRT_LOGF_DISABLE_TIME) == 0 )
    {
        // Not necessary if sending through the queue.
        timeval tv;
        gettimeofday(&tv, 0);
        time_t t = tv.tv_sec;
        struct tm tm = LocalTime(t);
        strftime(tmp_buf, 512, "%T.", &tm);

        serr << tmp_buf << setw(6) << setfill('0') << tv.tv_usec;
    }

    // Note: ThreadName::get needs a buffer of size min. ThreadName::BUFSIZE
    string out_prefix;
    if ( (flags & SRT_LOGF_DISABLE_SEVERITY) == 0 )
    {
        out_prefix = prefix;
    }

    if ( (flags & SRT_LOGF_DISABLE_THREADNAME) == 0 && ThreadName::get(tmp_buf) )
    {
        serr << "/" << tmp_buf << out_prefix << ": ";
    }
    else
    {
        serr << out_prefix << ": ";
    }
}

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
inline void LogDispatcher::PrintLogLine(const char* file, int line, const std::string& area, Args&&... args)
{
    std::ostringstream serr;
    CreateLogLinePrefix(serr);
    PrintArgs(serr, args...);

    if ( (flags & SRT_LOGF_DISABLE_EOL) == 0 )
        serr << std::endl;

    // Not sure, but it wasn't ever used.
    SendLogLine(file, line, area, serr.str());
}

#else

template <class Arg>
inline void LogDispatcher::PrintLogLine(const char* file, int line, const std::string& area, const Arg& arg)
{
    std::ostringstream serr;
    CreateLogLinePrefix(serr);
    serr << arg;

    if ( (flags & SRT_LOGF_DISABLE_EOL) == 0 )
        serr << std::endl;

    // Not sure, but it wasn't ever used.
    SendLogLine(file, line, area, serr.str());
}

#endif

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
