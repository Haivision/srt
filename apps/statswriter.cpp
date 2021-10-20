/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#include <string>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <memory>

#include "statswriter.hpp"
#include "netinet_any.h"
#include "srt_compat.h"

// Note: std::put_time is supported only in GCC 5 and higher
#if !defined(__GNUC__) || defined(__clang__) || (__GNUC__ >= 5)
#define HAS_PUT_TIME
#endif

using namespace std;


template <class TYPE>
inline SrtStatData* make_stat(SrtStatCat cat, const string& name, const string& longname,
        TYPE CBytePerfMon::*field)
{
    return new SrtStatDataType<TYPE>(cat, name, longname, field);
}

#define STATX(catsuf, sname, lname, field) s.emplace_back(make_stat(SSC_##catsuf, #sname, #lname, &CBytePerfMon:: field))
#define STAT(catsuf, sname, field) STATX(catsuf, sname, field, field)

vector<unique_ptr<SrtStatData>> g_SrtStatsTable;

struct SrtStatsTableInit
{
    SrtStatsTableInit(vector<unique_ptr<SrtStatData>>& s)
    {
        STATX(GEN, time, Time, msTimeStamp);

        STAT(WINDOW, flow, pktFlowWindow);
        STAT(WINDOW, congestion, pktCongestionWindow);
        STAT(WINDOW, flight, pktFlightSize);

        STAT(LINK, rtt, msRTT);
        STAT(LINK, bandwidth, mbpsBandwidth);
        STAT(LINK, maxBandwidth, mbpsMaxBW);

        STAT(SEND, packets, pktSent);
        STAT(SEND, packetsUnique, pktSentUnique);
        STAT(SEND, packetsLost, pktSndLoss);
        STAT(SEND, packetsDropped, pktSndDrop);
        STAT(SEND, packetsRetransmitted, pktRetrans);
        STAT(SEND, packetsFilterExtra, pktSndFilterExtra);
        STAT(SEND, bytes, byteSent);
        STAT(SEND, bytesUnique, byteSentUnique);
        STAT(SEND, bytesDropped, byteSndDrop);
        STAT(SEND, byteAvailBuf, byteAvailSndBuf);
        STAT(SEND, msBuf, msSndBuf);
        STAT(SEND, mbitRate, mbpsSendRate);
        STAT(SEND, sendPeriod, usPktSndPeriod);

        STAT(RECV, packets, pktRecv);
        STAT(RECV, packetsUnique, pktRecvUnique);
        STAT(RECV, packetsLost, pktRcvLoss);
        STAT(RECV, packetsDropped, pktRcvDrop);
        STAT(RECV, packetsRetransmitted, pktRcvRetrans);
        STAT(RECV, packetsBelated, pktRcvBelated);
        STAT(RECV, packetsFilterExtra, pktRcvFilterExtra);
        STAT(RECV, packetsFilterSupply, pktRcvFilterSupply);
        STAT(RECV, packetsFilterLoss, pktRcvFilterLoss);
        STAT(RECV, bytes, byteRecv);
        STAT(RECV, bytesUnique, byteRecvUnique);
        STAT(RECV, bytesLost, byteRcvLoss);
        STAT(RECV, bytesDropped, byteRcvDrop);
        STAT(RECV, byteAvailBuf, byteAvailRcvBuf);
        STAT(RECV, msBuf, msRcvBuf);
        STAT(RECV, mbitRate, mbpsRecvRate);
        STAT(RECV, msTsbPdDelay, msRcvTsbPdDelay);
    }
} g_SrtStatsTableInit (g_SrtStatsTable);


#undef STAT
#undef STATX

string srt_json_cat_names [] = {
    "",
    "window",
    "link",
    "send",
    "recv"
};

#ifdef HAS_PUT_TIME
// Follows ISO 8601
std::string SrtStatsWriter::print_timestamp()
{
    using namespace std;
    using namespace std::chrono;

    const auto   systime_now = system_clock::now();
    const time_t time_now    = system_clock::to_time_t(systime_now);

    std::ostringstream output;

    // SysLocalTime returns zeroed tm_now on failure, which is ok for put_time.
    const tm tm_now = SysLocalTime(time_now);
    output << std::put_time(&tm_now, "%FT%T.") << std::setfill('0') << std::setw(6);
    const auto    since_epoch = systime_now.time_since_epoch();
    const seconds s           = duration_cast<seconds>(since_epoch);
    output << duration_cast<microseconds>(since_epoch - s).count();
    output << std::put_time(&tm_now, "%z");
    return output.str();
}
#else

// This is a stub. The error when not defining it would be too
// misleading, so this stub will work if someone mistakenly adds
// the item to the output format without checking that HAS_PUT_TIME.
string SrtStatsWriter::print_timestamp()
{ return "<NOT IMPLEMENTED>"; }
#endif // HAS_PUT_TIME


class SrtStatsJson : public SrtStatsWriter
{
    static string quotekey(const string& name)
    {
        if (name == "")
            return "";

        return R"(")" + name + R"(":)";
    }

    static string quote(const string& name)
    {
        if (name == "")
            return "";

        return R"(")" + name + R"(")";
    }

public: 
    string WriteStats(int sid, const CBytePerfMon& mon) override
    {
        std::ostringstream output;

        string pretty_cr, pretty_tab;
        if (Option("pretty"))
        {
            pretty_cr = "\n";
            pretty_tab = "\t";
        }

        SrtStatCat cat = SSC_GEN;

        // Do general manually
        output << quotekey(srt_json_cat_names[cat]) << "{" << pretty_cr;

        // SID is displayed manually
        output << pretty_tab << quotekey("sid") << sid;

        // Extra Timepoint is also displayed manually
#ifdef HAS_PUT_TIME
        // NOTE: still assumed SSC_GEN category
        output << "," << pretty_cr << pretty_tab
            << quotekey("timepoint") << quote(print_timestamp());
#endif

        // Now continue with fields as specified in the table
        for (auto& i: g_SrtStatsTable)
        {
            if (i->category == cat)
            {
                output << ","; // next item in same cat
                output << pretty_cr;
                output << pretty_tab;
                if (cat != SSC_GEN)
                    output << pretty_tab;
            }
            else
            {
                if (cat != SSC_GEN)
                {
                    // DO NOT close if general category, just
                    // enter the depth.
                    output << pretty_cr << pretty_tab << "}";
                }
                cat = i->category;
                output << ",";
                output << pretty_cr;
                if (cat != SSC_GEN)
                    output << pretty_tab;

                output << quotekey(srt_json_cat_names[cat]) << "{" << pretty_cr << pretty_tab;
                if (cat != SSC_GEN)
                    output << pretty_tab;
            }

            // Print the current field
            output << quotekey(i->name);
            i->PrintValue(output, mon);
        }

        // Close the previous subcategory
        if (cat != SSC_GEN)
        {
            output << pretty_cr << pretty_tab << "}" << pretty_cr;
        }

        // Close the general category entity
        output << "}" << pretty_cr << endl;

        return output.str();
    }

    string WriteBandwidth(double mbpsBandwidth) override
    {
        std::ostringstream output;
        output << "{\"bandwidth\":" << mbpsBandwidth << '}' << endl;
        return output.str();
    }
};

class SrtStatsCsv : public SrtStatsWriter
{
private:
    bool first_line_printed;

public: 
    SrtStatsCsv() : first_line_printed(false) {}

    string WriteStats(int sid, const CBytePerfMon& mon) override
    {
        std::ostringstream output;

        // Header
        if (!first_line_printed)
        {
#ifdef HAS_PUT_TIME
            output << "Timepoint,";
#endif
            output << "Time,SocketID";

            for (auto& i: g_SrtStatsTable)
            {
                output << "," << i->longname;
            }
            output << endl;
            first_line_printed = true;
        }

        // Values
#ifdef HAS_PUT_TIME
        // HDR: Timepoint
        output << print_timestamp() << ",";
#endif // HAS_PUT_TIME

        // HDR: Time,SocketID
        output << mon.msTimeStamp << "," << sid;

        // HDR: the loop of all values in g_SrtStatsTable
        for (auto& i: g_SrtStatsTable)
        {
            output << ",";
            i->PrintValue(output, mon);
        }

        output << endl;
        return output.str();
    }

    string WriteBandwidth(double mbpsBandwidth) override
    {
        std::ostringstream output;
        output << "+++/+++SRT BANDWIDTH: " << mbpsBandwidth << endl;
        return output.str();
    }
};

class SrtStatsCols : public SrtStatsWriter
{
public: 
    string WriteStats(int sid, const CBytePerfMon& mon) override 
    { 
        std::ostringstream output;
        output << "======= SRT STATS: sid=" << sid << endl;
        output << "PACKETS     SENT: " << setw(11) << mon.pktSent            << "  RECEIVED:   " << setw(11) << mon.pktRecv              << endl;
        output << "LOST PKT    SENT: " << setw(11) << mon.pktSndLoss         << "  RECEIVED:   " << setw(11) << mon.pktRcvLoss           << endl;
        output << "REXMIT      SENT: " << setw(11) << mon.pktRetrans         << "  RECEIVED:   " << setw(11) << mon.pktRcvRetrans        << endl;
        output << "DROP PKT    SENT: " << setw(11) << mon.pktSndDrop         << "  RECEIVED:   " << setw(11) << mon.pktRcvDrop           << endl;
        output << "FILTER EXTRA  TX: " << setw(11) << mon.pktSndFilterExtra  << "        RX:   " << setw(11) << mon.pktRcvFilterExtra    << endl;
        output << "FILTER RX  SUPPL: " << setw(11) << mon.pktRcvFilterSupply << "  RX  LOSS:   " << setw(11) << mon.pktRcvFilterLoss     << endl;
        output << "RATE     SENDING: " << setw(11) << mon.mbpsSendRate       << "  RECEIVING:  " << setw(11) << mon.mbpsRecvRate         << endl;
        output << "BELATED RECEIVED: " << setw(11) << mon.pktRcvBelated      << "  AVG TIME:   " << setw(11) << mon.pktRcvAvgBelatedTime << endl;
        output << "REORDER DISTANCE: " << setw(11) << mon.pktReorderDistance << endl;
        output << "WINDOW      FLOW: " << setw(11) << mon.pktFlowWindow      << "  CONGESTION: " << setw(11) << mon.pktCongestionWindow  << "  FLIGHT: " << setw(11) << mon.pktFlightSize << endl;
        output << "LINK         RTT: " << setw(9)  << mon.msRTT            << "ms  BANDWIDTH:  " << setw(7)  << mon.mbpsBandwidth    << "Mb/s " << endl;
        output << "BUFFERLEFT:  SND: " << setw(11) << mon.byteAvailSndBuf    << "  RCV:        " << setw(11) << mon.byteAvailRcvBuf      << endl;
        return output.str();
    } 

    string WriteBandwidth(double mbpsBandwidth) override 
    {
        std::ostringstream output;
        output << "+++/+++SRT BANDWIDTH: " << mbpsBandwidth << endl;
        return output.str();
    }
};

shared_ptr<SrtStatsWriter> SrtStatsWriterFactory(SrtStatsPrintFormat printformat)
{
    switch (printformat)
    {
    case SRTSTATS_PROFMAT_JSON:
        return make_shared<SrtStatsJson>();
    case SRTSTATS_PROFMAT_CSV:
        return make_shared<SrtStatsCsv>();
    case SRTSTATS_PROFMAT_2COLS:
        return make_shared<SrtStatsCols>();
    default:
        break;
    }
    return nullptr;
}

SrtStatsPrintFormat ParsePrintFormat(string pf, string& w_extras)
{
    size_t havecomma = pf.find(',');
    if (havecomma != string::npos)
    {
        w_extras = pf.substr(havecomma+1);
        pf = pf.substr(0, havecomma);
    }

    if (pf == "default")
        return SRTSTATS_PROFMAT_2COLS;

    if (pf == "json")
        return SRTSTATS_PROFMAT_JSON;

    if (pf == "csv")
        return SRTSTATS_PROFMAT_CSV;

    return SRTSTATS_PROFMAT_INVALID;
}
