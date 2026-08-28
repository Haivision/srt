// This application should generate the bstow data.

// The source file *.bsrc has the following format:

// First line:
//  :<PARAMETERS>
// Every next line:
//  <SIZE>[ i]
//
// where:
// <PARAMETERS> are space separated KEY=VALUE pairs. Available keys:
//
// U: unit size - multiplier for the frame sizes (if 1, <SIZE> is in bytes)
// P: packet size - maximum payload size for a single packet, must be aligned to U.
// F: framerate - divisor of 1 for the DTS distance between frames
// T: timestamp - the DTS of the very first frame
//
// For following frames, <SIZE> declares a single frame of given size
// multiplied by the unit size (U). Optionally followed by a frame type
// declarator; if this is "i", it's an I-Frame, or otherwise the required
// synchronization point.
//
// The generated BSTOW (*.bs) file will contain the correctly determined
// bitrate and will be split into single packets. The I-Frame is considered
// as the checkpoint when the sending time based on the bitrate is synchronized
// with the DTS.

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <tuple>
#include <iterator>
#include "ofmt.h"
#include "utilities.h" // from srt
#include "hvu_bigendian.h"

#include "bstow-read.hpp"

#define BSLOG_ENABLED 1
#include "bstow-log.hpp"

namespace bstow
{
    template<class IntType>
    inline void WriteValue(std::ostream& out, int label, IntType value)
    {
        int32_t cutval = (value & 0x00FFFFFF);

        if (cutval == value)
        {
            // Usual 24-bit encoding.
            unsigned char data[4];
            data[0] = label & 0x7F;
            data[1] = (value >> 16) & 0xFF;
            data[2] = (value >> 8) & 0xFF;
            data[3] = value & 0xFF;
            out.write((char*)data, 4);
            return;
        }

        // Encode a greater value: [LA][BEL][LEN][GTH][payload-of-that-length]
        unsigned char data[4+8];
        unsigned short labform = unsigned(label) | 0x8000;
        unsigned char* p = data;
        hvu::FormatBE<2>(labform, (p));
        p += 2;
        // We'd ignore zero-skipping for now, just write out a 64-bit value
        hvu::FormatBE<2>(sizeof(IntType), (p));
        p += 2;
        hvu::FormatBE<sizeof(IntType)>(value, (p));

        out.write((char*)data, sizeof data);
    }
}

using namespace std;
using namespace hvu;

struct General
{
    int64_t bitrate = 0;
    size_t unitsize = 1;
    size_t packetsize = 1444;
    int64_t timestamp_base = 1000;
    size_t framerate = 0;

    std::string show() const
    {
        return ofcat("CONFIG: br=", bitrate, " size.{u=", unitsize, ", p=", packetsize,
                "} ts.base=", timestamp_base, " fps=", framerate);
    }

    // XXX Here you can add some information how to
    // generate data
};

struct Payload
{
    size_t length = 0;
    int64_t timestamp = 0;
    int flags = 0;

    enum Flags { I = 1 };

    bool operator & (Flags f) const
    {
        return (flags & int(f)) == int(f);
    }

    bool Interpret(const std::string& line)
    {
        using namespace std;

        deque<string> parts;
        srt::Split(line, ' ', back_inserter(parts));
        for (;;)
        {
            if (parts.empty())
                break;

            if (parts[0] == "")
                parts.pop_front();
            else
                break;
        }
        if (parts.empty())
            return false;

        // Very first must be a number
        length = stoi(parts[0]);
        parts.pop_front();

        for (auto& s: parts)
        {
            if (s == "")
                continue;
            InterpretFlag(s);
        }

        return true;
    }

    void InterpretFlag(const std::string& f)
    {
        if (f == "i")
        {
            flags |= Flags::I;
            return;
        }

        // XXX Here you might also interpret a timestamp, for example.

        hvu::ofprintl(std::cerr, "WARNING: unknown flag: ", f);
    }
};

struct Config
{
    General general;
    std::vector<Payload> payloads;

    void FixMissing();
    int64_t EstimateBitrate();
    void DefineTimestamps();

    size_t payloadSize(size_t ix) const
    {
        return general.unitsize * payloads[ix].length;
    }
};

int64_t SendPeriod(const General& gen, size_t bytes)
{
    size_t nbits_micro = bytes * 8 * 1000000;

    // size [Mega-bits] / rate [Bits / s]
    // -> 1s * (size[Mb] / Bits
    // -> 1s * 1000000 * (size[B] / factor[B])

    return nbits_micro / gen.bitrate;
}

class Extractor
{
    // Constants
    vector<string> m_prefix;
    string m_line;

    // Variable state
    struct State
    {
        size_t linepos = 0;
        string value;
    } m_state;

public:
    static const size_t npos = string::npos;

    Extractor(const vector<string>& prefixes, const string& line): m_prefix(prefixes), m_line(line) {}

    tuple<size_t, string> next()
    {
        // At first, starting from linepos, skip all space characters.
        // Exit with nothing if reached the end.
        for (;;)
        {
            if (m_state.linepos >= m_line.size())
                return make_tuple(+npos, string());

            if (m_line[m_state.linepos] == ' ')
            {
                ++m_state.linepos;
                continue;
            }

            break;
        }

        string::iterator line_start = m_line.begin() + m_state.linepos;
        size_t line_size = m_line.size() - m_state.linepos;
        for (size_t i = 0; i < m_prefix.size(); ++i)
        {
            auto& p = m_prefix[i];
            if (line_size <= p.size())
                continue;

            if (std::mismatch(p.begin(), p.end(), line_start).first == p.end())
            {
                // We have a match
                size_t valpos = m_state.linepos + p.size();
                string value;
                size_t earg = m_line.find(' ', valpos);
                if (earg == string::npos)
                {
                    m_state.value = m_line.substr(valpos);
                    m_state.linepos = m_line.size();
                }
                else
                {
                    size_t nchars = earg - valpos;
                    m_state.value = m_line.substr(valpos, nchars);
                    m_state.linepos = earg + 1;
                }
                return make_tuple(i, m_state.value);
            }
        }

        return make_tuple(string::npos, string("Unknown prefix"));
    }
};

static const vector<string> g_prefixes = {
    "F=",
    "U=",
    "P=",
    "T="
};

enum Arg: size_t
{
    FRAMERATE = 0,
    UNITSIZE,
    PACKETSIZE,
    TIMESTAMP,

    UNKNOWN = std::string::npos
};

bool InterpretParameters(const string& line, Config& config)
{
    Extractor ex (g_prefixes, line);

    try
    {
        for (;;)
        {
            auto [index, value] = ex.next();

            if (index == Arg::UNKNOWN)
            {
                // empty string means end. Otherwise it's an error message
                if (value != "")
                {
                    ofprintl(cerr, "Error reading parameters: ", value);
                    return false;
                }
                break;
            }

            switch (index)
            {
            case Arg::FRAMERATE:
                config.general.framerate = stoi(value);
                break;

            case Arg::UNITSIZE:
                config.general.unitsize = stoi(value);
                break;

            case Arg::PACKETSIZE:
                config.general.packetsize = stoi(value);
                break;

            case Arg::TIMESTAMP:
                config.general.timestamp_base = stoi(value);
                break;
            }
        }
    }
    catch (std::exception& e)
    {
        ofprintl(cerr, "ERROR: ", e.what());
        return false;
    }

    // HERE you should verify if all args were checked.
    return true;
}

bool InterpretFile(const string& infile, Config& config)
{
    std::ifstream ifile (infile);

    bool have_parameters = false;

    string line;
    while (getline(ifile, (line)))
    {
        // Skip empties and commetns
        if (line == "" || line[0] == '#')
            continue;
        if (line[0] == ':')
        {
            if (have_parameters)
            {
                ofprintl(cerr, "ERROR: Parameters specified twice");
                return false;
            }
            size_t i = 0;
            while (line[i] == ':' || line[i] == ' ')
            {
                ++i;
                if (i == line.size())
                {
                    ofprintl(cerr, "No parameters in parameters line: '", line, "' - bailing out");
                    return false;
                }
            }
            // Interpret parameters
            // Skip that initial :
            if (!InterpretParameters(line.substr(i), (config)))
                return false;

            have_parameters = true;

            // Do this update every time the settings come in.
            continue;
        }
        if (!have_parameters)
        {
            ofprintl(cerr, "ERROR: Parameters not specified");
            return false;
        }

        size_t size = 0;
        int64_t ts = -1;

        Payload pay;
        if (pay.Interpret(line))
            config.payloads.push_back(pay);
    }

    return true;
}

void Config::DefineTimestamps()
{
    int64_t timestamp_track = 0;
    int64_t timestamp_zero_align_s = 0;
    size_t timestamp_track_frames = 0;

    int64_t resolution = 1000000; // ms

    // BSLOG(DEBUG, "WILL DEFINE TIMESTAMPS:");

    for (auto& p : payloads)
    {
        double tval = timestamp_track_frames * double(resolution) / general.framerate;
        int64_t ival = tval;
        double fracval = tval - ival;
        int postfix = 2*fracval;

        p.timestamp = general.timestamp_base + ival + postfix;

        // BSLOG(DEBUG, "FRAME #", timestamp_track_frames, " tval=", tval, " TS=", p.timestamp);

        ++timestamp_track_frames;
    }
}

bool InterpretArgs(const vector<string>& args, Config& config)
{
    if (args.size() == 1)
        return InterpretFile(args[0], (config));
    string last;
    size_t nexp = 0;
    int ipayload = -1;

    // PROGRAM PARAMETERS:
    //
    // SINGLE PARAMETERS:
    // BITRATE = x bps
    // TIMESTAMP_BASE [optional = 1000] = FIRST portion timestamp
    // TIMESTAMP_STRIDE [optional = 0] = shift to add to next payloads
    // UNITSIZE [optional = 1] = size of payload alignment
    //
    // MULTIPLE PARAMETERS
    // LENGTH = total length of the payload divided by UNITSIZE
    // TIMESTAMP [optional = 0]

    auto& gen = config.general;
    auto& pay = config.payloads;

    try
    {
        for (int i = 0; i < args.size(); ++i)
        {
            if (nexp)
            {
                // Handles the -p case
                if (ipayload > -1)
                {
                    if (last == "-s" || last == "-size" || last == "-length")
                    {
                        pay[ipayload].length = stoi(args[i]);
                    }
                    else if (last == "-ts" || last == "-timestamp")
                    {
                        pay[ipayload].timestamp = stoll(args[i]);
                    }
                    else
                    {
                        throw std::invalid_argument(ofcat("Invalid parameter (payload): ", last, " value:", args[i]));
                    }
                }
                else if (last == "-b" || last == "-bitrate")
                {
                    gen.bitrate = stoi(args[i]);
                }
                else if (last == "-tb" || last == "-timestamp-base")
                {
                    gen.timestamp_base = stoll(args[i]);
                }
                else if (last == "-fps" || last == "-framerate")
                {
                    gen.framerate = stoi(args[i]);
                }
                else if (last == "-u" || last == "-unitsize")
                {
                    gen.unitsize = stoi(args[i]);
                }
                else
                {
                    throw std::invalid_argument(ofcat("Invalid parameter (general): ", last, " value:", args[i]));
                }

                last = "";
                nexp = 0;
                continue;
            }

            string arg = args[i];
            if (arg[0] == '-')
            {
                if (arg == "-p" || arg == "-payload")
                {
                    // Special handling
                    ++ipayload;
                    pay.push_back(Payload());
                    continue;
                }

                last = arg;
                nexp = 1;
                continue;
            }

            ofprintl(cout, "Unordered argument: ", arg);
            break;
        }
    }
    catch (std::invalid_argument& e)
    {
        ofprintl(cerr, "ERROR: ", e.what(), " last arg=", last, " payload=", ipayload);
        return false;
    }

    for (int i = 0; i < pay.size(); ++i)
    {
        if (pay[i].length == 0)
        {
            ofprintl(cerr, "ZERO length at payload #", i);
            return false;
        }

        if (pay[i].timestamp == 0 && gen.framerate == 0)
        {
            ofprintl(cerr, "ZERO timestamp (with no framerate specified) at payload #", i);
            return false;
        }
    }

    return true;
}

void GenerateRandomData(ostream& out, size_t ndata)
{
    // Currently we just create a payload of EE hex value
    for (size_t i = 0; i < ndata; ++i)
    {
        out.put(0xEE);
    }
}

size_t avg_tracked_plsize = 0;

void WritePayload(ostream& out, const Config& cf, size_t ix, const vector<char>& payload_data, int64_t& w_timestride_us)
{
    using namespace bstow;
    auto& pl = cf.payloads[ix];
    auto& gen = cf.general;

    // This should be split here into smaller pieces.
    // Rely on the timestamp defined in the payload.
    // General is necessary only for unit size and bitrate.

    size_t takesize = (gen.packetsize / gen.unitsize) * gen.unitsize;

    size_t remain = payload_data.size();

    if (::avg_tracked_plsize == 0)
        ::avg_tracked_plsize = remain;
    else
    {
        ::avg_tracked_plsize = srt::avg_iir<5>(::avg_tracked_plsize, remain);
    }

    // Determine the checkpoint
    if (pl & Payload::Flags::I)
    {
        w_timestride_us = 0;
        ::avg_tracked_plsize = 0;
    }

    BSLOG(DEBUG, "size=", remain, " avg=", ::avg_tracked_plsize, " STS=", w_timestride_us);

    bool first = true;

    while (remain)
    {
        // Signature first
        out.write(g_header, 4);

        string timelog = "UNSPEC";
        if (first)
        {
            WriteValue(out, DEF_PLAYTIME, pl.timestamp);
            timelog = fmts(pl.timestamp);
        }

        if (w_timestride_us)
        {
            WriteValue(out, DEF_SENDTIME, w_timestride_us);
        }

        first = false;

        size_t pktsize = std::min(takesize, remain);
        WriteValue(out, DEF_LENGTH, pktsize);

        size_t notch = payload_data.size() - remain;

        WriteValue(out, DEF_DATA, 0);
        out.write(payload_data.data() + notch, pktsize);

        remain -= pktsize;

        BSLOG(DEBUG, "packet size=", pktsize, " PTS=", timelog, " STS=", w_timestride_us);

        w_timestride_us += SendPeriod(gen, pktsize);
    }
}

vector<char> GeneratePayloadData(const Config& config, size_t ix)
{
    // NOTE: length defines the number of units

    vector<char> out;

    for (size_t l = 0; l < config.payloads[ix].length; ++l)
    {
        // A single unit is filled by subsequent numbers up to the
        // end of unit. The next unit will start from 1. If the unit
        // is larger than 255, it will just overflow.
        uint8_t b = 1;
        for (size_t i = 0; i < config.general.unitsize; ++i)
        {
            out.push_back(b++);
        }
    }

    return out;
}

int64_t Config::EstimateBitrate()
{
    size_t i;
    int64_t timestamp_begin = -1;

    // Search for the first I-Frame
    for (i = 0; i < payloads.size(); ++i)
    {
        if (srt::IsSet(payloads[i].flags, Payload::Flags::I))
        {
            timestamp_begin = payloads[i].timestamp;
            break;
        }
        BSLOG(DEBUG, "# ", i, " still no I-Frame");
    }
    if (timestamp_begin == -1)
        throw std::runtime_error("No I-frame found");

    size_t collected_size = payloadSize(i);

    int64_t avg_rate = 0; // Prevent it from being 0

    // BSLOG(DEBUG, "START: size=", collected_size);

    size_t prev_size = collected_size;

    BSLOG(DEBUG, "# ", i, ": [FIRST I-Frame] p.size=", payloadSize(i));

    // Continue from next frame until the next I-Frame.
    for (++i; i < payloads.size(); ++i)
    {
        int64_t dur = payloads[i].timestamp - payloads[i-1].timestamp;
        int64_t tdur = payloads[i].timestamp - timestamp_begin;
        size_t prev_col_size = collected_size;
        collected_size += payloadSize(i);

        int64_t irate = payloadSize(i) * 8 * 1000000 / dur;
        int64_t trate = collected_size * 8 * 1000000 / tdur;

        BSLOG(DEBUG, "# ", i, ": flags=", payloads[i].flags, " p.size=", payloadSize(i),
                " GOP.size=", collected_size, " p.dur=", dur, " GOP.dur=", tdur, " p.rate=", irate, " GOP.rate=", trate);

        if (srt::IsSet(payloads[i].flags, Payload::Flags::I))
        {
            break;
        }
        avg_rate = collected_size * 8 * 1000000 / (tdur + dur);
        prev_size = payloadSize(i);
    }

    return avg_rate;
}

void Config::FixMissing()
{
    if (payloads.empty())
        throw std::runtime_error("No payloads");

    // This means we haven't specified timestamps for every payload,
    // so construct the timestamps using the base and framerate.
    if (payloads[0].timestamp == 0)
    {
        DefineTimestamps();
    }

    // NOTE: timestamps are required to estimate the bitrate!
    if (general.bitrate == 0)
    {
        general.bitrate = EstimateBitrate();
        ofprintl(cerr, "BITRATE CALCULATED: ", general.bitrate);
    }
}

int main( int argc, char** argv )
{
    using namespace std;
    using namespace bstow;

    // CHECK 1: Either TIMESAMP_STRIDE must be specified
    // or TIMESTAMP must be specified for every payload

    string filename;
    if (argc < 2 || (filename = argv[1]) == "--help")
    {
        ofprintl(cerr, "Usage: ", argv[0], " <filename> <generation parameters...>");
        ofprintl(cerr, "Options:");
        ofprintl(cerr, "  -b\tbitrate [bps]");
        ofprintl(cerr, "  -tb\tTimestamp base [us]");
        ofprintl(cerr, "  -td\tTimestamp stride [us]");
        ofprintl(cerr, "  -u\tUnit size (size alignment for reading payloads)");
        ofprintl(cerr, "  -p: Specify further payload data:");
        ofprintl(cerr, "    -s: Payload size");
        ofprintl(cerr, "    -ts: Payload timestamp (if not specified by -tb/-td)");
        return 1;
    }

    vector<string> args(argv + 2, argv + argc);
    Config config;

    if (args.empty())
    {
        ofprintl(cerr, "After <filename> at least <source-file> or <generation-options> expected");
        return 1;
    }

    if (!InterpretArgs(args, (config)))
    {
        ofprintl(cerr, "Usage error, bailing out");
        return 1;
    }

    config.FixMissing();

    try
    {
        ofstream out;
        out.exceptions(ios::failbit | ios::badbit);
        out.open(filename, ios::out | ios::binary);

        // XXX set no buffering to see the file immediately
        out.setf(ios::unitbuf);
        out.rdbuf()->pubsetbuf(0, 0);

        // sendtime tracker - will get reset at the checkpoint
        int64_t timestride_us = 0;

        for (int i = 0; i < config.payloads.size(); ++i)
        {
            // BSLOG(DEBUG, "WRITE PAYLOAD #", i, " TS=", config.payloads[i].timestamp);
            vector<char> data = GeneratePayloadData(config, i);
            WritePayload((out), config, i, data, (timestride_us));
        }
    }
    catch (std::exception& e)
    {
        ofprintl(cerr, "ERROR: ", e.what(), " -- interrupted by exception");
    }

    return 0;
}
