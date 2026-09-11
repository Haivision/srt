#include <string>
#include "ofmt.h"

namespace srt
{
namespace sync
{

// Old implementation for FormatTime
std::string FormatTime1(const steady_clock::time_point& timestamp)
{
    using namespace hvu;
    if (is_zero(timestamp))
    {
        // Use special string for 0
        return "00:00:00.000000 [TMTN]";
    }

    const int decimals = clockSubsecondPrecision();
    const uint64_t total_sec = count_seconds(timestamp.time_since_epoch());
    const uint64_t days = total_sec / (60 * 60 * 24);
    const uint64_t hours = total_sec / (60 * 60) - days * 24;
    const uint64_t minutes = total_sec / 60 - (days * 24 * 60) - hours * 60;
    const uint64_t seconds = total_sec - (days * 24 * 60 * 60) - hours * 60 * 60 - minutes * 60;
    steady_clock::time_point frac = timestamp - seconds_from(total_sec);
    ofmt_bufs out;
    if (days)
        out << days << OFMT_SV("D+");

    fmtc d02 = fmtc().dec().fillzero().width(2),
         dec0 = fmtc().dec().fillzero().width(decimals);

    out << fmt(hours, d02) << OFMT_SV(":")
        << fmt(minutes, d02) << OFMT_SV(":")
        << fmt(seconds, d02) << OFMT_SV(".")
        << fmt(frac.time_since_epoch().count(), dec0)
        << OFMT_SV(" [TMTN]");
    return out.str();
}

}
}
