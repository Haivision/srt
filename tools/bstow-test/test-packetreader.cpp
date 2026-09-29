
#include <vector>
#include <string>
#include <chrono>
#include <csignal>
#include <fstream>

#include "ofmt.h"
#include "options.hpp"

#include "bstow-read.hpp"
#include "bstow-log.hpp"
#include "hvu_debug.h"

using namespace std;
using namespace hvu;

int64_t TimeNow()
{
    using namespace std::chrono;
    auto epoch_now = steady_clock::now().time_since_epoch();

    return duration_cast<microseconds>(epoch_now).count();
}

int g_interrupt_at = 0;

int main( int argc, char** argv )
{
    OptionHandler ops;

    // The only sensible setting for free options
    ops.default_arg(OptionScheme::ARG_VAR);

    ops.process(argv, argc);

    vector<string> args = ops[""];

    if (args.size() < 1)
    {
        ofprintl(cout, "Usage: ", argv[0], " <bstow URI> [options]");
        ofprintl(cout, "Options: -v (verbose), -t <time[us]|now>, -l <loglevel>, -lf <logfile>, -I <interrupt-at>");
        return 1;
    }

    bstow::PacketReader r (args[0]);

    int64_t base_ts = 0;

    string timesp = ops["t"];
    if (timesp != "")
    {
        if (isdigit(timesp[0]))
            base_ts = stoi(timesp);
        else if (timesp == "now")
            base_ts = TimeNow();
        else
        {
            ofprintl(cerr, "ERROR: -t <time|'now'> expected");
            return -1;
        }
    }

    int logl = ops.getfree(-1, "l");
    if (logl != -1)
        bstow::g_loglevel = logl;

    vector<string> logfile_spec = ops["lf"];
    std::ofstream out_logger;
    if (!logfile_spec.empty())
    {
        if (logfile_spec.size() > 1)
        {
            ofprintl(cerr, "OPTION: -lf requires one filename argument");
            return -1;
        }
        string logfile = logfile_spec[0];
        out_logger.open(logfile, ios::out | ios::trunc);
        if (!out_logger.good())
        {
            ofprintl(cerr, "ERROR OPENING FILE for logs: ", logfile);
            return -1;
        }
        bstow::g_logstream = &out_logger;
    }

    g_interrupt_at = ops.getfree(0, "I");

    if (!r.Prepare(base_ts))
    {
        ofprintl(cerr, "PREPARE FAILED: ", r.ErrorStr());
        return -1;
    }

    bool verbose = ops["v"];

    if (verbose)
        ofprintl(cout, "READING. FIRST TS=", base_ts);

    int npacket = 1;
    for (;;)
    {
        if (g_interrupt_at && npacket == g_interrupt_at)
            HVU_DEBUG_BREAK()
                ;

        MediaPacket p = r.Read();
        if (r.End())
            break;

        if (verbose)
            ofprintl(cout, fmt(ofcat("# ", npacket), fmtc().left().width(8)),
                    " [", fmt(p.payload.size(), fmtc().fillzero().width(4)), "] TS=", fmt(p.time, fmtc().width(8).fillzero()));
        ++npacket;
    }

    return 0;
}
