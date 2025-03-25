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



#include "logging.h"

using namespace std;


namespace srt_logging
{

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

void LogDispatcher::Update()
{
    bool enabled_in_fa = src_config->enabled_fa[fa];
    enabled = enabled_in_fa && level <= src_config->max_level;
}


// SendLogLine can be compiled normally. It's intermediately used by:
// - Proxy object, which is replaced by DummyProxy when !ENABLE_LOGGING
// - PrintLogLine, which has empty body when !ENABLE_LOGGING
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


#if ENABLE_LOGGING

LogDispatcher::Proxy::Proxy(LogDispatcher& guy) : that(guy), that_enabled(that.CheckEnabled())
{
    if (that_enabled)
    {
        i_file = "";
        i_line = 0;
        flags = that.src_config->flags;
        // Create logger prefix
        that.CreateLogLinePrefix(os);
    }
}

LogDispatcher::Proxy LogDispatcher::operator()()
{
    return Proxy(*this);
}

void LogDispatcher::CreateLogLinePrefix(std::ostringstream& serr)
{
    using namespace std;
    using namespace srt;

    SRT_STATIC_ASSERT(ThreadName::BUFSIZE >= sizeof("hh:mm:ss.") * 2, // multiply 2 for some margin
                      "ThreadName::BUFSIZE is too small to be used for strftime");
    char tmp_buf[ThreadName::BUFSIZE];
    if ( !isset(SRT_LOGF_DISABLE_TIME) )
    {
        // Not necessary if sending through the queue.
        timeval tv;
        gettimeofday(&tv, NULL);
        struct tm tm = SysLocalTime((time_t) tv.tv_sec);

        if (strftime(tmp_buf, sizeof(tmp_buf), "%X.", &tm))
        {
            serr << tmp_buf << setw(6) << setfill('0') << tv.tv_usec;
        }
    }

    string out_prefix;
    if ( !isset(SRT_LOGF_DISABLE_SEVERITY) )
    {
        out_prefix = prefix;
    }

    // Note: ThreadName::get needs a buffer of size min. ThreadName::BUFSIZE
    if ( !isset(SRT_LOGF_DISABLE_THREADNAME) && ThreadName::get(tmp_buf) )
    {
        serr << "/" << tmp_buf << out_prefix << ": ";
    }
    else
    {
        serr << out_prefix << ": ";
    }
}

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

} // (end namespace srt_logging)

