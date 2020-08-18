/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include "logsupport.hpp"
#include "../srtcore/srt.h"
#include "../srtcore/utilities.h"

using namespace std;

// This is based on codes taken from <sys/syslog.h>
// This is POSIX standard, so it's not going to change.
// Haivision standard only adds one more severity below
// DEBUG named DEBUG_TRACE to satisfy all possible needs.

map<string, int> srt_level_names
{
    { "alert", LOG_ALERT },
    { "crit", LOG_CRIT },
    { "debug", LOG_DEBUG },
    { "emerg", LOG_EMERG },
    { "err", LOG_ERR },
    { "error", LOG_ERR },		/* DEPRECATED */
    { "fatal", LOG_CRIT }, // XXX Added for SRT
    { "info", LOG_INFO },
    // WTF? Undefined symbol? { "none", INTERNAL_NOPRI },		/* INTERNAL */
    { "notice", LOG_NOTICE },
    { "note", LOG_NOTICE }, // XXX Added for SRT
    { "panic", LOG_EMERG },		/* DEPRECATED */
    { "warn", LOG_WARNING },		/* DEPRECATED */
    { "warning", LOG_WARNING },
    //{ "", -1 }
};



srt_logging::LogLevel::type SrtParseLogLevel(string level)
{
    using namespace srt_logging;

    if ( level.empty() )
        return LogLevel::fatal;

    if ( isdigit(level[0]) )
    {
        long lev = strtol(level.c_str(), 0, 10);
        if ( lev >= SRT_LOG_LEVEL_MIN && lev <= SRT_LOG_LEVEL_MAX )
            return LogLevel::type(lev);

        cerr << "ERROR: Invalid loglevel number: " << level << " - fallback to FATAL\n";
        return LogLevel::fatal;
    }

    int (*ToLower)(int) = &std::tolower; // manual overload resolution
    transform(level.begin(), level.end(), level.begin(), ToLower);

    auto i = srt_level_names.find(level);
    if ( i == srt_level_names.end() )
    {
        cerr << "ERROR: Invalid loglevel spec: " << level << " - fallback to FATAL\n";
        return LogLevel::fatal;
    }

    return LogLevel::type(i->second);
}

struct ToLowerFormat
{
    char operator()(char in)
    {
        if (islower(in))
            return in;
        if (isupper(in))
            return tolower(in);
        if (in == '_')
            return '-';

        throw std::invalid_argument("Wrong FA name - please check the definition in scripts/generate-logging-defs.tcl file");
    }
};

void LogFANames::Install(string upname, int value)
{
    string id;
    transform(upname.begin(), upname.end(), back_inserter(id), ToLowerFormat());
    namemap[id] = value;
}

// See logsupport_appdefs.cpp for log FA definitions
LogFANames srt_transmit_logfa_names;

const map<string, int> SrtLogFAList()
{
    return srt_transmit_logfa_names.namemap;
}

set<srt_logging::LogFA> SrtParseLogFA(string fa, set<string>* punknown)
{
    using namespace srt_logging;

    set<LogFA> fas;

    // The split algo won't work on empty string.
    if ( fa == "" )
        return fas;

    auto& names = srt_transmit_logfa_names.namemap;

    if ( fa == "all" )
    {
        for (auto entry: names)
        {
            // Skip "general", it's always on
            if (entry.first == "general")
                continue;
            fas.insert(entry.second);
        }
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
        int* pfa = map_getp(names, fa);
        if (!pfa)
        {
            if (punknown)
                punknown->insert(fa); // If requested, add it back silently
            else
                cerr << "ERROR: Invalid log functional area spec: '" << fa << "' - skipping\n";
            continue;
        }

        fas.insert(*pfa);
    }

    return fas;
}


