// BSTOW generator for MPEG-TS files.
//
// Analyzes a MPEG-TS file with exactly one program and one video stream and
// produces a BSTOW file that doesn't contain any payload data, but only
// references to the portions of the MPEG-TS file (FILENAME + OFFSET).
//
// - Block: all TS packets from the first packet of one video frame (PUSI on
//   the video PID) up to the packet preceding the next video frame, including
//   any audio and other data interleaved in this area.
// - Series: starts with the I-Frame (RAI flag, or IDR/IRAP picture detected
//   in the video elementary stream) and lasts up to the next I-Frame.
//   PAT/PMT packets located directly before the I-Frame are made a part of
//   the series-initial block.
// - PLAYTIME: DTS (or PTS if there's no DTS) of the video frame, in us,
//   starting from the timestamp base. Discontinuities (jump back or too long
//   gap) are handled by applying the previous frame distance.
// - SENDTIME: smooth distribution by the bitrate calculated for the series,
//   so that sending the series exactly by SENDTIME takes the same time
//   as the PLAYTIME distance between the series' I-Frames.
//
// Frames that precede the first I-Frame are skipped, as well as incomplete
// data at the end of file (not aligned to TS packet).

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "bstow-write.hpp"

using namespace std;
namespace fs = std::filesystem;

namespace
{

const size_t TS_SIZE = 188;
const int64_t TS_CLOCK = 90000;
const int64_t PTS_MOD = int64_t(1) << 33;
const int64_t NOTS = -1;

enum Codec { CODEC_NONE, CODEC_MPEG2, CODEC_H264, CODEC_HEVC, CODEC_OTHER };

Codec CodecOfStreamType(int st)
{
    switch (st)
    {
    case 0x01: case 0x02: return CODEC_MPEG2;
    case 0x1B: return CODEC_H264;
    case 0x24: return CODEC_HEVC;
    case 0x10: case 0x20: case 0x21: case 0x42: case 0xD1: case 0xEA:
        return CODEC_OTHER; // video, but I-frames can be only detected by RAI
    }
    return CODEC_NONE;
}

struct Frame
{
    uint64_t offset = 0;    // first TS packet of the frame
    uint64_t pre_offset = 0; // including the preceding PAT/PMT packets
    int64_t ts = NOTS;      // DTS or PTS, in 90kHz ticks
    bool iframe = false;

    uint64_t start() const { return iframe ? pre_offset : offset; }
};

struct Options
{
    string outfile;
    string tsfile;
    size_t packetsize = 1316;
    int64_t timestamp_base = 1000;
    int64_t bitrate = 0;
    int64_t maxgap_ticks = TS_CLOCK; // 1 s
    bool absolute_path = false;
};

// Detect I pictures in the elementary stream of a frame; the state is kept
// between the TS packets of one frame.
struct Mpeg2Scanner
{
    int zeros = 0;
    int state = 0; // 0: seek prefix; 1: start code value; 2: first picture header byte; 3: second
    bool idr = false;

    void reset() { zeros = 0; state = 0; idr = false; }

    void feed(const unsigned char* p, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
        {
            unsigned char b = p[i];
            switch (state)
            {
            case 0:
                if (zeros >= 2 && b == 1)
                {
                    state = 1;
                    zeros = 0;
                }
                else
                    zeros = b == 0 ? zeros + 1 : 0;
                break;
            case 1:
                state = (b == 0x00) ? 2 : 0; // 0x00 = picture_start_code
                zeros = b == 0 ? 1 : 0;
                break;
            case 2:
                state = 3;
                break;
            case 3:
                if (((b >> 3) & 7) == 1)
                    idr = true;
                state = 0;
                zeros = 0;
                break;
            }
        }
    }
};

struct NalScanner
{
    Codec codec = CODEC_H264;
    int zeros = 0;
    bool want = false;
    bool idr = false;

    void reset() { zeros = 0; want = false; idr = false; }

    void feed(const unsigned char* p, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
        {
            unsigned char b = p[i];
            if (want)
            {
                want = false;
                if (codec == CODEC_H264)
                {
                    if ((b & 0x1F) == 5)
                        idr = true;
                }
                else
                {
                    int t = (b >> 1) & 0x3F;
                    if (t >= 16 && t <= 21)
                        idr = true;
                }
                zeros = b == 0 ? 1 : 0;
                continue;
            }
            if (zeros >= 2 && b == 1)
            {
                want = true;
                zeros = 0;
            }
            else
                zeros = b == 0 ? zeros + 1 : 0;
        }
    }
};
}

namespace
{

// Returns the payload start of the TS packet, or TS_SIZE if no payload.
size_t PayloadStart(const unsigned char* pkt)
{
    int afc = (pkt[3] >> 4) & 3;
    if (!(afc & 1))
        return TS_SIZE;
    if (afc == 3)
    {
        size_t start = 5 + pkt[4];
        return start > TS_SIZE ? TS_SIZE : start;
    }
    return 4;
}

int64_t ReadTimestamp(const unsigned char* p)
{
    return (int64_t((p[0] >> 1) & 7) << 30)
        | (int64_t(p[1]) << 22)
        | (int64_t(p[2] >> 1) << 15)
        | (int64_t(p[3]) << 7)
        | int64_t(p[4] >> 1);
}

struct Analysis
{
    vector<Frame> frames;
    uint64_t usable_size = 0;
    int video_pid = -1;
    int pmt_pid = -1;
    Codec codec = CODEC_NONE;
};

[[noreturn]] void Fail(const string& msg)
{
    throw runtime_error(msg);
}

void ParsePAT(const unsigned char* sec, size_t len, Analysis& an)
{
    // sec: table_id and further; len: bytes available
    if (len < 8 || sec[0] != 0)
        return;
    size_t seclen = ((sec[1] & 0x0F) << 8) | sec[2];
    if (seclen + 3 > len)
        Fail("PAT section doesn't fit in one TS packet");

    size_t end = 3 + seclen - 4; // without CRC
    int nprograms = 0;
    for (size_t i = 8; i + 4 <= end; i += 4)
    {
        int prog = (sec[i] << 8) | sec[i+1];
        int pid = ((sec[i+2] & 0x1F) << 8) | sec[i+3];
        if (prog == 0)
            continue; // network PID
        ++nprograms;
        if (an.pmt_pid == -1)
            an.pmt_pid = pid;
    }
    if (nprograms > 1)
        Fail("More than one program in PAT - not supported");
}

void ParsePMT(const unsigned char* sec, size_t len, Analysis& an)
{
    if (len < 12 || sec[0] != 2)
        return;
    size_t seclen = ((sec[1] & 0x0F) << 8) | sec[2];
    if (seclen + 3 > len)
        Fail("PMT section doesn't fit in one TS packet");

    size_t end = 3 + seclen - 4;
    size_t pilen = ((sec[10] & 0x0F) << 8) | sec[11];
    size_t i = 12 + pilen;
    int nvideo = 0;
    while (i + 5 <= end)
    {
        int st = sec[i];
        int pid = ((sec[i+1] & 0x1F) << 8) | sec[i+2];
        size_t esl = ((sec[i+3] & 0x0F) << 8) | sec[i+4];
        Codec c = CodecOfStreamType(st);
        if (c != CODEC_NONE)
        {
            ++nvideo;
            if (an.video_pid == -1)
            {
                an.video_pid = pid;
                an.codec = c;
            }
        }
        i += 5 + esl;
    }
    if (nvideo > 1)
        Fail("More than one video stream in PMT - not supported");
}

Analysis AnalyzeTS(const string& path)
{
    ifstream in(path, ios::in | ios::binary);
    if (!in)
        Fail("Can't open " + path);

    Analysis an;

    NalScanner nal;
    Mpeg2Scanner mp2;
    bool in_frame = false;
    bool pat_done = false, pmt_done = false;

    uint64_t psi_run_start = UINT64_MAX; // start of PAT/PMT packets run
    uint64_t psi_run_end = 0;

    unsigned char pkt[TS_SIZE];
    uint64_t offset = 0;
    while (in.read((char*)pkt, TS_SIZE))
    {
        if (pkt[0] != 0x47)
            Fail("Lost TS sync at offset " + to_string(offset)
                    + " (only 188-byte TS packets are supported)");

        int pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
        bool pusi = pkt[1] & 0x40;
        size_t pay = PayloadStart(pkt);

        if (pid < 0x20 || (an.pmt_pid != -1 && pid == an.pmt_pid))
        {
            // PSI/SI packets (PAT, SDT etc.) preceding the I-Frame belong to its block
            if (psi_run_start == UINT64_MAX || psi_run_end != offset)
                psi_run_start = offset;
            psi_run_end = offset + TS_SIZE;

            if (pusi && pay < TS_SIZE)
            {
                size_t ptr = pkt[pay];
                size_t sstart = pay + 1 + ptr;
                if (sstart < TS_SIZE)
                {
                    if (pid == 0 && !pat_done)
                    {
                        ParsePAT(pkt + sstart, TS_SIZE - sstart, an);
                        pat_done = an.pmt_pid != -1;
                    }
                    else if (pid != 0 && !pmt_done)
                    {
                        ParsePMT(pkt + sstart, TS_SIZE - sstart, an);
                        pmt_done = an.video_pid != -1;
                        nal.codec = an.codec;
                    }
                }
            }
        }
        else if (an.video_pid != -1 && pid == an.video_pid)
        {
            if (pusi)
            {
                Frame f;
                f.offset = offset;
                f.pre_offset = (psi_run_start != UINT64_MAX && psi_run_end == offset) ? psi_run_start : offset;

                // RAI is in the adaptation field flags
                int afc = (pkt[3] >> 4) & 3;
                if (afc & 2 && pkt[4] > 0 && (pkt[5] & 0x40))
                    f.iframe = true;

                nal.reset();
                mp2.reset();

                if (pay + 9 <= TS_SIZE && pkt[pay] == 0 && pkt[pay+1] == 0 && pkt[pay+2] == 1)
                {
                    int flags = pkt[pay+7] >> 6;
                    size_t hlen = pkt[pay+8];
                    if ((flags & 2) && pay + 14 <= TS_SIZE)
                        f.ts = ReadTimestamp(pkt + pay + 9);
                    if ((flags & 1) && pay + 19 <= TS_SIZE)
                        f.ts = ReadTimestamp(pkt + pay + 14); // DTS preferred
                    pay += 9 + hlen;
                }
                else
                {
                    pay = TS_SIZE; // not a PES start; nothing to scan
                }
                an.frames.push_back(f);
                in_frame = true;
            }

            if (in_frame && pay < TS_SIZE)
            {
                if (an.codec == CODEC_MPEG2)
                {
                    mp2.feed(pkt + pay, TS_SIZE - pay);
                    if (mp2.idr)
                        an.frames.back().iframe = true;
                }
                else if (an.codec == CODEC_H264 || an.codec == CODEC_HEVC)
                {
                    nal.feed(pkt + pay, TS_SIZE - pay);
                    if (nal.idr)
                        an.frames.back().iframe = true;
                }
            }
        }

        offset += TS_SIZE;
    }

    an.usable_size = offset;

    if (an.pmt_pid == -1)
        Fail("PAT not found");
    if (an.video_pid == -1)
        Fail("No video stream found in PMT");
    if (an.frames.empty())
        Fail("No video frames found");

    return an;
}

struct Series
{
    size_t first = 0, last = 0; // frame indexes
};

void Generate(const Options& opt)
{
    Analysis an = AnalyzeTS(opt.tsfile);
    auto& fr = an.frames;

    // Skip all before the first I-Frame
    size_t begin = 0;
    while (begin < fr.size() && !fr[begin].iframe)
        ++begin;
    if (begin == fr.size())
        Fail("No I-Frame found in the video stream");

    cerr << "Video PID=" << an.video_pid << " frames=" << fr.size()
        << " skipped before first I-Frame=" << begin << "\n";

    // Timeline: play time per frame, in ticks relative to frame #begin
    size_t nfr = fr.size() - begin;
    vector<int64_t> play(nfr); // ticks
    int64_t last_delta = 0;
    int64_t cumul = 0;
    for (size_t i = 1; i < nfr; ++i)
    {
        int64_t a = fr[begin+i-1].ts, b = fr[begin+i].ts;
        int64_t delta = -1;
        if (a != NOTS && b != NOTS)
            delta = ((b - a) % PTS_MOD + PTS_MOD) % PTS_MOD;

        if (delta <= 0 || delta > opt.maxgap_ticks)
        {
            if (last_delta == 0)
            {
                // Look ahead for the first good distance
                int64_t found = 0;
                for (size_t j = i + 1; j < nfr && !found; ++j)
                {
                    int64_t x = fr[begin+j-1].ts, y = fr[begin+j].ts;
                    if (x == NOTS || y == NOTS)
                        continue;
                    int64_t d = ((y - x) % PTS_MOD + PTS_MOD) % PTS_MOD;
                    if (d > 0 && d <= opt.maxgap_ticks)
                        found = d;
                }
                last_delta = found ? found : TS_CLOCK / 25;
            }
            cerr << "WARNING: time discontinuity at frame #" << (begin+i) << " (offset "
                << fr[begin+i].offset << "), using the previous distance " << last_delta << " ticks\n";
            delta = last_delta;
        }
        last_delta = delta;
        cumul += delta;
        play[i] = cumul;
    }
    auto play_us = [&](size_t i) { return opt.timestamp_base + play[i] * 1000000 / TS_CLOCK; };
    int64_t final_delta = last_delta ? last_delta : TS_CLOCK / 25;

    // Block boundaries
    auto blockStart = [&](size_t i) { return fr[begin+i].start(); };
    auto blockEnd = [&](size_t i) { return i + 1 < nfr ? blockStart(i+1) : an.usable_size; };

    // The file name to store
    string stored = opt.tsfile;
    if (!opt.absolute_path)
    {
        error_code ec;
        fs::path outdir = fs::weakly_canonical(fs::path(opt.outfile).parent_path().empty()
                ? fs::path(".") : fs::path(opt.outfile).parent_path(), ec);
        fs::path rel = ec ? fs::path() : fs::relative(fs::weakly_canonical(opt.tsfile, ec), outdir, ec);
        if (!ec && !rel.empty())
            stored = rel.generic_string();
        else
        {
            stored = fs::absolute(opt.tsfile).generic_string();
        }
    }
    else
    {
        stored = fs::absolute(opt.tsfile).generic_string();
    }

    ofstream out(opt.outfile, ios::out | ios::binary);
    if (!out)
        Fail("Can't open output " + opt.outfile);

    using namespace bstow;

    bool first_unit = true;
    size_t nseries = 0, npackets = 0;

    size_t i = 0;
    while (i < nfr)
    {
        // Series: from i to the next I-Frame
        size_t e = i + 1;
        while (e < nfr && !fr[begin+e].iframe)
            ++e;

        int64_t bitrate = opt.bitrate;
        if (!bitrate)
        {
            int64_t dur_ticks = (e < nfr ? play[e] : play[e-1] + final_delta) - play[i];
            int64_t dur_us = dur_ticks * 1000000 / TS_CLOCK;
            uint64_t bytes = blockEnd(e-1) - blockStart(i);
            if (dur_us <= 0)
                Fail("Zero duration of the series at frame #" + to_string(begin+i));
            bitrate = int64_t(bytes) * 8 * 1000000 / dur_us;
            if (bitrate <= 0)
                bitrate = 1;
        }
        ++nseries;

        int64_t stride_us = 0;
        for (size_t b = i; b < e; ++b)
        {
            uint64_t pos = blockStart(b);
            uint64_t endpos = blockEnd(b);
            bool first_packet = true;
            while (pos < endpos)
            {
                size_t len = size_t(min<uint64_t>(opt.packetsize, endpos - pos));

                out.write(g_header, 4);
                if (first_unit)
                {
                    WriteString(out, DEF_FILENAME, stored);
                    first_unit = false;
                }
                if (first_packet)
                    WriteValue<int64_t>(out, DEF_PLAYTIME, play_us(b));
                if (stride_us)
                    WriteValue<int64_t>(out, DEF_SENDTIME, stride_us);
                WriteValue<int64_t>(out, DEF_LENGTH, len);
                WriteValue<int64_t>(out, DEF_OFFSET, int64_t(pos));

                stride_us += int64_t(len) * 8 * 1000000 / bitrate;
                pos += len;
                first_packet = false;
                ++npackets;
            }
        }
        cerr << "Series #" << nseries << ": frames " << (begin+i) << ".." << (begin+e-1)
            << " bitrate=" << bitrate << " bps\n";
        i = e;
    }

    cerr << "Written " << nseries << " series, " << npackets << " packets; stored file name: "
        << stored << "\n";
}

void Usage(const char* prog)
{
    cerr << "Usage: " << prog << " <output.bs> <input.ts> [options]\n"
        "Options:\n"
        "  -p <bytes>   Packet size (default 1316, must be a multiple of 188)\n"
        "  -tb <us>     Timestamp base for the first frame (default 1000)\n"
        "  -b <bps>     Force bitrate instead of calculating it for every series\n"
        "  -gap <ms>    Max distance between frames, longer is a discontinuity (default 1000)\n"
        "  -abs         Store absolute path of the TS file instead of relative to the output\n";
}

}

int main(int argc, char** argv)
{
    Options opt;
    vector<string> args(argv + 1, argv + argc);
    if (args.size() < 2 || args[0] == "--help" || args[0] == "-h")
    {
        Usage(argv[0]);
        return 1;
    }

    opt.outfile = args[0];
    opt.tsfile = args[1];

    try
    {
        for (size_t i = 2; i < args.size(); ++i)
        {
            const string& a = args[i];
            auto next = [&]() -> const string& {
                if (i + 1 >= args.size())
                    throw invalid_argument("Missing value for " + a);
                return args[++i];
            };
            if (a == "-p")
                opt.packetsize = stoul(next());
            else if (a == "-tb")
                opt.timestamp_base = stoll(next());
            else if (a == "-b")
                opt.bitrate = stoll(next());
            else if (a == "-gap")
                opt.maxgap_ticks = stoll(next()) * TS_CLOCK / 1000;
            else if (a == "-abs")
                opt.absolute_path = true;
            else
                throw invalid_argument("Unknown option: " + a);
        }
        if (opt.packetsize == 0 || opt.packetsize % TS_SIZE != 0)
            throw invalid_argument("Packet size must be a multiple of 188");
        if (opt.timestamp_base < 1)
            throw invalid_argument("Timestamp base must be positive");

        Generate(opt);
    }
    catch (exception& e)
    {
        cerr << "ERROR: " << e.what() << endl;
        return 1;
    }
    return 0;
}
