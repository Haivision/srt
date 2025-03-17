/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Written by:
 *             Haivision Systems Inc.
 */

#include <array>
#include <future>
#include <thread>
#include <string>
#include <gtest/gtest.h>
#include "test_env.h"

// SRT includes
#include "any.hpp"
#include "socketconfig.h"
#include "srt.h"

using namespace std;
using namespace srt;

#define PLEASE_LOG 0

#if PLEASE_LOG
#define LOGD(args) args
#else
#define LOGD(args) (void)0
#endif

class TestOptionsCommon: public srt::Test
{
protected:
    TestOptionsCommon() = default;
    ~TestOptionsCommon() override = default;

    sockaddr_any m_sa;
    SRTSOCKET m_caller_sock = SRT_INVALID_SOCK;
    SRTSOCKET m_listen_sock = SRT_INVALID_SOCK;

    int       m_pollid = 0;
public:

    void BindListener() const
    {
        // Specify address of the listener
        const auto* psa = (const sockaddr*)&m_sa;
        ASSERT_NE(srt_bind(m_listen_sock, psa, sizeof m_sa), SRT_ERROR);
    }

    void StartListener() const
    {
        BindListener();

        srt_listen(m_listen_sock, 1);
    }

    int Connect() const
    {
        const auto* psa = (const sockaddr*)&m_sa;
        return srt_connect(m_caller_sock, psa, sizeof m_sa);
    }

    SRTSOCKET EstablishConnection()
    {
        auto accept_async = [](SRTSOCKET listen_sock) {
            sockaddr_in client_address;
            int length = sizeof(sockaddr_in);
            const SRTSOCKET accepted_socket = srt_accept(listen_sock, (sockaddr*)&client_address, &length);
            return accepted_socket;
        };
        auto accept_res = async(launch::async, accept_async, m_listen_sock);

        // Make sure the thread was kicked
        this_thread::yield();

        const int connect_res = Connect();
        EXPECT_EQ(connect_res, SRT_SUCCESS);

        const SRTSOCKET accepted_sock = accept_res.get();
        EXPECT_NE(accepted_sock, SRT_INVALID_SOCK);

        return accepted_sock;
    }

    void teardown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        EXPECT_NE(srt_close(m_caller_sock), SRT_ERROR);
        EXPECT_NE(srt_close(m_listen_sock), SRT_ERROR);
    }
};


class TestSocketOptions: public TestOptionsCommon
{
protected:
    TestSocketOptions() = default;
    ~TestSocketOptions() override = default;

    // setup() is run immediately before a test starts.
    void setup() override
    {
        const int yes = 1;
        m_sa = srt::CreateAddr("127.0.0.1", 5200, AF_INET);
        ASSERT_FALSE(m_sa.empty());

        m_caller_sock = srt_create_socket();
        ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();
        m_listen_sock = srt_create_socket();
        ASSERT_NE(m_listen_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

        ASSERT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
        ASSERT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_SNDSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect

        ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
        ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_SNDSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
    }
};

#if ENABLE_BONDING
// Test group options
class TestGroupOptions: public TestOptionsCommon
{
protected:
    TestGroupOptions() = default;
    ~TestGroupOptions() override = default;

    // Is run immediately before a test starts.
    void setup() override
    {
        const int yes = 1;

        m_sa = srt::CreateAddr("127.0.0.1", 5200, AF_INET);
        ASSERT_FALSE(m_sa.empty());

        m_caller_sock = srt_create_group(SRT_GTYPE_BROADCAST);
        ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();
        m_listen_sock = srt_create_socket();
        ASSERT_NE(m_listen_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

        ASSERT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
        ASSERT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_SNDSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect

        ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
        ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_SNDSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
        ASSERT_EQ(srt_setsockflag(m_listen_sock, SRTO_GROUPCONNECT, &yes, sizeof yes), SRT_SUCCESS);
    }
};
#endif

#if PLEASE_LOG
static std::string to_string(const linb::any& val)
{
    using namespace linb;

    if (val.type() == typeid(const char*))
    {
        return any_cast<const char*>(val);
    }

    std::ostringstream out;
    if (val.type() == typeid(bool))
    {
        out << any_cast<bool>(val);
    }
    else if (val.type() == typeid(int))
    {
        out << any_cast<int>(val);
    }
    else if (val.type() == typeid(int64_t))
    {
        out << any_cast<int64_t>(val);
    }
    else
    {
        return "<bad-any-cast>";
    }

    return out.str();
}
#endif

enum class RestrictionType
{
    PREBIND = 0,
    PRE     = 1,
    POST    = 2
};

// FLAGS
// - READABLE (R)
//   : You can call srt_getsockflag
// - WRITABLE (W)
//   : You can call srt_setsockflag
// - SOCKETWISE (S)
//   : Can be set on a socket
// - GROUPWISE (G)
//   : Can be set on a group
// - DERIVED (D)
//   : TRUE: If it's set on the group, it will be derived by members
//   : FALSE: It cannot be set on the group to be derived by members
// - GROUPUNIQUE (I)
//   : TRUE: If set on the group, it's assigned to a group (not members)
//   : FALSE: If set on the group, it's derived by members
// - MODIFIABLE (M)
//   : TRUE: Can be set on individual member socket differently.
//   : FALSE: Cannot be altered on the individual member socket.

namespace Flags
{
enum type: char
{
    O = 0, // Marker for an unset flag

    R = 1 << 0,  // readable
    W = 1 << 1,  // writable
    S = 1 << 2,  // can be set on single socket
    G = 1 << 3,  // can be set on group
    D = 1 << 4,  // when set on group, derived by the socket
    I = 1 << 5,  // when set on group, it concerns group only
    M = 1 << 6   // can be modified on individual member
};

inline type operator|(type f1, type f2)
{
    char val = char(f1) | char(f2);
    return type(val);
}

inline bool operator&(type ff, type mask)
{
    char val = char(ff) & char(mask);
    return val == mask;
}

const std::string str(type t)
{
    static const char names [] = "RWSGDI+";
    std::string out;

    for (int i = 0; i < 7; ++i)
        if (int(t) & (1 << i))
            out += names[i];

    if (out.empty())
        return "O";
    return out;
}
}

const char* RestrictionTypeStr(RestrictionType val)
{
    static const std::map<RestrictionType, const char*> type_to_str = {
        { RestrictionType::PREBIND, "PREBIND" },
        { RestrictionType::PRE,     "PRE" },
        { RestrictionType::POST,    "POST" }
    };

    return type_to_str.find(val) != type_to_str.end() ? type_to_str.at(val) : "INVALID";
}

struct OptionTestEntry
{
    SRT_SOCKOPT optid;
    const char* optname;            // TODO: move to a separate array, function or std::map.
    RestrictionType restriction;    // TODO: consider using SRTO_R_PREBIND, etc. from core.cpp 
    size_t  opt_len;
    linb::any min_val;
    linb::any max_val;
    linb::any dflt_val;
    linb::any ndflt_val; 
    vector<linb::any> invalid_vals;
    Flags::type flags;

    bool allof() const { return true; }

    template<typename... Args>
    bool allof(Flags::type flg, Args... args) const
    {
        return flags & flg && allof(args...);
    }

    bool anyof() const { return false; }

    template<typename... Args>
    bool anyof(Flags::type flg, Args... args) const
    {
        return flags & flg || anyof(args...);
    }
};

static const size_t UDP_HDR_SIZE = 28;   // 20 bytes IPv4 + 8 bytes of UDP { u16 sport, dport, len, csum }.
static const size_t DFT_MTU_SIZE = 1500; // Default MTU size
static const size_t SRT_PKT_SIZE = DFT_MTU_SIZE - UDP_HDR_SIZE; // MTU without UDP header

namespace Table
{
    // A trick to localize 1-letter flags without exposing
    // them for the rest of the file.
using namespace Flags;

const OptionTestEntry g_test_matrix_options[] =
{
    //                                                                                                                                                                 Place 'O' if not set.
    // Option ID,                Option Name |          Restriction |         optlen |             min |       max |  default | nondefault    | invalid vals | flags:  R | W | G | S | D | I | M

    //SRTO_BINDTODEVICE                                                                                                                                                R | W | G | S | D | I | M
    //{ SRTO_CONGESTION,      "SRTO_CONGESTION",  RestrictionType::PRE,               4,           "live",     "file",   "live",       "file",   {"liv", ""},          O | W | O | S | O | O | O },
    { SRTO_CONNTIMEO,        "SRTO_CONNTIMEO",  RestrictionType::PRE,     sizeof(int),                0,  INT32_MAX,     3000,          250,   {-1},                   O | W | G | S | D | O | M },
    { SRTO_DRIFTTRACER,    "SRTO_DRIFTTRACER",  RestrictionType::POST,   sizeof(bool),            false,       true,     true,        false,     {},                   R | W | G | S | D | O | O },
    { SRTO_ENFORCEDENCRYPTION, "SRTO_ENFORCEDENCRYPTION", RestrictionType::PRE, sizeof(bool),     false,       true,     true,        false,     {},                   O | W | G | S | D | O | O },
    //SRTO_EVENT                                                                                                                                                       R | O | O | S | O | O | O
    { SRTO_FC,                      "SRTO_FC",  RestrictionType::PRE,     sizeof(int),               32,  INT32_MAX,    25600,        10000,   {-1, 31},               R | W | G | S | D | O | O },
    //SRTO_GROUPCONNECT                                                                                                                                                O | W | O | S | O | O | O 
#if ENABLE_BONDING
    // Max value can't exceed SRTO_PEERIDLETIMEO
    { SRTO_GROUPMINSTABLETIMEO, "SRTO_GROUPMINSTABLETIMEO", RestrictionType::PRE, sizeof(int),       60,       5000,       60,        70, {0, -1, 50, 5001},           O | W | G | O | D | I | M },
#endif
    //SRTO_GROUPTYPE
    //SRTO_INPUTBW
    //SRTO_IPTOS
    //SRTO_IPTTL
    //SRTO_IPV6ONLY
    //SRTO_ISN
    { SRTO_KMPREANNOUNCE, "SRTO_KMPREANNOUNCE", RestrictionType::PRE,     sizeof(int),                 0, INT32_MAX,        0,         1024,   {-1},                   O | W | G | S | D | O | O },
    { SRTO_KMREFRESHRATE, "SRTO_KMREFRESHRATE", RestrictionType::PRE,     sizeof(int),                 0, INT32_MAX,        0,         1024,   {-1},                   O | W | G | S | D | O | O },
    //SRTO_KMSTATE
    { SRTO_LATENCY,             "SRTO_LATENCY", RestrictionType::PRE,     sizeof(int),                 0, INT32_MAX,      120,          200,  {-1},                    R | W | G | S | D | O | O },
    //SRTO_LINGER
    { SRTO_LOSSMAXTTL,       "SRTO_LOSSMAXTTL", RestrictionType::POST,    sizeof(int),                 0, INT32_MAX,        0,           10,   {},                     R | W | G | S | D | O | M },
    { SRTO_MAXBW,                 "SRTO_MAXBW", RestrictionType::POST, sizeof(int64_t),      int64_t(-1),  INT64_MAX, int64_t(-1), int64_t(200000),  {int64_t(-2)},    R | W | G | S | D | O | O },
#ifdef ENABLE_MAXREXMITBW
    { SRTO_MAXREXMITBW,      "SRTO_MAXREXMITBW", RestrictionType::POST, sizeof(int64_t),     int64_t(-1), INT64_MAX,  int64_t(-1), int64_t(200000),  {int64_t(-2)},    R | W | G | S | D | O | O },
#endif
    { SRTO_MESSAGEAPI,       "SRTO_MESSAGEAPI", RestrictionType::PRE,    sizeof(bool),             false,      true,     true,        false,     {},                   O | W | G | S | D | O | O },
    { SRTO_MININPUTBW,       "SRTO_MININPUTBW", RestrictionType::POST, sizeof(int64_t),       int64_t(0),  INT64_MAX,  int64_t(0), int64_t(200000),  {int64_t(-1)},    R | W | G | S | D | O | O },
    { SRTO_MINVERSION,       "SRTO_MINVERSION", RestrictionType::PRE,     sizeof(int),                 0,  INT32_MAX, 0x010000,    0x010300,    {},                    R | W | G | S | D | O | O },
    { SRTO_MSS,                     "SRTO_MSS", RestrictionType::PREBIND, sizeof(int),                76,     65536,     1500,        1400,    {-1, 0, 75},            R | W | G | S | D | O | O },
    { SRTO_NAKREPORT,         "SRTO_NAKREPORT", RestrictionType::PRE,    sizeof(bool),             false,      true,     true,        false,     {},                   R | W | G | S | D | O | M },
    { SRTO_OHEADBW,             "SRTO_OHEADBW", RestrictionType::POST,    sizeof(int),                 5,        100,       25,          20, {-1, 0, 4, 101},          R | W | G | S | D | O | O },
    //SRTO_PACKETFILTER
    //SRTO_PASSPHRASE
    { SRTO_PAYLOADSIZE,     "SRTO_PAYLOADSIZE", RestrictionType::PRE,     sizeof(int),                 0,      1456,      1316,        1400,   {-1, 1500},             O | W | G | S | D | O | O },
    //SRTO_PBKEYLEN
    { SRTO_PEERIDLETIMEO, "SRTO_PEERIDLETIMEO", RestrictionType::PRE,     sizeof(int),                 0, INT32_MAX,      5000,        4500,    {-1},                  R | W | G | S | D | O | M },
    { SRTO_PEERLATENCY,     "SRTO_PEERLATENCY", RestrictionType::PRE,     sizeof(int),                 0, INT32_MAX,         0,        180,    {-1},                   R | W | G | S | D | O | O },
    //SRTO_PEERVERSION
    { SRTO_RCVBUF,              "SRTO_RCVBUF",  RestrictionType::PREBIND, sizeof(int), (int)(32 * SRT_PKT_SIZE), 2147483256, (int)(8192 * SRT_PKT_SIZE), 1000000, {-1},R | W | G | S | D | O | M },
    //SRTO_RCVDATA
    //SRTO_RCVKMSTATE
    { SRTO_RCVLATENCY,       "SRTO_RCVLATENCY", RestrictionType::PRE,     sizeof(int),                 0, INT32_MAX, 120, 1100, {-1},                                  R | W | G | S | D | O | O },
    //SRTO_RCVSYN
    { SRTO_RCVTIMEO,           "SRTO_RCVTIMEO", RestrictionType::POST,    sizeof(int),                -1, INT32_MAX,  -1, 2000, {-2},                                  R | W | G | S | O | I | O },
    //SRTO_RENDEZVOUS
    { SRTO_RETRANSMITALGO, "SRTO_RETRANSMITALGO", RestrictionType::PRE,   sizeof(int),                 0,         1,   1,    0, {-1, 2},                               R | W | G | S | D | O | O },
    //SRTO_REUSEADDR
    //SRTO_SENDER
    { SRTO_SNDBUF,              "SRTO_SNDBUF",  RestrictionType::PREBIND, sizeof(int), (int)(32 * SRT_PKT_SIZE), 2147483256, (int)(8192 * SRT_PKT_SIZE), 1000000, {-1},R | W | G | S | D | O | M },
    //SRTO_SNDDATA
    { SRTO_SNDDROPDELAY,  "SRTO_SNDDROPDELAY", RestrictionType::POST,     sizeof(int),                -1, INT32_MAX, 0, 1500, {-2},                                    O | W | G | S | D | O | M },
    //SRTO_SNDKMSTATE
    //SRTO_SNDSYN
    { SRTO_SNDTIMEO,          "SRTO_SNDTIMEO", RestrictionType::POST,     sizeof(int),                -1, INT32_MAX, -1, 1400, {-2},                                   R | W | G | S | O | I | O },
    //SRTO_STATE
    //SRTO_STREAMID
    { SRTO_TLPKTDROP,        "SRTO_TLPKTDROP",  RestrictionType::PRE,    sizeof(bool),             false,      true,     true, false, {},                              R | W | G | S | D | O | O },
    //SRTO_TRANSTYPE
    //SRTO_TSBPDMODE
    //SRTO_UDP_RCVBUF
    //SRTO_UDP_SNDBUF
    //SRTO_VERSION
};
} // end namespace Table

using Table::g_test_matrix_options;

template<class ValueType>
void CheckGetSockOptMustFail(const OptionTestEntry& entry, SRTSOCKET sock, const char* desc)
{
    ValueType opt_val;
    int opt_len = (int)entry.opt_len;
    EXPECT_NE(srt_getsockopt(sock, 0, entry.optid, &opt_val, &opt_len), SRT_SUCCESS)
        << desc << " Getting " << entry.optname << " must fail, but succeeded.";
}

template<class ValueType>
void CheckGetSockOpt(const OptionTestEntry& entry, SRTSOCKET sock, const ValueType& value, const char* desc)
{
    ValueType opt_val;
    int opt_len = (int) entry.opt_len;
    EXPECT_EQ(srt_getsockopt(sock, 0, entry.optid, &opt_val, &opt_len), SRT_SUCCESS)
        << "Getting " << entry.optname << " returned error: " << srt_getlasterror_str();

    EXPECT_EQ(opt_val, value) << desc << ": Wrong " << entry.optname << " value " << opt_val;
    EXPECT_EQ(opt_len, (int) entry.opt_len) << desc << "Wrong " << entry.optname << " value length";
}

using strptr = const char *;
template<>
void CheckGetSockOpt<strptr>(const OptionTestEntry& entry, SRTSOCKET sock, const strptr& value, const char* desc)
{
    std::array<char, 16> opt_val;
    int opt_len = 0;
    EXPECT_EQ(srt_getsockopt(sock, 0, entry.optid, opt_val.data(), &opt_len), SRT_SUCCESS)
        << "Getting " << entry.optname << " returned error: " << srt_getlasterror_str();

    EXPECT_EQ(strncmp(opt_val.data(), value, min(opt_len, (int)entry.opt_len)), 0) << desc << ": Wrong " << entry.optname << " value " << opt_val.data();
    EXPECT_EQ(opt_len, (int) entry.opt_len) << desc << "Wrong " << entry.optname << " value length";
}

template<class ValueType>
void CheckSetSockOpt(const OptionTestEntry& entry, SRTSOCKET sock, const ValueType& value, int expect_return, const char* desc)
{
    ValueType opt_val = value;
    int opt_len = (int)entry.opt_len;
    EXPECT_EQ(srt_setsockopt(sock, 0, entry.optid, &opt_val, opt_len), expect_return)
        << "Setting " << entry.optname << " to " << opt_val << " must " << (expect_return == SRT_SUCCESS ? "succeed" : "fail");

    if (expect_return == SRT_SUCCESS)
    {
        CheckGetSockOpt<ValueType>(entry, sock, value, desc);
    }
    // TODO: else check the previous value is in force
}

template<class ValueType>
bool CheckDefaultValue(const OptionTestEntry& entry, SRTSOCKET sock, const char* desc)
{
    LOGD(cerr << "Will check default value: " << entry.optname << " = " << to_string(entry.dflt_val) << ": " << desc << endl);
    try {
        const ValueType dflt_val = linb::any_cast<ValueType>(entry.dflt_val);
        CheckGetSockOpt<ValueType>(entry, sock, dflt_val, desc);
    }
    catch (const linb::bad_any_cast&)
    {
        std::cerr << entry.optname << " default value type: " << entry.dflt_val.type().name() << "\n";
        return false;
    }

    return true;
}

template<class ValueType>
bool CheckSetNonDefaultValue(const OptionTestEntry& entry, SRTSOCKET sock, int expected_return, const char* desc)
{
    try {
        /*const ValueType dflt_val = linb::any_cast<ValueType>(entry.dflt_val);
        const ValueType min_val  = linb::any_cast<ValueType>(entry.min_val);
        const ValueType max_val  = linb::any_cast<ValueType>(entry.max_val);*/
        //const ValueType ndflt_val = (min_val != dflt_val) ? min_val : max_val;

        const ValueType ndflt_val = linb::any_cast<ValueType>(entry.ndflt_val);;

        CheckSetSockOpt<ValueType>(entry, sock, ndflt_val, expected_return, desc);
    }
    catch (const linb::bad_any_cast&)
    {
        std::cerr << entry.optname << " non-default value type: " << entry.ndflt_val.type().name() << "\n";
        return false;
    }

    return true;
}

template<class ValueType>
bool CheckMinValue(const OptionTestEntry& entry, SRTSOCKET sock, const char* desc)
{
    try {
        const ValueType min_val = linb::any_cast<ValueType>(entry.min_val);
        CheckSetSockOpt<ValueType>(entry, sock, min_val, SRT_SUCCESS, desc);

        const ValueType dflt_val = linb::any_cast<ValueType>(entry.dflt_val);
        CheckSetSockOpt<ValueType>(entry, sock, dflt_val, SRT_SUCCESS, desc);
    }
    catch (const linb::bad_any_cast&)
    {
        std::cerr << entry.optname << " min value type: " << entry.min_val.type().name() << "\n";
        return false;
    }

    return true;
}

template<class ValueType>
bool CheckMaxValue(const OptionTestEntry& entry, SRTSOCKET sock, const char* desc)
{
    try {
        const ValueType max_val = linb::any_cast<ValueType>(entry.max_val);
        CheckSetSockOpt<ValueType>(entry, sock, max_val, SRT_SUCCESS, desc);
    }
    catch (const linb::bad_any_cast&)
    {
        std::cerr << entry.optname << " max value type: " << entry.max_val.type().name() << "\n";
        return false;
    }

    return true;
}

template<class ValueType>
bool CheckInvalidValues(const OptionTestEntry& entry, SRTSOCKET sock, const char* sock_name)
{
    for (const auto& inval : entry.invalid_vals)
    {
        LOGD(cerr << "Will check INVALID value: " << entry.optname << " : " << to_string(inval) << ": " << sock_name << endl);
        try {
            const ValueType val = linb::any_cast<ValueType>(inval);
            CheckSetSockOpt<ValueType>(entry, sock, val, SRT_ERROR, sock_name);
        }
        catch (const linb::bad_any_cast&)
        {
            std::cerr << entry.optname << " value type: " << inval.type().name() << "\n";
            return false;
        }
    }

    return true;
}

void TestDefaultValues(SRTSOCKET s)
{
    const char* test_desc = "[Caller, default]";
    for (const auto& entry : g_test_matrix_options)
    {
        // Check flags. An option must be RW to test default value
        const bool is_group = (s & SRTGROUP_MASK) != 0;

        if (!(entry.flags & Flags::R))
        {
            // TODO: Check reading retuns an error.
            LOGD(cerr << "Skipping " << entry.optname << ": not readable.\n");
            continue; // The flag must be READABLE and WRITABLE for this.
        }

        // Check that retrieving a value must fail if the option is not a group option read on a group and not a socket
        // option read on a socket.
        bool readable = true;
        if (is_group)
        {
            readable = entry.allof(Flags::G) && entry.anyof(Flags::I, Flags::D);
            LOGD(cerr << "Group option " << entry.optname << ": expected " << (readable? "" : "NOT ") << "readable\n");
        }
        else
        {
            readable = entry.allof(Flags::S);
            LOGD(cerr << "Socket option " << entry.optname << ": expected " << (readable? "" : "NOT ") << "readable\n");
        }

        if (!readable)
        {
            if (entry.dflt_val.type() == typeid(bool))
            {
                CheckGetSockOptMustFail<bool>(entry, s, test_desc);
            }
            else if (entry.dflt_val.type() == typeid(int))
            {
                CheckGetSockOptMustFail<int>(entry, s, test_desc);
            }
            else if (entry.dflt_val.type() == typeid(int64_t))
            {
                CheckGetSockOptMustFail<int64_t>(entry, s, test_desc);
            }
            else if (entry.dflt_val.type() == typeid(const char*))
            {
                CheckGetSockOptMustFail<const char*>(entry, s, test_desc);
            }
            else
            {
                FAIL() << entry.optname << ": Unexpected type " << entry.dflt_val.type().name();
            }

            continue; // s is group && The option is not groupwise-individual option
        }

        if (entry.dflt_val.type() == typeid(bool))
        {
            EXPECT_TRUE(CheckDefaultValue<bool>(entry, s, test_desc));
        }
        else if (entry.dflt_val.type() == typeid(int))
        {
            EXPECT_TRUE(CheckDefaultValue<int>(entry, s, test_desc));
        }
        else if (entry.dflt_val.type() == typeid(int64_t))
        {
            EXPECT_TRUE(CheckDefaultValue<int64_t>(entry, s, test_desc));
        }
        else if (entry.dflt_val.type() == typeid(const char*))
        {
            EXPECT_TRUE(CheckDefaultValue<const char*>(entry, s, test_desc));
        }
        else
        {
            FAIL() << entry.optname << ": Unexpected type " << entry.dflt_val.type().name();
        }
    }
}

TEST_F(TestSocketOptions, DefaultVals)
{
    TestDefaultValues(m_caller_sock);
}

#if ENABLE_BONDING
TEST_F(TestGroupOptions, DefaultVals)
{
    SRTST_REQUIRES(Bonding);
    TestDefaultValues(m_caller_sock);
}
#endif

TEST_F(TestSocketOptions, MaxVals)
{
    // Note: Changing SRTO_FC changes SRTO_RCVBUF limitation
    for (const auto& entry : g_test_matrix_options)
    {
        if (!(entry.flags & Flags::R))
        {
            cerr << "Skipping " << entry.optname << ": option not readable\n";
        }

        if (!(entry.flags & Flags::W))
        {
            cerr << "Skipping " << entry.optname << ": option not writable\n";
        }

        if (entry.optid == SRTO_KMPREANNOUNCE || entry.optid == SRTO_KMREFRESHRATE)
        {
            cerr << "Skipping " << entry.optname << "\n";
            continue;
        }

        const char* test_desc = "[Caller, max value]";
        if (entry.max_val.type() == typeid(bool))
        {
            EXPECT_TRUE(CheckMaxValue<bool>(entry, m_caller_sock, test_desc));
        }
        else if (entry.max_val.type() == typeid(int))
        {
            EXPECT_TRUE(CheckMaxValue<int>(entry, m_caller_sock, test_desc));
        }
        else if (entry.max_val.type() == typeid(int64_t))
        {
            EXPECT_TRUE(CheckMaxValue<int64_t>(entry, m_caller_sock, test_desc));
        }
        else
        {
            FAIL() << "Unexpected type " << entry.max_val.type().name();
        }

        // TODO: back to default ?
    }
}

TEST_F(TestSocketOptions, MinVals)
{
    // Note: Changing SRTO_FC changes SRTO_RCVBUF limitation
    for (const auto& entry : g_test_matrix_options)
    {
        if (!(entry.flags & Flags::R))
        {
            cerr << "Skipping " << entry.optname << ": option not readable\n";
        }

        if (!(entry.flags & Flags::W))
        {
            cerr << "Skipping " << entry.optname << ": option not writable\n";
        }

        const char* test_desc = "[Caller, min val]";
        if (entry.min_val.type() == typeid(bool))
        {
            EXPECT_TRUE(CheckMinValue<bool>(entry, m_caller_sock, test_desc));
        }
        else if (entry.min_val.type() == typeid(int))
        {
            EXPECT_TRUE(CheckMinValue<int>(entry, m_caller_sock, test_desc));
        }
        else if (entry.min_val.type() == typeid(int64_t))
        {
            EXPECT_TRUE(CheckMinValue<int64_t>(entry, m_caller_sock, test_desc));
        }
        else
        {
            FAIL() << entry.optname << ": Unexpected type " << entry.min_val.type().name();
        }

        // TODO: back to default
    }
}

void TestInvalidValues(SRTSOCKET s)
{
    // Note: Changing SRTO_FC changes SRTO_RCVBUF limitation
    for (const auto& entry : g_test_matrix_options)
    {
        if (!(entry.flags & Flags::W))
        {
            cerr << "Skipping " << entry.optname << ": option not writable\n";
        }

        const char* desc = "[Group Caller, invalid val]";
        if (entry.dflt_val.type() == typeid(bool))
        {
            EXPECT_TRUE(CheckInvalidValues<bool>(entry, s, desc));
        }
        else if (entry.dflt_val.type() == typeid(int))
        {
            EXPECT_TRUE(CheckInvalidValues<int>(entry, s, desc));
        }
        else if (entry.dflt_val.type() == typeid(int64_t))
        {
            EXPECT_TRUE(CheckInvalidValues<int64_t>(entry, s, desc));
        }
        else
        {
            FAIL() << "Unexpected type " << entry.dflt_val.type().name();
        }

        // TODO: expect default is still in force?
    }
}

TEST_F(TestSocketOptions, InvalidVals)
{
    TestInvalidValues(m_caller_sock);
}


#if ENABLE_BONDING
TEST_F(TestGroupOptions, InvalidVals)
{
    SRTST_REQUIRES(Bonding);
    TestInvalidValues(m_caller_sock);
}
#endif

const char* StateToStr(SRT_SOCKSTATUS st)
{
    std::map<SRT_SOCKSTATUS, const char* const> st_to_str = {
        { SRTS_INIT,       "SRTS_INIT" },
        { SRTS_OPENED,     "SRTS_OPENED" },
        { SRTS_LISTENING,  "SRTS_LISTENING" },
        { SRTS_CONNECTING, "SRTS_CONNECTING" },
        { SRTS_CONNECTED,  "SRTS_CONNECTED" },
        { SRTS_BROKEN,     "SRTS_BROKEN" },
        { SRTS_CLOSING,    "SRTS_CLOSING" },
        { SRTS_CLOSED,     "SRTS_CLOSED" },
        { SRTS_NONEXIST,   "SRTS_NONEXIST" }
    };

    return st_to_str.find(st) != st_to_str.end() ? st_to_str.at(st) : "INVALID";
}

#if 0
// No socket option can be set in blocking mode because m_ConnectionLock is required by both srt_setsockopt and srt_connect
// TODO: Use non-blocking mode
TEST_F(TestSocketOptions, RestrictionCallerConnecting)
{
    // The default SRTO_CONNTIMEO is 3 seconds. It is assumed all socket options could be checked.
    auto connect_async = [this]() {
        return Connect();
    };
    auto connect_res = async(launch::async, connect_async);

    for (int i = 0; i < 100; ++i)
    {
        if (srt_getsockstate(m_caller_sock) == SRTS_CONNECTING)
            break;

        this_thread::sleep_for(chrono::microseconds(100));
    }

    cout << "Running test\n";

    for (const auto& entry : g_test_matrix_options)
    {
        if (entry.restriction != RestrictionType::PRE)
            continue;

        // Setting a valid minimum value
        EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, entry.optid, &entry.min_val, entry.opt_len), SRT_ERROR)
            << "Setting " << entry.optname << " (PRE) must not succeed while connecting. Sock state: " << g_socket_state[srt_getsockstate(m_caller_sock)];
    }

    connect_res.get();
}
#endif

TEST_F(TestSocketOptions, RestrictionBind)
{
    BindListener();

    for (const auto& entry : g_test_matrix_options)
    {
        const char* test_desc = "[Caller, after bind]";
        const int expected_res = (entry.restriction == RestrictionType::PREBIND) ? SRT_ERROR : SRT_SUCCESS;

        if (entry.dflt_val.type() == typeid(bool))
        {
            EXPECT_TRUE(CheckSetNonDefaultValue<bool>(entry, m_listen_sock, expected_res, test_desc))
                << "Sock state : " << StateToStr(srt_getsockstate(m_listen_sock));
        }
        else if (entry.dflt_val.type() == typeid(int))
        {
            EXPECT_TRUE(CheckSetNonDefaultValue<int>(entry, m_listen_sock, expected_res, test_desc))
                << "Sock state : " << StateToStr(srt_getsockstate(m_listen_sock));
        }
        else if (entry.dflt_val.type() == typeid(int64_t))
        {
            EXPECT_TRUE(CheckSetNonDefaultValue<int64_t>(entry, m_listen_sock, expected_res, test_desc))
                << "Sock state : " << StateToStr(srt_getsockstate(m_listen_sock));
        }
        else
        {
            FAIL() << "Unexpected type " << entry.dflt_val.type().name();
        }
    }
}

// Check that only socket option with POST binding can be set on a listener socket in "listening" state.
TEST_F(TestSocketOptions, RestrictionListening)
{
    StartListener();

    for (const auto& entry : g_test_matrix_options)
    {
        const int expected_res = (entry.restriction != RestrictionType::POST) ? SRT_ERROR : SRT_SUCCESS;

        // Setting a valid minimum value
        const char* test_desc ="[Listener, listening]";

        if (entry.dflt_val.type() == typeid(bool))
        {
            EXPECT_TRUE(CheckSetNonDefaultValue<bool>(entry, m_listen_sock, expected_res, test_desc))
                << test_desc << entry.optname << " Sock state: " << StateToStr(srt_getsockstate(m_listen_sock));
        }
        else if (entry.dflt_val.type() == typeid(int))
        {
            EXPECT_TRUE(CheckSetNonDefaultValue<int>(entry, m_listen_sock, expected_res, test_desc))
                << test_desc << entry.optname << " Sock state: " << StateToStr(srt_getsockstate(m_listen_sock));
        }
        else if (entry.dflt_val.type() == typeid(int64_t))
        {
            EXPECT_TRUE(CheckSetNonDefaultValue<int64_t>(entry, m_listen_sock, expected_res, test_desc))
                << test_desc << entry.optname << " Sock state: " << StateToStr(srt_getsockstate(m_listen_sock));
        }
        else
        {
            FAIL() << "Unexpected type " << entry.dflt_val.type().name();
        }
    }
}

// Check that only socket option with POST binding can be set on a connected socket (caller and accepted).
TEST_F(TestSocketOptions, RestrictionConnected)
{
    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    for (const auto& entry : g_test_matrix_options)
    {
        const int expected_res = (entry.restriction != RestrictionType::POST) ? SRT_ERROR : SRT_SUCCESS;

        // Setting a valid minimum value
        for (SRTSOCKET sock : { m_caller_sock, accepted_sock })
        {
            const char* test_desc = sock == m_caller_sock ? "[Caller, connected]" : "[Accepted, connected]";

            if (entry.dflt_val.type() == typeid(bool))
            {
                EXPECT_TRUE(CheckSetNonDefaultValue<bool>(entry, sock, expected_res, test_desc))
                    << test_desc << entry.optname << " Sock state: " << StateToStr(srt_getsockstate(sock));
            }
            else if (entry.dflt_val.type() == typeid(int))
            {
                EXPECT_TRUE(CheckSetNonDefaultValue<int>(entry, sock, expected_res, test_desc))
                    << test_desc << entry.optname << " Sock state: " << StateToStr(srt_getsockstate(sock));
            }
            else if (entry.dflt_val.type() == typeid(int64_t))
            {
                EXPECT_TRUE(CheckSetNonDefaultValue<int64_t>(entry, sock, expected_res, test_desc))
                    << test_desc << entry.optname << " Sock state: " << StateToStr(srt_getsockstate(sock));
            }
            else
            {
                FAIL() << "Unexpected type " << entry.dflt_val.type().name();
            }
        }
    }
}

// TODO: TEST_F(TestSocketOptions, CheckInheritedAfterConnection)
// Check that accepted socket has correct socket option values.
// Check setting and getting SRT_MININPUTBW
TEST_F(TestSocketOptions, TLPktDropInherits)
{
    const bool tlpktdrop_dflt = true;
    const bool tlpktdrop_new  = false;
    
    bool optval = tlpktdrop_dflt;
    int optlen  = (int)(sizeof optval);
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_TLPKTDROP, &tlpktdrop_new, sizeof tlpktdrop_new), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_TLPKTDROP, &optval, &optlen), SRT_SUCCESS);
    EXPECT_EQ(optval, tlpktdrop_new);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    for (SRTSOCKET sock : { m_listen_sock, accepted_sock })
    {
        optval = tlpktdrop_dflt;
        optlen = (int)(sizeof optval);
        EXPECT_EQ(srt_getsockopt(sock, 0, SRTO_TLPKTDROP, &optval, &optlen), SRT_SUCCESS);
        EXPECT_EQ(optlen, (int)(sizeof optval));
        EXPECT_EQ(optval, tlpktdrop_new);
    }

    this_thread::sleep_for(chrono::seconds(2));

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

TEST_F(TestSocketOptions, Latency)
{
    const int latency_a    = 140;
    const int latency_b    = 100;
    const int latency_dflt = 120;

    int optval;
    int optlen = (int)(sizeof optval);
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_RCVLATENCY,  &latency_a, sizeof latency_a), SRT_SUCCESS);
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PEERLATENCY, &latency_b, sizeof latency_b), SRT_SUCCESS);

    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_RCVLATENCY, &optval, &optlen), SRT_SUCCESS);
    EXPECT_EQ(optval, latency_a);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PEERLATENCY, &optval, &optlen), SRT_SUCCESS);
    EXPECT_EQ(optval, latency_b);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check caller socket
    EXPECT_EQ(srt_getsockopt(m_caller_sock, 0, SRTO_RCVLATENCY, &optval, &optlen), SRT_SUCCESS);
    EXPECT_EQ(optval, latency_dflt);
    EXPECT_EQ(srt_getsockopt(m_caller_sock, 0, SRTO_PEERLATENCY, &optval, &optlen), SRT_SUCCESS);
    EXPECT_EQ(optval, latency_a);

    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_RCVLATENCY, &optval, &optlen), SRT_SUCCESS);
    EXPECT_EQ(optval, latency_a);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_PEERLATENCY, &optval, &optlen), SRT_SUCCESS);
    EXPECT_EQ(optval, latency_dflt);

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

/// A regression test for issue #735, fixed by PR #843.
/// Checks propagation of listener's socket option SRTO_LOSSMAXTTL
/// on SRT sockets being accepted.
TEST_F(TestSocketOptions, LossMaxTTL)
{
    const int loss_max_ttl = 5;
    ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_LOSSMAXTTL, &loss_max_ttl, sizeof loss_max_ttl), SRT_SUCCESS);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    int opt_val = 0;
    int opt_len = 0;
    ASSERT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_LOSSMAXTTL, &opt_val, &opt_len), SRT_SUCCESS);
    EXPECT_EQ(opt_val, loss_max_ttl) << "Wrong SRTO_LOSSMAXTTL value on the accepted socket";
    EXPECT_EQ(size_t(opt_len), sizeof opt_len) << "Wrong SRTO_LOSSMAXTTL value length on the accepted socket";

    SRT_TRACEBSTATS stats;
    EXPECT_EQ(srt_bstats(accepted_sock, &stats, 0), SRT_SUCCESS);
    EXPECT_EQ(stats.pktReorderTolerance, loss_max_ttl);

    ASSERT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_LOSSMAXTTL, &opt_val, &opt_len), SRT_SUCCESS);
    EXPECT_EQ(opt_val, loss_max_ttl) << "Wrong SRTO_LOSSMAXTTL value on the listener socket";
    EXPECT_EQ(size_t(opt_len), sizeof opt_len) << "Wrong SRTO_LOSSMAXTTL value length on the listener socket";

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}


// Try to set/get SRTO_MININPUTBW with wrong optlen
TEST_F(TestSocketOptions, MinInputBWWrongLen)
{
    int64_t mininputbw = 0;
    int optlen = (int)(sizeof mininputbw) - 1;
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &mininputbw, &optlen), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(NULL), SRT_EINVPARAM);
    optlen += 2;
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &mininputbw, &optlen), SRT_SUCCESS) << "Bigger storage is allowed";
    EXPECT_EQ(optlen, (int)(sizeof mininputbw));

    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &mininputbw, sizeof mininputbw - 1), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(NULL), SRT_EINVPARAM);
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &mininputbw, sizeof mininputbw + 1), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(NULL), SRT_EINVPARAM);
}

// Check the default SRTO_MININPUTBW is SRT_PACING_MAXBW_DEFAULT
TEST_F(TestSocketOptions, MinInputBWDefault)
{
    const int mininputbw_expected = 0;
    int64_t mininputbw = 1;
    int optlen = (int)(sizeof mininputbw);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &mininputbw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(optlen, (int)(sizeof mininputbw));
    EXPECT_EQ(mininputbw, mininputbw_expected);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check both listener and accepted socket have default values
    for (SRTSOCKET sock : { m_listen_sock, accepted_sock })
    {
        optlen = (int)(sizeof mininputbw);
        EXPECT_EQ(srt_getsockopt(sock, 0, SRTO_MININPUTBW, &mininputbw, &optlen), SRT_SUCCESS);
        EXPECT_EQ(optlen, (int)(sizeof mininputbw));
        EXPECT_EQ(mininputbw, mininputbw_expected);
    }

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

// Check setting and getting SRT_MININPUTBW
TEST_F(TestSocketOptions, MinInputBWSet)
{
    const int64_t mininputbw_dflt = 0;
    const int64_t mininputbw = 50000000;
    auto optlen = (int)(sizeof mininputbw);

    int64_t bw = -100;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &bw, sizeof bw), SRT_ERROR) << "Has to be a non-negative number";
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, mininputbw_dflt);

    bw = mininputbw;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &bw, sizeof bw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, mininputbw);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    for (SRTSOCKET sock : { m_listen_sock, accepted_sock })
    {
        optlen = (int)(sizeof bw);
        EXPECT_EQ(srt_getsockopt(sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
        EXPECT_EQ(optlen, (int)(sizeof bw));
        EXPECT_EQ(bw, mininputbw);
    }

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

// Check setting and getting SRTO_MININPUTBW in runtime
TEST_F(TestSocketOptions, MinInputBWRuntime)
{
    const int64_t mininputbw = 50000000;

    // Establish a connection
    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Test a connected socket
    int64_t bw = mininputbw;
    int optlen = (int)(sizeof bw);
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_MININPUTBW, &bw, sizeof bw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, mininputbw);

    bw = 0;
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_INPUTBW, &bw, sizeof bw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_INPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, 0);

    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_MAXBW, &bw, sizeof bw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_MAXBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, 0);

    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, mininputbw);

    const int64_t new_mininputbw = 20000000;
    bw = new_mininputbw;
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_MININPUTBW, &bw, sizeof bw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_MININPUTBW, &bw, &optlen), SRT_SUCCESS);
    EXPECT_EQ(bw, new_mininputbw);

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

TEST_F(TestSocketOptions, StreamIDWrongLen)
{
    std::array<char, CSrtConfig::MAX_SID_LENGTH + 135> buffer;
    for (size_t i = 0; i < buffer.size(); ++i)
        buffer[i] = 'a' + i % 25;

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_STREAMID, buffer.data(), CSrtConfig::MAX_SID_LENGTH + 1), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM);
}

// Check if setting -1 as optlen returns an error 
TEST_F(TestSocketOptions, StringOptLenInvalid)
{
    const string test_string = "test1234567";
    const string srto_congestion_string ="live";
    const string fec_config = "fec,cols:10,rows:10";

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_STREAMID, test_string.c_str(), -1), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM);

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_BINDTODEVICE, test_string.c_str(), -1), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM);

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_CONGESTION, srto_congestion_string.c_str(), -1), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM);

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_PACKETFILTER, fec_config.c_str(), -1), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM);

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_PASSPHRASE, test_string.c_str(), -1), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM);
}

// Try to set/get a 13-character string in SRTO_STREAMID.
// This tests checks that the StreamID is set to the correct size
// while it is transmitted as 16 characters in the Stream ID HS extension.
TEST_F(TestSocketOptions, StreamIDOdd)
{
    // 13 characters, that is, 3*4+1
    string sid_odd = "something1234";

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_STREAMID, sid_odd.c_str(), (int)sid_odd.size()), SRT_SUCCESS);

    std::array<char, CSrtConfig::MAX_SID_LENGTH + 135> buffer;
    auto buffer_len = (int) buffer.size();
    EXPECT_EQ(srt_getsockopt(m_caller_sock, 0, SRTO_STREAMID, buffer.data(), &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(std::string(buffer.data()), sid_odd);
    EXPECT_EQ(size_t(buffer_len), sid_odd.size());
    EXPECT_EQ(strlen(buffer.data()), sid_odd.size());

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    fill(buffer.begin(), buffer.end(), 'a');
    buffer_len = (int) buffer.size();
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(size_t(buffer_len), sid_odd.size());
    EXPECT_EQ(strlen(buffer.data()), sid_odd.size());

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}


TEST_F(TestSocketOptions, StreamIDEven)
{
    // 12 characters = 4*3, that is, aligned to 4
    string sid_even = "123412341234";

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_STREAMID, sid_even.c_str(), (int)sid_even.size()), SRT_SUCCESS);

    array<char, CSrtConfig::MAX_SID_LENGTH + 135> buffer;
    auto buffer_len = (int) buffer.size();
    EXPECT_EQ(srt_getsockopt(m_caller_sock, 0, SRTO_STREAMID, buffer.data(), &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(std::string(buffer.data()), sid_even);
    EXPECT_EQ(size_t(buffer_len), sid_even.size());
    EXPECT_EQ(strlen(buffer.data()), sid_even.size());

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    fill(buffer.begin(), buffer.end(), 'a');
    buffer_len = (int) buffer.size();
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(size_t(buffer_len), sid_even.size());
    EXPECT_EQ(strlen(buffer.data()), sid_even.size());

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

// Test handling of StreamID with length close to the maximum allowed.
// Also tests the proper handling of a null character in the middle of the StreamID.
TEST_F(TestSocketOptions, StreamIDAlmostFull)
{
    // 12 characters = 4*3, that is, aligned to 4
    array<char, CSrtConfig::MAX_SID_LENGTH - 2> sid_almost_full;
    const size_t size = sid_almost_full.size();
    // Just to manipulate the last ones.
    sid_almost_full.fill('x');
    sid_almost_full[size-2] = '\0';
    sid_almost_full[size-1] = 'z';

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_STREAMID, sid_almost_full.data(), (int)size), SRT_SUCCESS);

    std::array<char, CSrtConfig::MAX_SID_LENGTH + 135> buffer;
    auto buffer_len = (int) buffer.size();
    EXPECT_EQ(srt_getsockopt(m_caller_sock, 0, SRTO_STREAMID, buffer.data(), &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(size_t(buffer_len), sid_almost_full.size());
    EXPECT_EQ(std::memcmp(buffer.data(), sid_almost_full.data(), buffer_len), 0);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    buffer_len = (int) buffer.size();
    fill(buffer.begin(), buffer.end(), 'a');
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_STREAMID, &buffer, &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(size_t(buffer_len), sid_almost_full.size());
    EXPECT_EQ(std::memcmp(buffer.data(), sid_almost_full.data(), buffer_len), 0);
    EXPECT_EQ(buffer[sid_almost_full.size() - 2], '\0');
    EXPECT_EQ(buffer[sid_almost_full.size() - 1], 'z');

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

TEST_F(TestSocketOptions, StreamIDFull)
{
    // 12 characters = 4*3, that is, aligned to 4
    array<char, CSrtConfig::MAX_SID_LENGTH> sid_full;
    sid_full.fill('x');

    // Just to manipulate the last ones.
    size_t size = sid_full.size();
    sid_full[size-2] = '\0';
    sid_full[size-1] = 'z';

    EXPECT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_STREAMID, sid_full.data(), (int)sid_full.size()), SRT_SUCCESS);

    array<char, CSrtConfig::MAX_SID_LENGTH + 135> buffer;
    auto buffer_len = (int) buffer.size();
    EXPECT_EQ(srt_getsockopt(m_caller_sock, 0, SRTO_STREAMID, buffer.data(), &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(memcmp(buffer.data(), sid_full.data(), sid_full.size()), 0);
    EXPECT_EQ(size_t(buffer_len), sid_full.size());

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    fill(buffer.begin(), buffer.end(), 'a');
    buffer_len = (int) buffer.size();
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_STREAMID, buffer.data(), &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(size_t(buffer_len), sid_full.size());
    EXPECT_EQ(std::memcmp(buffer.data(), sid_full.data(), buffer_len), 0);

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

// Check that StreamID assigned to a listener socket is not inherited by accepted sockets,
// and is not derived by a caller socket.
TEST_F(TestSocketOptions, StreamIDLenListener)
{
    string stream_id_13 = "something1234";

    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_STREAMID, stream_id_13.c_str(), (int)stream_id_13.size()), SRT_SUCCESS);

    array<char, 648> buffer;
    auto buffer_len = (int) buffer.size();
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_STREAMID, buffer.data(), &buffer_len), SRT_SUCCESS);
    EXPECT_EQ(string(buffer.data()), stream_id_13);
    EXPECT_EQ(size_t(buffer_len), stream_id_13.size());

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted and caller sockets do not inherit StreamID.
    for (SRTSOCKET sock : { m_caller_sock, accepted_sock })
    {
        buffer_len = (int) buffer.size();
        fill_n(buffer.data(), buffer_len, 'a');
        EXPECT_EQ(srt_getsockopt(sock, 0, SRTO_STREAMID, buffer.data(), &buffer_len), SRT_SUCCESS);
        EXPECT_EQ(buffer_len, 0) << (sock == accepted_sock ? "ACCEPTED" : "CALLER");
    }

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}
