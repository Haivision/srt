/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


#ifndef INC_SRT_APPS_STATSWRITER_H
#define INC_SRT_APPS_STATSWRITER_H

#include <string>
#include <map>
#include <vector>
#include <memory>

#include "srt.h"
#include "utilities.h"

enum SrtStatsPrintFormat
{
    SRTSTATS_PROFMAT_INVALID = -1,
    SRTSTATS_PROFMAT_2COLS = 0,
    SRTSTATS_PROFMAT_JSON,
    SRTSTATS_PROFMAT_CSV
};

SrtStatsPrintFormat ParsePrintFormat(std::string pf, std::string& w_extras);

enum SrtStatCat
{
    SSC_GEN, //< General
    SSC_WINDOW, // flow/congestion window
    SSC_LINK, //< Link data
    SSC_SEND, //< Sending
    SSC_RECV //< Receiving
};

struct SrtStatData
{
    SrtStatCat category;
    std::string name;
    std::string longname;

    SrtStatData(SrtStatCat cat, std::string n, std::string l): category(cat), name(n), longname(l) {}
    virtual ~SrtStatData() {}

    virtual void PrintValue(std::ostream& str, const CBytePerfMon& mon) = 0;
};

template <class TYPE>
struct SrtStatDataType: public SrtStatData
{
    typedef TYPE CBytePerfMon::*pfield_t;
    pfield_t pfield;

    SrtStatDataType(SrtStatCat cat, const std::string& name, const std::string& longname, pfield_t field)
        : SrtStatData (cat, name, longname), pfield(field)
    {
    }

    void PrintValue(std::ostream& str, const CBytePerfMon& mon) override
    {
        str << mon.*pfield;
    }
};

class SrtStatsWriter
{
public:
    virtual std::string WriteStats(int sid, const CBytePerfMon& mon) = 0;
    virtual std::string WriteBandwidth(double mbpsBandwidth) = 0;
    virtual ~SrtStatsWriter() {}

    // Only if HAS_PUT_TIME. Specified in the imp file.
    std::string print_timestamp();

    void Option(const std::string& key, const std::string& val)
    {
        options[key] = val;
    }

    bool Option(const std::string& key, std::string* rval = nullptr)
    {
        const std::string* out = map_getp(options, key);
        if (!out)
            return false;

        if (rval)
            *rval = *out;
        return true;
    }

protected:
    std::map<std::string, std::string> options;
};

extern std::vector<std::unique_ptr<SrtStatData>> g_SrtStatsTable;

std::shared_ptr<SrtStatsWriter> SrtStatsWriterFactory(SrtStatsPrintFormat printformat);



#endif
