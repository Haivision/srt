///
//
///

#include "bstow-read.hpp"

#include <string>
#include <fstream>
#include <set>
#include <exception>
#include <chrono>
#include <thread> // for sleep

#include "hvu_compat.h"
#include "hvu_bigendian.h"

#define BSLOG_ENABLED 1
#include "bstow-log.hpp"

//#define BSTOW_ENABLE_VERBOSE 1

using namespace std;
using namespace hvu;

typedef std::chrono::steady_clock ClockType;
typedef ClockType::time_point ClockTime;
typedef ClockType::duration Duration;

inline int64_t count_microseconds(const Duration& dur)
{
    return chrono::duration_cast<chrono::microseconds>(dur).count();
}

struct TimeConverter
{
    ClockTime base_local;
    std::chrono::system_clock::time_point base_system;

    TimeConverter()
    {
        base_local = ClockType::now();
        base_system = std::chrono::system_clock::now();
    }

    std::chrono::system_clock::time_point ToSystem(const ClockTime& steady)
    {
        Duration sep = steady - base_local;
        return base_system + sep;
    }

    template<class Clock>
    static int64_t SinceEpoch(const Clock& val)
    {
        return val.time_since_epoch().count();
    }

} g_time_converter;

inline std::tm SystemTM(ClockTime time)
{
    using namespace std;
    using namespace std::chrono;
    using namespace hvu;

    auto systime = g_time_converter.ToSystem(time);

    const time_t time_epoch = std::chrono::system_clock::to_time_t(systime);

    ofmt_bufs output;

    // SysLocalTime returns zeroed tm_now on failure, which is ok for put_time.
    const tm tm_now = sys_localtime(time_epoch);
    return tm_now;
}

enum eDurationUnit {DUNIT_S, DUNIT_MS, DUNIT_US};

template <eDurationUnit u>
struct DurationUnitName;

template<>
struct DurationUnitName<DUNIT_US>
{
    static const char* name() { return "us"; }
    static double count(const Duration& dur) { return static_cast<double>(count_microseconds(dur)); }
};

template<>
struct DurationUnitName<DUNIT_MS>
{
    static const char* name() { return "ms"; }
    static double count(const Duration& dur) { return static_cast<double>(count_microseconds(dur))/1000.0; }
};

template<>
struct DurationUnitName<DUNIT_S>
{
    static const char* name() { return "s"; }
    static double count(const Duration& dur) { return static_cast<double>(count_microseconds(dur))/1000000.0; }
};

template<eDurationUnit UNIT>
inline std::string FormatDuration(const Duration& dur, bool plus = false)
{
    using namespace hvu;
    double val = DurationUnitName<UNIT>::count(dur);
    return ofcat(plus && val >= 0 ? OFMT_SV("+") : OFMT_SV(""), fmtm(val, std::fixed), DurationUnitName<UNIT>::name());
}

inline std::string FormatDuration(const Duration& dur)
{
    return FormatDuration<DUNIT_US>(dur);
}
std::string FormatDurationAuto(const Duration& dur, bool plus = false)
{
    int64_t value = count_microseconds(dur);

    if (value < 1000)
        return FormatDuration<DUNIT_US>(dur, plus);

    if (value < 1000000)
        return FormatDuration<DUNIT_MS>(dur, plus);

    return FormatDuration<DUNIT_S>(dur, plus);
}



namespace hvu
{
namespace internal
{
struct snd_steadyclock
{
    bool daytime = false;
    bool timezone = false;

    snd_steadyclock(const char* spec)
    {
        if (!spec)
            return;
        while (char c = *spec)
        {
            if (c == 'z')
                timezone = true;
            else if (c == 'd')
                daytime = true;
        }
    }

    template <class Value, class OutStream>
    void format_send(const Value& val, OutStream& os) const
    {
        using namespace std::chrono;

        struct std::tm systime = SystemTM(val);

        if (daytime)
            os << fmt(systime, "%FT%T.");
        else
            os << fmt(systime, "%T.");

        // Fraction of a second part
        const auto us_now = duration_cast<microseconds>(val.time_since_epoch());
        const auto us_rem = us_now - duration_cast<seconds>(us_now);
        os << fmt(us_rem.count(), hvu::fmtc().fillzero().width(6));

        // Timezone
        if (timezone)
            os << fmt(systime, "%z");
    }
};
}

inline auto fmt(const std::chrono::steady_clock::time_point& tim, const char* spec = nullptr)
// NOTE: `auto` as return type would work, but only in C++17
// The `decltype` could be tried, but the declaration wouldn't be less complicated
            -> internal::fmt_proxy_template<
                std::chrono::steady_clock::time_point,
                internal::snd_steadyclock>
{
    return fmt_make_proxy(tim, internal::snd_steadyclock(spec));
}

}



namespace bstow
{

struct PacketReader::PIMP
{
    std::istream& m_Datastream;

    // BSTOW stream structure:
    //
    // SERIES {
    //     BLOCK {
    //         PACKET <---------- INITIAL PACKET; TS > 0; TD = 0 {
    //              Very first packet, so
    //               - m_Checkpoint.series_duration_us = 0
    //               - m_Checkpoint.playtime_us = TS
    //               - m_Checkpoint.basetime_us = 0 // will be always used as relative time
    //               - m_Checkpoint.tdSendTime = NOW
    //
    //               - m_Last.tdPacketDuration_us = 0 (unknown)
    //               - m_Last.tdPacketTimeStride_us = 0 (initial)
    //               - m_Last.tdSendTime = NOW
    //         }
    //         PACKET ; TD>0 {
    //               At lest one packet is there, so
    //               - m_Last.tdPacketDuration_us = TD - m_Last.tdPacketTimeStride_us
    //               - m_Last.tdPacketTimeStride_us = m_Checkpoint.basetime_us + TD
    //               - m_Last.tdSendTime = NOW
    //         }
    //         PACKET
    //         PACKET
    //         PACKET
    //         PACKET
    //         PACKET
    //     }
    //     BLOCK {
    //         PACKET ; TS > 0, TD > 0
    //         PACKET
    //         PACKET
    //     }
    //     BLOCK {
    //         PACKET
    //         PACKET
    //         PACKET
    //     }
    // }
    // SERIES {
    //     BLOCK {
    //         PACKET <---------- CHECKPOINT PACKET; TS > 0; TD = 0 {
    //              Next series first:
    //               - m_Checkpoint.series_duration_us = TS - m_Checkpoint.playtime_us
    //               - m_Checkpoint.playtime_us = TS
    //               - NEW_BASETIME = m_Checkpoint.(basetime_us + series_duration_us)
    //               - m_Checkpoint.basetime_us = NEW_BASETIME
    //               - m_Checkpoint.tdSendTime = m_Checkpoint.tdSendTime + m_Checkpoint.series_duration_us
    //
    //               - m_Last.tdPacketDuration_us = UNCHANGED (remains with previous value)
    //               - m_Last.tdPacketTimeStride_us = m_Checkpoint.basetime_us
    //               - m_Last.tdSendTime = NOW
    //         }
    //         PACKET ; TD>0
    //         PACKET
    //         PACKET
    //         PACKET
    //         PACKET
    //         PACKET
    //     }
    //     BLOCK {
    //         PACKET ; TS > 0, TD > 0
    //         PACKET
    //         PACKET
    //     }
    //     BLOCK {
    //         PACKET {
    //            PREV PACKET SENDTIME = NOW SEND TIME  
    //         }
    //         PACKET {  --- DATA AT THIS POSITION
    //            m_Checkpoint.playtime_us = TS from the CKECHPOINT PACKET
    //            m_Checkpoint.sendtime = [now] when CHECKPOINT PACKET was sent
    //            m_tsLastSendTime = NOW SEND TIME
    //            m_tsBasetime_us = [user time] representing ZERO SEND TIME
    //            m_tdLastPacketTimeStride_us = NOW SEND TIME - ZERO SEND TIME
    //            m_tdLastPacketDuration = NOW SEND TIME - PREV PACKET SENDTIME
    //         }
    //         PACKET
    //     }
    // }
    //
    // SENDING INITIAL PACKET:
    //
    // - send it now, immediately
    // - packet timestamp = m_tsBasetime_us (in ms)
    //
    // SENDING CHECKPOINT PACKET:
    //
    //   FIRST: HANDLE CHECKPOINT :
    //       - NOW TIME - m_tsZeroSendtime -> SEND DURATION
    //       - incooming timestamp - m_tsZeroPlaytime_us -> PLAY DURATION
    //       - PLAY DURATION - SEND DURATION -> TIME SKEW
    //
    // - m_tsLastSendTime + m_tdLastPacketDuration + FIX
    //     where FIX:
    //        - < 0: borrow from the future sending time
    //        - > 0: add the value to the sending time
    //

    struct Header
    {
        // Updated with Refill(). If it's 0 after Refill, it's EOF.
        size_t packet_size = 0;

        // Last received media timestamp - remains unchanged as the
        // further units come in, until the next unit with timestamp
        int64_t media_timestamp_us = 0;

        // Last read packet timestamp, in the source version
        int64_t packet_timestride_us = 0;

        void Reset()
        {
            packet_size = 0;
            media_timestamp_us = 0;
            packet_timestride_us = 0;
        }
    } m_Header;

    struct Zero
    {
        // Initial value read from the timestamp in the begingging
        // or at the last checkpoint.
        int64_t playtime_us = 0;

        // The base time value since which the values of the timestamp
        // will be set to the outgoing packets. This value is initially 0
        // and updated with every checkpoint; the app should treat it as
        // relative time towards the beginning.
        int64_t basetime_us = 0;

        int64_t series_duration_us = 0;

        // Time recorded when sending the initial packet from the series.
        // NOTE: realtime doesn't follow any data, it's only used for
        // applying a wait time.
        ClockTime sendtime;

        int64_t diff_playtime(int64_t new_playtime)
        {
            return playtime_us == 0 ? 0 : new_playtime - playtime_us;
        }

        bool update(int64_t new_playtime)
        {
            bool following;
            if (playtime_us)
            {
                // FIRST SERIES packet already in-transmission.
                int64_t diff = diff_playtime(new_playtime);
                series_duration_us = diff;
                int64_t newbase = basetime_us + diff;
                auto newsend = sendtime + chrono::microseconds(diff);
                following = true;
                BSLOG(DEBUG, "NEXT packet; send time: ", fmt(sendtime), " STEP: ", diff,
                        " BASE: ", basetime_us, " -> ", newbase,
                        " SEND: ", fmt(sendtime), " -> ", fmt(newsend));
                basetime_us = newbase;
                sendtime = newsend;
            }
            else
            {
                // VERY FIRST packet
                sendtime = ClockType::now();
                following = false;
                BSLOG(DEBUG, "FIRST packet; send time: ", fmt(sendtime));
            }
            playtime_us = new_playtime;
            return following;
        }

        // This function is to be run with every block that provides the
        // block timestamp; this should confront it with the series-initial
        // timestamp (playtime_us) and the sendtime latched at the same time.
        ClockTime expectedReceiveTime(int64_t new_playtime)
        {
            return sendtime + chrono::microseconds(diff_playtime(new_playtime));
        }

        // NOTE: ZERO PACKET TIME IS ALWAYS ZERO.
    } m_Checkpoint;

    struct Last
    {
        ClockTime tsSendtime;

        // TimeStride from the packet that was sent as the last one. The packet
        // with that time stride was sent at the time recorded in tsSendtime.
        int64_t tdPacketTimeStride_us = 0;

        // Updated with every packet with TD > 0; distance between
        // incoming TD and previous TD (can be remembered in a local variable)
        int64_t tdPacketDuration_us = 0;

        // This represents the value of m_Header.packet_timestride_us in
        // case when this value as last read from BSTOW was 0.
        int next_stride() const
        {
            return tdPacketTimeStride_us + tdPacketDuration_us;
        }

        // Called only during the checkpoint, after checkpoint update
        void checkpoint(int64_t new_basetime_us, bool renew = true)
        {
            if (renew)
            {
                // Set this to 0 because after checkpoint you'll be getting
                // packet stride value relative to the last checkpoint
                new_basetime_us = 0;
            }
            BSLOG(DEBUG, "last/checkpoint: new pacekt stride: ", new_basetime_us, " - override ", tdPacketTimeStride_us);
            tdPacketTimeStride_us = new_basetime_us;

        }

        void update(int64_t packet_timestride_us)
        {
            tdPacketDuration_us = packet_timestride_us - tdPacketTimeStride_us;
            BSLOG(DEBUG, "last/update: stride ", tdPacketTimeStride_us, " -> ", packet_timestride_us,
                    " duration ", tdPacketDuration_us);
            tdPacketTimeStride_us = packet_timestride_us;
        }

    } m_Last;

    // XXX This should track bigger differences between the time distance resulting
    // from the packet time strides and series timestamps. Not in use yet.
    int64_t m_tdTimeSkew = 0;

    // Called with every packet after the whole header was read and interpreted,
    // just at the moment of DATA command (before giving up to reading a payload).
    // ONLY the payload size is set directly.
    // XXX Consider making the payload size also a parameter.
    void InterpretTS(int64_t media_timestamp, int packet_timestride)
    {
        // NOTE: possible cases are:
        // ONLY packet_timestride == 0, media_timestamp specified
        // - INITIAL packet of the series, CHECKPOINT.
        // ONLY media_timestamp == 0, packet_timestride specified
        // - SUBSEQUENT packet of the block
        // BOTH specified
        // - FIRST packet of the block, but subsequent block in the series
        BSLOG(DEBUG, "TS.media=", media_timestamp, " .packet=", packet_timestride);

        m_Header.packet_timestride_us = packet_timestride;

        if (packet_timestride == 0)
        {
            return Checkpoint(media_timestamp);
        }

        m_Last.update(packet_timestride);
    }

    // TNEN: after reading was done:
    void RecordSendTime()
    {
        m_Last.tsSendtime = ClockType::now();
        if (m_Checkpoint.sendtime == ClockTime()) // INITIAL setting
            m_Checkpoint.sendtime = m_Last.tsSendtime;
    }

    void Checkpoint(int64_t play_timestamp)
    {
        using namespace std::chrono;

        if (m_Checkpoint.update(play_timestamp))
        {
            // One checkpoint was made already, estimate the passed time

            //  LAST DISTANCE: --------------/-\..... == m_Last.tdPacketDuration_us
            // [I] [P] [P] [P] [P] [P] [P] [P] [P] [I]
            //  \-----------------------------------/ == m_Checkpoint.series_duration_us
            //                                      |
            // HERE -------------------------------/
            // We have last frame's packet time + duration
            int64_t packet_order_time = m_Last.next_stride();
            int64_t playout_order_time = m_Checkpoint.basetime_us;
            //
            // Skew is positive if we spent more time to send than we should.
            // We need to decrease the spending time, if so.
            //
            int64_t skew = m_tdTimeSkew + (packet_order_time - playout_order_time);
            int64_t borrow_skew = m_Last.tdPacketDuration_us * 0.75;

            BSLOG(DEBUG, "Checkpoint: playTS=", playout_order_time, " sendTS=", packet_order_time, " skew=", skew);

            if (skew < borrow_skew)
            {
                packet_order_time = playout_order_time;
                m_tdTimeSkew = 0;
            }
            else
            {
                // First time after checkpoint, but also with every next packet
                m_tdTimeSkew = skew - borrow_skew;
                packet_order_time -= borrow_skew;
            }

            m_Last.checkpoint(packet_order_time);
            BSLOG(DEBUG, "...BASE: playTS=", playout_order_time, " sendTS=", packet_order_time, " remain-skew=", m_tdTimeSkew);
        }
        else
        {
            BSLOG(DEBUG, "Checkpoint: first");
        }
    }

    int64_t GetUnitTimestamp_us()
    {
        return m_Checkpoint.basetime_us + m_Last.tdPacketTimeStride_us;
    }

    PIMP(std::istream& src): m_Datastream(src) {}

    bool PIMP_Prepare(int64_t basetime);
    MediaPacket PIMP_Read();
    bool PIMP_End() const;

    bool Refill();

    bool ReadCheckHeader();

    // Done initially or after getting a bigger portion
    void ResetState();

    // TRIGGERED WHEN: payload notch reaches payload size
    void Trigger_full_notch();
    // TRIGGERED WHEN: updated play rate is higher than previous play rate
    void Trigger_high_spike();

    std::string last_error;

    virtual bool TestRead(char matrix[4]) = 0;
    virtual std::string type() const = 0;
};

bool PacketReader::PIMP::ReadCheckHeader()
{
    char matrix[4];

    if (!TestRead((matrix)))
        return false;

    int dif = memcmp(matrix, g_header, 4);
    if (dif != 0)
    {
        last_error = "No 4-byte BSTOW signature found";
        return false;
    }

    return true;
}

PacketReader::PacketReader(const string& spec): pimp(NULL)
{
    pimp = Factory(spec);
}

bool PacketReader::Prepare(int64_t basetime) { return pimp->PIMP_Prepare(basetime); }
MediaPacket PacketReader::Read() { return pimp->PIMP_Read(); }
bool PacketReader::End() const { return pimp->PIMP_End(); }
std::string PacketReader::ErrorStr() const { return pimp->last_error; }
std::string PacketReader::type() const { return pimp->type(); }

template<class IntType>
inline IntType TryData(const vector<unsigned char>& data, IntType value)
{
    if (data.empty())
        return value;

    return hvu::ParseBE(data.data(), data.size());
}

// XXX Put this into a separate file of "bstow-format" module or something
static int Extract(const unsigned char* data, int& w_lab, int& w_val)
{
    int val3 = data[0];
    if ( (val3 & 0x80) == 0 )
    {
        w_lab = val3;
        w_val = (data[1] << 16) | (data[2] << 8) | data[3];
        return 0;
    }
    const unsigned char* udata = reinterpret_cast<const unsigned char*>(data);

    w_lab = ((udata[0] & 0x7F) << 8) | udata[1];
    int length = (udata[2] << 8) | udata[3];
    w_val = 0;
    return length;
}

struct ConsoleReader: public PacketReader::PIMP
{
    std::string type() const { return "file"; }

    ConsoleReader(): PacketReader::PIMP(std::cin) {}

    // For console reader, read explicitly since the beginning, with blocking.
    bool TestRead(char matrix[4]) override
    {
        m_Datastream.read(matrix, 4);
        if (m_Datastream.eof())
        {
            last_error = "Unexpected EOF when reading";
            return false;
        }

        return true;
    }
};

struct FileReader: public PacketReader::PIMP
{
    std::string type() const { return "file"; }
    ifstream ifile;
    FileReader(const string& path): PacketReader::PIMP(ifile)
    {
        ifile.exceptions(ios::failbit | ios::badbit);
        ifile.open(path);

        // Turn off after correctly open
        ifile.exceptions(ios::goodbit);
    }

    bool TestRead(char matrix[4]) override
    {
        // First 4 bytes expected to be a header
        m_Datastream.read(matrix, 4);
        if (m_Datastream.eof())
        {
            // You can't get EOF even if there's nothing more to read from the file.
            // If you really have nothing more to read, it's EOF + 0 bytes read.
            if (m_Datastream.gcount() != 0)
                last_error = "Unexpected EOF when reading";
            return false;
        }

        return true;
    }

};

PacketReader::PIMP* PacketReader::Factory(const string& spec)
{
    UriParser u (spec, UriParser::EXPECT_FILE);

    if (u.type() != UriParser::FILE)
        throw std::invalid_argument("Invalid specification");

    if (u.host() == "con")
    {
        return new ConsoleReader();
    }
    if (u.host() == "" && u.path() != "")
    {
        return new FileReader(u.path());
    }

    throw std::invalid_argument("Invalid specification");
}

// This must be called initially to refill the containers.
// This will be chain-called from PIMP_Read once it has extracted
// everything from the container
bool PacketReader::PIMP::PIMP_Prepare(int64_t basetime)
{
    if (!Refill())
        return false;

    // Extra initial settings?
    m_Checkpoint.basetime_us = basetime;
    BSLOG(DEBUG, "PREPARE: initial TS=", basetime);
    return true;
}

static map<int, string> g_parameter_names {
#define MAPNAME(name) { DEF_##name, #name }
    MAPNAME(LENGTH),
    MAPNAME(PLAYTIME),
    MAPNAME(SENDTIME),
    MAPNAME(DATA)
#undef MAPNAME
};

// Reads the next unit's data and updates things as needed.
bool PacketReader::PIMP::Refill()
{
    using namespace hvu;

    char matrix[4];

    if (!ReadCheckHeader())
        return false;

    int64_t block_timestamp_us = 0;
    int64_t packet_timestamp_us = 0;

    set<int> expected { DEF_LENGTH, DEF_PLAYTIME, DEF_SENDTIME };

    // Ok, now read by 4 portions until you get all data
    for (;;)
    {
        m_Datastream.read(matrix, 4);
        if (m_Datastream.eof())
        {
            last_error = "No 4-byte data available while parameters expected";
            return false;
        }

        int label, value;
        int xsize = Extract((const unsigned char*)matrix, (label), (value));
        if (xsize < 0)
            return false;

        // This should appear when all data are already read.
        // (We don't check for the value at all - it should be 0)
        if (label == DEF_DATA)
        {
            if (!expected.empty())
            {
                last_error = "DATA parameter found while not all parameters specified yet";
                return false;
            }

            if (packet_timestamp_us == 0 && block_timestamp_us == 0)
            {
                last_error = "TIMESTAMP/TIMESTRIDE not provided in this block";
                return false;
            }

            ClockTime wait_time;
            if (block_timestamp_us)
                wait_time = m_Checkpoint.expectedReceiveTime(block_timestamp_us);

            InterpretTS(block_timestamp_us, packet_timestamp_us);

            // XXX Controversial - might be that for console reading pace control
            // should not be done.
            if (wait_time != ClockTime())
            {
                // Can be in the past - if so it will simply do nothing.
                BSLOG(DEBUG, "Simulate non-encoder-ready: SLEEP FOR: ", FormatDurationAuto(wait_time - ClockType::now()));
                this_thread::sleep_until(wait_time);
            }

#ifdef BSTOW_ENABLE_VERBOSE
            static int64_t last_packet_timestamp_us = 0;
            if (block_timestamp_us)
            {
                ofprint(cerr, "\nF PTS=", fmtm(block_timestamp_us/1000000.0, std::fixed), " ");
                ofprint(cerr, "[", m_Header.packet_size, "]@{", packet_timestamp_us, "} ");
                last_packet_timestamp_us = packet_timestamp_us;
            }
            else
            {
                ofprint(cerr, "[", m_Header.packet_size, "]+", packet_timestamp_us - last_packet_timestamp_us, " ");
            }
#endif

            return true;
        }

        if (expected.empty()) // too early
        {
            hvu::ofmt_bufs out;
            out.print("Excessive parameters while all specified: ",
                    srt::map_get(g_parameter_names, label, hvu::ofcat("UNKNOWN:", label)));
            last_error = out.str();
            return false;
        }

        vector<unsigned char> databuffer;
        if (xsize)
        {
            databuffer.resize(xsize);
            m_Datastream.read((char*)databuffer.data(), databuffer.size());
            if (m_Datastream.eof())
            {
                last_error = hvu::ofcat("Error reading extended length of ", xsize, " for label=",
                        srt::map_get(g_parameter_names, label, "?"));
                return false;
            }
        }

        if (label == DEF_LENGTH)
        {
            // Theoretically to stay universal, it should do TryData<int64_t>,
            // but this is a size for a single network packet, so a 24-bit integer
            // should suffice.
            m_Header.packet_size = value;
            BSLOG(DEBUG, "... specified LENGTH:", value);
        }
        else if (label == DEF_PLAYTIME)
        {
            block_timestamp_us = TryData<int64_t>(databuffer, value);
            BSLOG(DEBUG, "... specified PLAYTIME:", block_timestamp_us, " removed also SENDTIME");

            // If PLAYTIME is specified, we allow SENDTIME to be not specified.
            expected.erase(DEF_SENDTIME);
        }
        else if (label == DEF_SENDTIME)
        {
            packet_timestamp_us = TryData<int64_t>(databuffer, value);
            BSLOG(DEBUG, "... specified SENDTIME:", packet_timestamp_us, " removed also PLAYTIME");

            expected.erase(DEF_PLAYTIME);
        }

        expected.erase(label);
    }
}


MediaPacket PacketReader::PIMP::PIMP_Read()
{
    MediaPacket mp;
    static int readcounter = 0;

    // NOTE: If you didn't call Prepare first, this will behave as EOF.
    if (m_Header.packet_size == 0)
        return mp;

    size_t takesize = m_Header.packet_size;

    // This call also does updates
    int64_t based_packettime_us = GetUnitTimestamp_us();

    // NOTE: Reading is done once and directly to the output.
    // XXX Consider any kind of alloc-uninitialized-and-read method
    mp.payload.resize(takesize);
    m_Datastream.read(mp.payload.data(), takesize);
    size_t nread = m_Datastream.gcount();
    mp.time = based_packettime_us;
    RecordSendTime();
    ++readcounter;

    // Just formally - this should be impossible
    if (nread > takesize)
    {
        throw std::runtime_error("Read more than requested");
    }
    if (nread != takesize)
    {
        // Likely unexpected EOF. Report whatever you have
        mp.payload.resize(nread);
    }
    if (m_Datastream.eof())
    {
        // Do nothing else. It's useless to make it retry reading.
        m_Header.packet_size = 0;
    }
    else
    {
        m_Header.packet_size -= nread;
        if (m_Header.packet_size == 0)
        {
            if (!Refill())
            {
                if (last_error != "")
                    hvu::ofprintl(cerr, "PacketReader::Refill: ERROR: ", last_error);
                return MediaPacket();
            }
            BSLOG(DEBUG, "# ", readcounter, " xp.{ play=", m_Checkpoint.playtime_us,
                    " base=", m_Checkpoint.basetime_us, " sdur=", m_Checkpoint.series_duration_us, " }",
                    " last.{ stride=", m_Last.tdPacketTimeStride_us,
                    " dur=", m_Last.tdPacketDuration_us, " }");
        }
    }

    return mp;
}

bool PacketReader::PIMP::PIMP_End() const
{
    return m_Header.packet_size == 0;
}

}

