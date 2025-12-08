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

// This file contains the interface for the logging system and should be
// included in a file where you are going to use the logging instructions,
// also indirectly through the generated logger FA interface file.

// Usage:
//
// LOGC(gglog.Note, log << "There are  " << note_no << " notes.");
// LOGP(gglog.Note, "There are  ", note_no, " notes.");
//
// Where:
//
// LOGC/LOGP: The logger macro. This allows to turn logging off, if not HVU_ENABLE_LOGGING.
// *C: Use the iostream-style formatting with 'log' as the stream marker.
// *P: Use multiple arguments (note: for C++03 only one argument available).
//
// Note that the logger dispatchers ("Note" here) can be also called directly, but
// this way you can't control the logging at compile time (or you have to organize
// it somehow by yourself; this macro allows also to record __FILE__ and __LINE__
// of the log (although not used by the default format).
//
//    gglog.Note("There are ", note_no, " notes.");
// or
//    gglog.Note() << "There are " << note_no << " notes.";
//
// Formating with printf-style is partially supported, but you need to do your own
// wrapper for that, which will do something like:
//
// va_list ap;
// va_start(ap, args);
// gglog.Note.setloc(file, line, function).vform(format, ap);
// va_end(ap);
//
// (Note that file, line and function parameters should be extracted through
// the macro from __FILE__, __LINE__ and __function__ at the macro application).


#ifndef INC_HVU_LOGGING_H
#define INC_HVU_LOGGING_H

// This is for a case when compiling in C++03/C++98 mode.
// In this case you need to provide the definitions like
// below and define HVU_EXT_NOCXX11 to 1.

#ifndef HVU_EXT_NOCXX11
#define HVU_EXT_NOCXX11 0
#define HVU_EXT_INCLUDE_MUTEX <mutex>
#define HVU_EXT_INCLUDE_ATOMIC <atomic>
#endif

#include <iostream>
#include <iomanip>
#include <set>
#include <vector>
#include <cstdarg>

#ifdef HVU_EXT_INCLUDE_SYNC
#include HVU_EXT_INCLUDE_SYNC
#else
#include "hvu_sync.h"
#endif


#include <stdexcept>
#ifdef _WIN32
#include "win/wintime.h"
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif

#include "logging_api.h"
#include "ofmt.h"

#if !defined(HVU_ENABLE_LOGGING)
#define HVU_ENABLE_LOGGING 0
#endif

#if HVU_ENABLE_LOGGING

// GENERAL NOTE: All logger functions ADD THEIR OWN \n (EOL). Don't add any your own EOL character.
// The logging system may not add the EOL character, if appropriate flag was set in log settings.
// Anyway, treat the whole contents of eventually formatted message as exactly one line.

// LOGC uses an iostream-like syntax, using the special 'log' symbol.
// This symbol isn't visible outside the log macro parameters.
// Usage: LOGC(gglog.Debug, log << param1 << param2 << param3);
#define LOGC(logdes, args) if (logdes.IsEnabled()) \
{ \
    hvu::logging::LogDispatcher::Proxy log(logdes, __FILE__, __LINE__, __FUNCTION__); \
    { (void)(const hvu::logging::LogDispatcher::Proxy&)(args); } \
}

// LOGP is C++11 only OR with only one argument.
// Usage: LOGP(gglog.Debug, param1, param2, param3);
#define LOGP(logdes, ...) if (logdes.IsEnabled()) logdes.printloc(__FILE__, __LINE__, __FUNCTION__,##__VA_ARGS__)

#define IF_LOGGING(instr,...) instr,##__VA_ARGS__

#if HVU_ENABLE_HEAVY_LOGGING

#define HLOGC LOGC
#define HLOGP LOGP
#define IF_HEAVY_LOGGING IF_LOGGING

#else

#define HLOGC(...)
#define HLOGP(...)
#define IF_HEAVY_LOGGING(...) (void)0

#endif

#else // IF LOGGING DISABLED

#define LOGC(...)
#define LOGP(...)
#define HLOGC(...)
#define HLOGP(...)
#define IF_HEAVY_LOGGING(...) (void)0
#define IF_LOGGING(...) (void)0

#endif

namespace hvu
{
namespace logging
{

// The LogDispatcher class represents the object that is responsible for
// printing the log line.
class LogDispatcher
{
    friend class Logger;
    friend class LogConfig;

    int fa;
    LogLevel::type level;
    static const size_t MAX_PREFIX_SIZE = 32;
    const char* level_prefix; // ONLY STATIC CONSTANTS ALLOWED
    char prefix[MAX_PREFIX_SIZE+1];
    size_t prefix_len;
    HVU_EXT_ATOMIC <bool> enabled;
    class LogConfig* src_config;

    bool isset(int flg);

public:

    void set_prefix(const char* prefix);

    LogDispatcher(int functional_area, bool initially_enabled, class LogConfig& config, LogLevel::type log_level,
            const char* level_pfx, //NOTE: ONLY STATIC CONSTANTS ALLOWED!
            const char* logger_pfx = NULL);

    ~LogDispatcher();

    void Update();

    bool IsEnabled() { return enabled; }

    void CreateLogLinePrefix(hvu::ofmtbufstream&);
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

#if HVU_ENABLE_LOGGING
    struct Proxy
    {
        LogDispatcher& that;

        hvu::ofmtbufstream os;

        // CACHE!!!
        const char* i_file;
        int i_line;
        int flags;
        std::string area;

        // Left for future. Not sure if it's more convenient
        // to use this to translate __PRETTY_FUNCTION__ to
        // something short, or just let's leave __FUNCTION__
        // or better __func__.
        std::string ExtractName(std::string pretty_function);

        Proxy(LogDispatcher& guy);
        Proxy(LogDispatcher& guy, const char* f, int l, const std::string& a);

        // Copy constructor is needed due to noncopyable ostringstream.
        // This is used only in creation of the default object, so just
        // use the default values, just copy the location cache.
        Proxy(const Proxy& p)
            : that(p.that)
              , i_file(p.i_file)
              , i_line(p.i_line)
              , flags(p.flags)
              , area(p.area)
        {
        }

        template <class T>
        Proxy& operator<<(const T& arg) // predicted for temporary objects
        {
            if ( that.IsEnabled() )
            {
                os << arg;
            }
            return *this;
        }

        // Provide explicit overloads for const char* and string
        // so that printing them bypasses the formatting facility

        // Special case for atomics, as passing them to the fmt facility
        // requires unpacking the real underlying value.
        template <class T>
        Proxy& operator<<(const HVU_EXT_ATOMIC<T>& arg)
        {
            if (that.IsEnabled())
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
        void dispatch(const HVU_EXT_ATOMIC<Arg1>& a1, const Args&... others)
        {
            *this << a1.load();
            dispatch(others...);
        }

#endif
        ~Proxy()
        {
            if (that.IsEnabled())
            {
                if ((flags & HVU_LOGF_DISABLE_EOL) == 0)
                    os << OFMT_RAWSTR("\n"); // XXX would be nice to use a symbol for it

                that.SendLogLine(i_file, i_line, area, os.str());
            }
            // XXX Consider clearing the 'os' manually
        }

        Proxy& vform(const char* fmts, va_list ap);
    };


    friend struct Proxy;

    Proxy setloc(const char* f, int l, const std::string& a)
    {
        return Proxy(*this, f, l, a);
    }
    Proxy operator()() { return Proxy(*this); }

#else

    // Dummy proxy that does nothing
    struct DummyProxy
    {
        template <class T>
        DummyProxy& operator<<(const T& )
        {
            return *this;
        }

        DummyProxy& vform(const char*, va_list)
        {
            return *this;
        }

    };

    DummyProxy operator()()
    {
        return DummyProxy();
    }

    DummyProxy setloc(const char* , int , const std::string& )
    {
        return DummyProxy();
    }
#endif
};

// Proxy is the class provided for the sake of C++03 support
// using the << operator syntax. Ignore it if you only use
// the multi-parameter call.

#if HVU_ENABLE_LOGGING


#endif

class Logger
{
    friend class LogConfig;

    int m_fa;
    Logger(const std::string& idname, class LogConfig& config, bool initially_enabled, const char* logger_pfx, int forced_fa);

public:

    LogDispatcher Debug;
    LogDispatcher Note;
    LogDispatcher Warn;
    LogDispatcher Error;
    LogDispatcher Fatal;

    Logger(const std::string& idname, class LogConfig& config, bool initially_enabled, const char* logger_pfx = NULL);
    int id() const { return m_fa; }
};

class LogConfig
{
public:
    typedef std::vector<bool> fa_flags_t;
private:

    friend class Logger;
    friend class LogDispatcher;
    char initialized; // Marker to detect uninitialized object

    fa_flags_t enabled_fa;   // NOTE: assumed atomic reading
    LogLevel::type max_level; // NOTE: assumed atomic reading
    std::ostream* log_stream;
    HVU_LOG_HANDLER_FN* loghandler_fn;
    void* loghandler_opaque;
    mutable HVU_EXT_MUTEX config_lock;
    int flags;
    std::vector<class LogDispatcher*> loggers;

    // Index of 'names' and 'enabled_fa' come together
    // and they are numeric index of the logger.
    std::vector<std::string> names;

public:

    Logger general;

    size_t size() const { return names.size(); }

    const std::string& name(size_t ix) const
    {
        static const std::string& emp = "";
        return ix >= names.size() ? emp : names[ix];
    }

    int find_id(const std::string& name) const
    {
        // Linear search, but we state the number of FAs will be
        // relatively low and will only happen in the setup time
        // of the program.
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == name)
                return int(i);

        return -1;
    }

    // Setters
    void set_handler(void* opaque, HVU_LOG_HANDLER_FN* fn)
    {
        HVU_EXT_LOCKGUARD gg(config_lock);
        loghandler_fn = fn;
        loghandler_opaque = opaque;
    }

    void set_flags(int f)
    {
        HVU_EXT_LOCKGUARD gg(config_lock);
        flags = f;
    }

    void set_stream(std::ostream& str)
    {
        HVU_EXT_LOCKGUARD gg(config_lock);
        log_stream = &str;
    }

    void set_maxlevel(LogLevel::type l)
    {
        HVU_EXT_LOCKGUARD gg(config_lock);
        max_level = l;
        updateLoggersState();
    }

    void enable_fa(const std::string& name, bool enabled)
    {
        HVU_EXT_LOCKGUARD gg(config_lock);

        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == name)
            {
                enabled_fa[i] = enabled;
                break;
            }
    }

    // XXX You can add the use of std::array in C++11 mode.
    void enable_fa(const int* farray, size_t fs, bool enabled)
    {
        HVU_EXT_LOCKGUARD gg(config_lock);
        if (fs == 0)
        {
            if (enabled)
                enabled_fa = fa_flags_t(enabled_fa.size(), enabled);
            else
            {
                enabled_fa = fa_flags_t(enabled_fa.size(), enabled);

                // General can never be disabled.
                enabled_fa[0] = true;
            }
        }
        else
        {
            for (size_t i = 0; i < fs; ++i)
            {
                size_t fa = farray[i];
                if (fa < enabled_fa.size())
                    enabled_fa[fa] = enabled;
            }
        }
        updateLoggersState();
    }

    void setup_fa(const std::set<int>& selected)
    {
        HVU_EXT_LOCKGUARD gg(config_lock);
        for (size_t i = 0; i < enabled_fa.size(); ++i)
            enabled_fa[i] = bool(selected.count(i));
        updateLoggersState();
    }

    void setup_fa(const std::set<int>& selected, bool enabled)
    {
        HVU_EXT_LOCKGUARD gg(config_lock);
        std::set<int>::const_iterator i = selected.begin(), e = selected.end();
        for (; i != e; ++i)
            if (size_t(*i) < enabled_fa.size())
                enabled_fa[*i] = enabled;
        updateLoggersState();
    }

    int generate_fa_id(const std::string& name)
    {
        HVU_EXT_LOCKGUARD gg(config_lock);

        // 'names' and 'enabled_fa' grow together!
        size_t firstfree = names.size();
        names.push_back(name);
        enabled_fa.push_back(false);
        return int(firstfree);
    }

    LogConfig()
        : initialized(1) // global objects are 0-initialized
        , max_level(LogLevel::warning)
        , log_stream(&std::cerr)
        , loghandler_fn()
        , loghandler_opaque()
        , flags()
        , general("GENERAL", *this, true, "HVU.gg")
    {
        // XXX May do some verification code if LogConfig is
        // a global variable.
    }

    ~LogConfig()
    {
    }

    // XXX Add TSA markers for lock/unlock
    void lock() const { config_lock.lock(); }
    void unlock() const { config_lock.unlock(); }

    void subscribe(LogDispatcher*);
    void unsubscribe(LogDispatcher*);
    void updateLoggersState();
};

struct LogConfigSingleton
{
    LogConfig& instance()
    {
        static LogConfig this_instance;
        return this_instance;
    }
};

inline Logger::Logger(const std::string& idname, class LogConfig& config, bool initially_enabled, const char* logger_pfx):
        m_fa(config.generate_fa_id(idname)),
        Debug (m_fa, initially_enabled, config, LogLevel::debug, " D", logger_pfx),
        Note  (m_fa, initially_enabled, config, LogLevel::note,  ".N", logger_pfx),
        Warn  (m_fa, initially_enabled, config, LogLevel::warning, "!W", logger_pfx),
        Error (m_fa, initially_enabled, config, LogLevel::error, "*E", logger_pfx),
        Fatal (m_fa, initially_enabled, config, LogLevel::fatal, "!!FATAL!!", logger_pfx)
{
    if (!config.initialized)
    {
        // Global object initialization problem!
        throw std::runtime_error("Config object can be used only if declared in the same file");
    }
    config.enabled_fa[m_fa] = initially_enabled;
}

inline bool LogDispatcher::isset(int flg) { return (src_config->flags & flg) != 0; }


#if HAVE_CXX11

template <class... Args>
inline void LogDispatcher::PrintLogLine(const char* file, int line, const std::string& area, Args&&... args)
{
    (void)file;
    (void)line;
    (void)area;
#if HVU_ENABLE_LOGGING
    Proxy(*this).dispatch(args...);
#else
    (void)sizeof...(args);
#endif
}

#else // !HAVE_CXX11

template <class Arg>
inline void LogDispatcher::PrintLogLine(const char* file, int line, const std::string& area, const Arg& arg)
{
    (void)file;
    (void)line;
    (void)area;
#if HVU_ENABLE_LOGGING
    Proxy(*this) << arg;
#else
    (void)(arg);
#endif
}

#endif // HAVE_CXX11

}
}

#endif // INC_SRT_LOGGING_H
