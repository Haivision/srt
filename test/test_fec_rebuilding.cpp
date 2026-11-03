#include <vector>
#include <algorithm>
#include <future>
#include <thread>

#include "gtest/gtest.h"
#include "test_env.h"
#include "packet.h"
#include "fec.h"
#include "core.h"
#include "packetfilter.h"
#include "packetfilter_api.h"

// For direct imp access
#include "api.h"
#include "haicrypt.h"

using namespace std;
using namespace srt;

class TestFECRebuilding: public srt::Test
{
protected:
    FECFilterBuiltin* fec = nullptr;
    vector<SrtPacket> provided;
    vector<unique_ptr<CPacket>> source;
    int sockid = 54321;
    int isn = 123456;
    size_t plsize = 1316;

    TestFECRebuilding()
    {
        // Required to make ParseCorrectorConfig work
    }

    void setup() override
    {
        int timestamp = 10;

        SrtFilterInitializer init = {
            sockid,
            isn - 1, // It's passed in this form to PacketFilter constructor, it should increase it
            isn - 1, // XXX Probably this better be changed.
            plsize,
            CSrtConfig::DEF_BUFFER_SIZE
        };


        // Make configuration row-only with size 7
        string conf = "fec,rows:1,cols:7";

        provided.clear();

        fec = new FECFilterBuiltin(init, provided, conf);

        int32_t seq = isn;

        for (int i = 0; i < 7; ++i)
        {
            source.emplace_back(new CPacket);
            CPacket& p = *source.back();

            p.allocate(SRT_LIVE_MAX_PLSIZE);

            uint32_t* hdr = p.getHeader();

            // Fill in the values
            hdr[SRT_PH_SEQNO] = seq;
            hdr[SRT_PH_MSGNO] = 1 | MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO);
            hdr[SRT_PH_ID] = sockid;
            hdr[SRT_PH_TIMESTAMP] = timestamp;

            // Fill in the contents.
            // Randomly chose the size

            int minsize = 732;
            int divergence = int(plsize) - minsize - 1;
            size_t length = minsize + rand() % divergence;

            p.setLength(length);
            for (size_t b = 0; b < length; ++b)
            {
                p.data()[b] = rand() % 255;
            }

            timestamp += 10;
            seq = CSeqNo::incseq(seq);
        }
    }

    void teardown() override
    {
        delete fec;
    }

};

static std::future<int> spawn_connect(SRTSOCKET s, sockaddr_in& sa, int timeout_ms = 1000)
{
    std::cout << "[M] SPAWNING srt_connect()\n";
    return std::async(std::launch::async, [s, &sa, timeout_ms]()
        {
            // Add a delay for starting connection to give a chance
            // for the main thread to establish an EID with the listener
            // BEFORE the handshake packet is received, otherwise the
            // epoll will miss the signal (a bug fixed in 1.6.0).
            std::this_thread::sleep_for(chrono::milliseconds(timeout_ms));
            std::cout << "[T] RUNNING srt_connect()\n";
            return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });
}


// The expected whole procedure of connection using FEC is
// expected to:
//
// 1. Successfully set the FEC option for correct filter type.
//    - STOP ON FAILURE: unknown filter type (the table below, case D)
// 2. Perform the connection and integrate configurations.
//    - STOP on failed integration (the table below, cases A and B)
// 3. Deliver on both sides identical configurations consisting
//    of combined configurations and completed with default values.
//    - Not possible if stopped before.
//
// Test coverage for the above cases:
//
// Success cases in all of the above: ConfigExchange, Connection, ConnectionReorder
// Failure cases:
// 1. ConfigExchangeFaux - setting unknown filter type
// 2. ConfigExchangeFaux, RejectionConflict, RejectionIncomplete, RejectionIncompleteEmpty
//
// For config exchange we have several possibilities here:
//
// - any same parameters with different values are rejected (Case A)
// - resulting configuiration should have the `cols` value set (Cases B)
//
// The configuration API rules that control correctness:
//
// 1. The first word defines an existing filter type.
// 2. Parameters are defined in whatever order.
// 3. Some parameters are optional and have default values. Others are mandatory.
// 4. A parameter provided twice remains with the last specification.
// 5. A parameter with empty value is like not provided parameter.
// 6. Only parameters handled by given filter type are allowed.
// 7. Every parameter may have limitations on the provided value:
//    a. Numeric values in appropriate range
//    b. String-enumeration with only certain values allowed
//
// Additionally there are rules for configuration integration:
//
// 8. Configuration consists of parameters provided in both sides.
// 9. Parameters lacking after integration are set to default values.
// 10. Parameters specified on both sides (including type) must be equal.
// 11. Empty configuration blindly accepts the configuration from the peer.
// 12. The final configuration must provide mandatory parameters
//
// Restrictive rules type are: 1, 6, 7, 10
//
// Case description:
// A: Conflicting values on the same parameter (rejection, rule 10 failure)
// B: Missing a mandatory parameter (rejection, rule 12 failure)
// C: Successful setting and combining parameters
//    1: rules (positive): 1, 3, 6, 7(part), 8, 9, 12
//    2: rules (positive): 1, 2, 3, 6, 7(part), 9, 10, 12
//    3,4: rules (positive): 1, 2, 3(all), 6, 7(all), 8, 10, 12
//    5: rules (positive): 1, 3, 4, 5, 6, 7, 8, 9, 12
//    6: rules (positive): 1, 3, 6, 7, 8, 11, 12
// D: Unknown filter type (failed option, rule 1)
// E: Incorrect values of the parameters (failed option, rule 7)
// F: Unknown excessive parameters (failed option, rule 6)
//
// Case |Party A                 |  Party B           | Situation           | Test coverage
//------|------------------------|--------------------|---------------------|---------------
//  A   |fec,cols:10             | fec,cols:20        | Conflict            | ConfigExchangeFaux, RejectionConflict
//  B1  |fec,rows:10             | fec,arq:never      | Missing `cols`      | RejectionIncomplete
//  B2  |fec,rows:10             |                    | Missing `cols`      | RejectionIncompleteEmpty
//  C1  |fec,cols:10,rows:10     | fec                | OK                  | ConfigExchange, Connection
//  C2  |fec,cols:10,rows:10     | fec,rows:10,cols:10| OK                  | ConnectionReorder
//  C3  |FULL 1 (see below)      | FULL 2 (see below) | OK                  | ConnectionFull1
//  C4  |FULL 3 (see below)      | FULL 4 (see below) | OK                  | ConnectionFull2
//  C5  |fec,cols:,cols:10       | fec,cols:,rows:10  | OK                  | ConnectionMess
//  C6  |fec,rows:20,cols:20     |                    | OK                  | ConnectionForced
//  D   |FEC,Cols:10             | (unimportant)      | Option rejected     | ConfigExchangeFaux
//  E1  |fec,cols:-10            | (unimportant)      | Option rejected     | ConfigExchangeFaux
//  E2  |fec,cols:10,rows:0      | (unimportant)      | Option rejected     | ConfigExchangeFaux
//  E3  |fec,cols:10,rows:-1     | (unimportant)      | Option rejected     | ConfigExchangeFaux
//  E4  |fec,cols:10,layout:x (*)| (unimportant)      | Option rejected     | ConfigExchangeFaux
//  E5  |fec,cols:10,arq:x (*)   | (unimportant)      | Option rejected     | ConfigExchangeFaux
//  F   |fec,cols:10,weight:2    | (unimportant)      | Option rejected     | ConfigExchangeFaux
//
// (*) Here is just an example of a longer string that surely is wrong for this parameter.
//
// The configurations for FULL (cases C3 and C4) are longer and use all possible
// values in different order:
// 1. fec,cols:10,rows:20,arq:never,layout:even
// 1. fec,layout:even,rows:20,cols:10,arq:never
// 1. fec,cols:10,rows:20,arq:always,layout:even
// 1. fec,layout:even,rows:20,cols:10,arq:always


bool filterConfigSame(const string& config1, const string& config2)
{
    vector<string> config1_vector;
    Split(config1, ',', back_inserter(config1_vector));
    sort(config1_vector.begin(), config1_vector.end());

    vector<string> config2_vector;
    Split(config2, ',', back_inserter(config2_vector));
    sort(config2_vector.begin(), config2_vector.end());

    return config1_vector == config2_vector;
}

TEST(TestFEC, ConfigExchange)
{
    srt::TestInit srtinit;

    CUDTSocket* s1;

    SRTSOCKET sid1 = CUDT::uglobal().newSocket(&s1);

    TestMockCUDT m1;
    m1.core = &s1->core();

    // Can't access the configuration storage without
    // accessing the private fields, so let's use the official API

    char fec_config1 [] = "fec,cols:10,rows:10";

    // Check empty configuration first
    EXPECT_EQ(srt_setsockflag(sid1, SRTO_PACKETFILTER, "", 0), -1);
    EXPECT_NE(srt_setsockflag(sid1, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);

    EXPECT_TRUE(m1.checkApplyFilterConfig("fec,cols:10,arq:never"));

    char fec_configback[200];
    int fec_configback_size = 200;
    srt_getsockflag(sid1, SRTO_PACKETFILTER, fec_configback, &fec_configback_size);

    // Order of parameters may differ, so store everything in a vector and sort it.

    string exp_config = "fec,cols:10,rows:10,arq:never,layout:staircase";

    EXPECT_TRUE(filterConfigSame(fec_configback, exp_config));
    srt_close(sid1);
}

TEST(TestFEC, ConfigExchangeFaux)
{
    srt::TestInit srtinit;
    using namespace std;


    CUDTSocket* s1;

    SRTSOCKET sid1 = CUDT::uglobal().newSocket(&s1);

    const char* fec_config_wrong [] = {
        "FEC,Cols:20", // D: unknown filter
        "fec,cols:-10", // E1: invalid value for cols
        "fec,cols:10,rows:0", // E2: invalid value for rows
        "fec,cols:10,rows:-1", // E3: invalid value for rows
        "fec,cols:10,layout:stairwars", // E4: invalid value for layout
        "fec,cols:10,arq:sometimes", // E5: invalid value for arq
        "fec,cols:10,weight:2", // F: invalid parameter name
        "fec,cols:80000,rows:70000", // oversized
        "fec,cols:10,rows:-70000" // negative oversized rows
    };

    for (auto badconfig: fec_config_wrong)
    {
        cout << "CASE: " << badconfig << endl;
        EXPECT_EQ(srt_setsockflag(sid1, SRTO_PACKETFILTER, badconfig, (int)strlen(badconfig)), -1);
    }

    TestMockCUDT m1;
    m1.core = &s1->core();

    // Can't access the configuration storage without
    // accessing the private fields, so let's use the official API

    char fec_config1 [] = "fec,cols:20,rows:10";

    EXPECT_NE(srt_setsockflag(sid1, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);

    cout << "(NOTE: expecting a failure message)\n";
    EXPECT_FALSE(m1.checkApplyFilterConfig("fec,cols:10,arq:never"));

    srt_close(sid1);
}

TEST(TestFEC, Connection)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt::TestInit srtinit;

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:10,rows:10";
    const char fec_config2 [] = "fec,cols:10,arq:never";
    const char fec_config_final [] = "fec,cols:10,rows:10,arq:never,layout:staircase";

    EXPECT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);
    EXPECT_NE(srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1), -1);

    EXPECT_NE(srt_listen(l, 1), -1);

    auto connect_res = spawn_connect(s, sa, 1);

    // Make sure that the async call to srt_connect() is already kicked.
    std::this_thread::yield();

    // Given 2s timeout for accepting as it has occasionally happened with Travis
    // that 1s might not be enough.
    SRTSOCKET la[] = { l };
    SRTSOCKET a = srt_accept_bond(la, 1, 5000);
    EXPECT_NE(a, SRT_ERROR);
    EXPECT_EQ(connect_res.get(), SRT_SUCCESS);

    // Now that the connection is established, check negotiated config

    char result_config1[200] = "";
    int result_config1_size = 200;
    char result_config2[200] = "";
    int result_config2_size = 200;

    EXPECT_NE(srt_getsockflag(s, SRTO_PACKETFILTER, result_config1, &result_config1_size), -1);
    EXPECT_NE(srt_getsockflag(a, SRTO_PACKETFILTER, result_config2, &result_config2_size), -1);

    string caller_config = result_config1;
    string accept_config = result_config2;
    EXPECT_EQ(caller_config, accept_config);

    EXPECT_TRUE(filterConfigSame(caller_config, fec_config_final));
    EXPECT_TRUE(filterConfigSame(accept_config, fec_config_final));

    // Exceptionally blocked here to test "forgotten socket cleanup" additionally
    //srt_close(a);
    //srt_close(s);
    //srt_close(l);
}

TEST(TestFEC, ConnectionReorder)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt::TestInit srtinit;

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:10,rows:10";
    const char fec_config2 [] = "fec,rows:10,cols:10";
    const char fec_config_final [] = "fec,cols:10,rows:10,arq:onreq,layout:staircase";

    EXPECT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);
    EXPECT_NE(srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1), -1);

    int conntimeo = 10000;
    EXPECT_NE(srt_setsockflag(s, SRTO_CONNTIMEO, &conntimeo, sizeof (conntimeo)), SRT_ERROR);

    srt_listen(l, 1);

    auto connect_res = spawn_connect(s, sa);

    // Make sure that the async call to srt_connect() is already kicked.
    std::this_thread::yield();

    SRTSOCKET la[] = { l };
    SRTSOCKET a = srt_accept_bond(la, 1, 5000);
    EXPECT_NE(a, SRT_ERROR);
    EXPECT_EQ(connect_res.get(), SRT_SUCCESS);

    // Now that the connection is established, check negotiated config

    char result_config1[200] = "";
    int result_config1_size = 200;
    char result_config2[200] = "";
    int result_config2_size = 200;

    srt_getsockflag(s, SRTO_PACKETFILTER, result_config1, &result_config1_size);
    srt_getsockflag(a, SRTO_PACKETFILTER, result_config2, &result_config2_size);

    string caller_config = result_config1;
    string accept_config = result_config2;
    EXPECT_EQ(caller_config, accept_config);

    EXPECT_TRUE(filterConfigSame(caller_config, fec_config_final));
    EXPECT_TRUE(filterConfigSame(accept_config, fec_config_final));

    srt_close(a);
    srt_close(s);
    srt_close(l);
}

TEST(TestFEC, ConnectionFull1)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt::TestInit srtinit;

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:10,rows:20,arq:never,layout:even";
    const char fec_config2 [] = "fec,layout:even,rows:20,cols:10,arq:never";
    const char fec_config_final [] = "fec,cols:10,rows:20,arq:never,layout:even";

    EXPECT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);
    EXPECT_NE(srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1), -1);

    srt_listen(l, 1);

    auto connect_res = spawn_connect(s, sa);
    // Make sure that the async call to srt_connect() is already kicked.
    std::this_thread::yield();

    SRTSOCKET la[] = { l };
    SRTSOCKET a = srt_accept_bond(la, 1, 5000);
    EXPECT_NE(a, SRT_ERROR);
    EXPECT_EQ(connect_res.get(), SRT_SUCCESS);

    // Now that the connection is established, check negotiated config

    char result_config1[200] = "";
    int result_config1_size = 200;
    char result_config2[200] = "";
    int result_config2_size = 200;

    srt_getsockflag(s, SRTO_PACKETFILTER, result_config1, &result_config1_size);
    srt_getsockflag(a, SRTO_PACKETFILTER, result_config2, &result_config2_size);

    string caller_config = result_config1;
    string accept_config = result_config2;
    EXPECT_EQ(caller_config, accept_config);

    EXPECT_TRUE(filterConfigSame(caller_config, fec_config_final));
    EXPECT_TRUE(filterConfigSame(accept_config, fec_config_final));

    srt_close(a);
    srt_close(s);
    srt_close(l);
}

TEST(TestFEC, ConnectionFull2)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt::TestInit srtinit;

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:10,rows:20,arq:always,layout:even";
    const char fec_config2 [] = "fec,layout:even,rows:20,cols:10,arq:always";
    const char fec_config_final [] = "fec,cols:10,rows:20,arq:always,layout:even";

    EXPECT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);
    EXPECT_NE(srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1), -1);

    srt_listen(l, 1);

    auto connect_res = spawn_connect(s, sa);

    // Make sure that the async call to srt_connect() is already kicked.
    std::this_thread::yield();

    SRTSOCKET la[] = { l };
    SRTSOCKET a = srt_accept_bond(la, 1, 5000);
    EXPECT_NE(a, SRT_ERROR);
    EXPECT_EQ(connect_res.get(), SRT_SUCCESS);

    // Now that the connection is established, check negotiated config

    char result_config1[200] = "";
    int result_config1_size = 200;
    char result_config2[200] = "";
    int result_config2_size = 200;

    srt_getsockflag(s, SRTO_PACKETFILTER, result_config1, &result_config1_size);
    srt_getsockflag(a, SRTO_PACKETFILTER, result_config2, &result_config2_size);

    string caller_config = result_config1;
    string accept_config = result_config2;
    EXPECT_EQ(caller_config, accept_config);

    EXPECT_TRUE(filterConfigSame(caller_config, fec_config_final));
    EXPECT_TRUE(filterConfigSame(accept_config, fec_config_final));

    srt_close(a);
    srt_close(s);
    srt_close(l);
}

TEST(TestFEC, ConnectionMess)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt::TestInit srtinit;

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:,cols:10";
    const char fec_config2 [] = "fec,cols:,rows:10";
    const char fec_config_final [] = "fec,cols:10,rows:10,arq:onreq,layout:staircase";

    EXPECT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);
    EXPECT_NE(srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1), -1);

    EXPECT_NE(srt_listen(l, 1), -1);

    auto connect_res = spawn_connect(s, sa);

    // Make sure that the async call to srt_connect() is already kicked.
    std::this_thread::yield();

    SRTSOCKET la[] = { l };
    SRTSOCKET a = srt_accept_bond(la, 1, 5000);
    EXPECT_NE(a, SRT_ERROR) << srt_getlasterror_str();
    EXPECT_EQ(connect_res.get(), SRT_SUCCESS);

    // Now that the connection is established, check negotiated config

    char result_config1[200] = "";
    int result_config1_size = 200;
    char result_config2[200] = "";
    int result_config2_size = 200;

    srt_getsockflag(s, SRTO_PACKETFILTER, result_config1, &result_config1_size);
    srt_getsockflag(a, SRTO_PACKETFILTER, result_config2, &result_config2_size);

    string caller_config = result_config1;
    string accept_config = result_config2;
    EXPECT_EQ(caller_config, accept_config);

    EXPECT_TRUE(filterConfigSame(caller_config, fec_config_final));
    EXPECT_TRUE(filterConfigSame(accept_config, fec_config_final));

    srt_close(a);
    srt_close(s);
    srt_close(l);
}

TEST(TestFEC, ConnectionForced)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt::TestInit srtinit;

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,rows:20,cols:20";
    const char fec_config_final [] = "fec,cols:20,rows:20";

    EXPECT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);

    srt_listen(l, 1);

    auto connect_res = spawn_connect(s, sa);

    // Make sure that the async call to srt_connect() is already kicked.
    std::this_thread::yield();

    SRTSOCKET la[] = { l };
    SRTSOCKET a = srt_accept_bond(la, 1, 5000);
    EXPECT_NE(a, SRT_ERROR);
    EXPECT_EQ(connect_res.get(), SRT_SUCCESS);

    // Now that the connection is established, check negotiated config

    char result_config1[200] = "";
    int result_config1_size = 200;
    char result_config2[200] = "";
    int result_config2_size = 200;

    srt_getsockflag(s, SRTO_PACKETFILTER, result_config1, &result_config1_size);
    srt_getsockflag(a, SRTO_PACKETFILTER, result_config2, &result_config2_size);

    EXPECT_TRUE(filterConfigSame(result_config1, fec_config_final));
    EXPECT_TRUE(filterConfigSame(result_config2, fec_config_final));

    srt_close(a);
    srt_close(s);
    srt_close(l);
}

TEST(TestFEC, RejectionConflict)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt::TestInit srtinit;

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:10,rows:10";
    const char fec_config2 [] = "fec,cols:20,arq:never";

    srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1);
    srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1);

    srt_listen(l, 1);

    auto connect_res = spawn_connect(s, sa);

    // Make sure that the async call to srt_connect() is already kicked.
    std::this_thread::yield();

    EXPECT_EQ(connect_res.get(), SRT_ERROR);
    EXPECT_EQ(srt_getrejectreason(s), SRT_REJ_FILTER);

    bool no = false;
    // Set non-blocking so that srt_accept can return
    // immediately with failure. Just to make sure that
    // the connection is not about to be established,
    // also on the listener side.
    srt_setsockflag(l, SRTO_RCVSYN, &no, sizeof no);
    sockaddr_in scl;
    int sclen = sizeof scl;
    EXPECT_EQ(srt_accept(l, (sockaddr*)& scl, &sclen), SRT_ERROR);

    srt_close(s);
    srt_close(l);
}

TEST(TestFEC, RejectionIncompleteEmpty)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt::TestInit srtinit;

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,rows:10";
    srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1);

    srt_listen(l, 1);

    auto connect_res = spawn_connect(s, sa);

    // Make sure that the async call to srt_connect() is already kicked.
    std::this_thread::yield();

    EXPECT_EQ(connect_res.get(), SRT_ERROR);
    EXPECT_EQ(srt_getrejectreason(s), SRT_REJ_FILTER);

    bool no = false;
    // Set non-blocking so that srt_accept can return
    // immediately with failure. Just to make sure that
    // the connection is not about to be established,
    // also on the listener side.
    srt_setsockflag(l, SRTO_RCVSYN, &no, sizeof no);
    sockaddr_in scl;
    int sclen = sizeof scl;
    EXPECT_EQ(srt_accept(l, (sockaddr*)& scl, &sclen), SRT_ERROR);

    srt_close(s);
    srt_close(l);
}


TEST(TestFEC, RejectionIncomplete)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt::TestInit srtinit;

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,rows:10";
    const char fec_config2 [] = "fec,arq:never";

    srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1);
    srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1);

    srt_listen(l, 1);

    auto connect_res = spawn_connect(s, sa);

    // Make sure that the async call to srt_connect() is already kicked.
    std::this_thread::yield();

    EXPECT_EQ(connect_res.get(), SRT_ERROR);
    EXPECT_EQ(srt_getrejectreason(s), SRT_REJ_FILTER);

    bool no = false;
    // Set non-blocking so that srt_accept can return
    // immediately with failure. Just to make sure that
    // the connection is not about to be established,
    // also on the listener side.
    srt_setsockflag(l, SRTO_RCVSYN, &no, sizeof no);
    sockaddr_in scl;
    int sclen = sizeof scl;
    EXPECT_EQ(srt_accept(l, (sockaddr*)& scl, &sclen), SRT_ERROR);

    srt_close(s);
    srt_close(l);
}

TEST_F(TestFECRebuilding, Prepare)
{
    // Stuff in prepared packets into the source fec.
    int32_t seq;
    for (int i = 0; i < 7; ++i)
    {
        CPacket& p = *source[i].get();

        // Feed it simultaneously into the sender FEC
        fec->feedSource(p);
        seq = p.getSeqNo();
    }

    SrtPacket fec_ctl(SRT_LIVE_MAX_PLSIZE);

    // Use the sequence number of the last packet, as usual.
    bool have_fec_ctl = fec->packControlPacket(fec_ctl, seq);

    EXPECT_EQ(have_fec_ctl, true);
}

TEST_F(TestFECRebuilding, NoRebuild)
{
    // Stuff in prepared packets into the source fec.
    int32_t seq;
    for (int i = 0; i < 7; ++i)
    {
        CPacket& p = *source[i].get();

        // Feed it simultaneously into the sender FEC
        fec->feedSource(p);
        seq = p.getSeqNo();
    }

    SrtPacket fec_ctl(SRT_LIVE_MAX_PLSIZE);

    // Use the sequence number of the last packet, as usual.
    const bool have_fec_ctl = fec->packControlPacket(fec_ctl, seq);

    ASSERT_EQ(have_fec_ctl, true);
    // By having all packets and FEC CTL packet, now stuff in
    // these packets into the receiver

    FECFilterBuiltin::loss_seqs_t loss; // required as return, ignore

    for (int i = 0; i < 7; ++i)
    {
        // SKIP packet 4 to simulate loss
        if (i == 4 || i == 6)
            continue;

        // Stuff in the packet into the FEC filter
        bool want_passthru = fec->receive(*source[i], loss);
        EXPECT_EQ(want_passthru, true);
    }

    // Prepare a real packet basing on the SrtPacket.

    // XXX Consider packing this into a callable function as this
    // is a code directly copied from PacketFilter::packControlPacket.

    unique_ptr<CPacket> fecpkt ( new CPacket );

    uint32_t* chdr = fecpkt->getHeader();
    memcpy(chdr, fec_ctl.hdr, SRT_PH_E_SIZE * sizeof(*chdr));

    // The buffer can be assigned.
    fecpkt->m_pcData = fec_ctl.buffer;
    fecpkt->setLength(fec_ctl.length);

    // This sets only the Packet Boundary flags, while all other things:
    // - Order
    // - Rexmit
    // - Crypto
    // - Message Number
    // will be set to 0/false
    fecpkt->set_msgflags(MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO));

    // ... and then fix only the Crypto flags
    fecpkt->setMsgCryptoFlags(EncryptionKeySpec(0));

    // And now receive the FEC control packet

    bool want_passthru_fec = fec->receive(*fecpkt, loss);
    EXPECT_EQ(want_passthru_fec, false); // Confirm that it's been eaten up
    EXPECT_EQ(provided.size(), 0U); // Confirm that nothing was rebuilt

    /*
    // XXX With such a short sequence, losses will not be reported.
    // You need at least one packet past the row, even in 1-row config.
    // Probably a better way for loss collection should be devised.

    ASSERT_EQ(loss.size(), 2);
    EXPECT_EQ(loss[0].first, isn + 4);
    EXPECT_EQ(loss[1].first, isn + 6);
     */
}

TEST_F(TestFECRebuilding, Rebuild)
{
    // Stuff in prepared packets into the source fec->
    int32_t seq;
    for (int i = 0; i < 7; ++i)
    {
        CPacket& p = *source[i].get();

        // Feed it simultaneously into the sender FEC
        fec->feedSource(p);
        seq = p.getSeqNo();
    }

    SrtPacket fec_ctl(SRT_LIVE_MAX_PLSIZE);

    // Use the sequence number of the last packet, as usual.
    const bool have_fec_ctl = fec->packControlPacket(fec_ctl, seq);

    ASSERT_EQ(have_fec_ctl, true);
    // By having all packets and FEC CTL packet, now stuff in
    // these packets into the receiver

    FECFilterBuiltin::loss_seqs_t loss; // required as return, ignore

    for (int i = 0; i < 7; ++i)
    {
        // SKIP packet 4 to simulate loss
        if (i == 4)
            continue;

        // Stuff in the packet into the FEC filter
        bool want_passthru = fec->receive(*source[i], loss);
        EXPECT_EQ(want_passthru, true);
    }

    // Prepare a real packet basing on the SrtPacket.

    // XXX Consider packing this into a callable function as this
    // is a code directly copied from PacketFilter::packControlPacket.

    unique_ptr<CPacket> fecpkt ( new CPacket );

    uint32_t* chdr = fecpkt->getHeader();
    memcpy(chdr, fec_ctl.hdr, SRT_PH_E_SIZE * sizeof(*chdr));

    // The buffer can be assigned.
    fecpkt->m_pcData = fec_ctl.buffer;
    fecpkt->setLength(fec_ctl.length);

    // This sets only the Packet Boundary flags, while all other things:
    // - Order
    // - Rexmit
    // - Crypto
    // - Message Number
    // will be set to 0/false
    fecpkt->set_msgflags(MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO));

    // ... and then fix only the Crypto flags
    fecpkt->setMsgCryptoFlags(EncryptionKeySpec(0));

    // And now receive the FEC control packet

    const bool want_passthru_fec = fec->receive(*fecpkt, loss);
    EXPECT_EQ(want_passthru_fec, false); // Confirm that it's been eaten up

    EXPECT_EQ(loss.size(), 0U);
    ASSERT_EQ(provided.size(), 1U);

    SrtPacket& rebuilt = provided[0];
    CPacket& skipped = *source[4];

    // Set artificially the SN_REXMIT flag in the skipped source packet
    // because the rebuilt packet shall have REXMIT flag set.
    skipped.set_msgflags(skipped.msgflags() | MSGNO_REXMIT::wrap(true));

    // Compare the header
    EXPECT_EQ(skipped.getHeader()[SRT_PH_SEQNO], rebuilt.hdr[SRT_PH_SEQNO]);
    EXPECT_EQ(skipped.getHeader()[SRT_PH_MSGNO], rebuilt.hdr[SRT_PH_MSGNO]);
    EXPECT_EQ(skipped.getHeader()[SRT_PH_ID], rebuilt.hdr[SRT_PH_ID]);
    EXPECT_EQ(skipped.getHeader()[SRT_PH_TIMESTAMP], rebuilt.hdr[SRT_PH_TIMESTAMP]);

    // Compare sizes and contents
    ASSERT_EQ(skipped.size(), rebuilt.size());

    EXPECT_EQ(memcmp(skipped.data(), rebuilt.data(), rebuilt.size()), 0);
}

// The FEC control packet carries the FEC header (4 bytes) followed by the
// payload clip, which also covers the AES-GCM authentication tag. All of it
// must fit in SRT_LIVE_MAX_PLSIZE (#3391).
TEST(TestFECRebuildingLimits, RejectsOversizedPayload)
{
    srt::TestInit srtinit;

    vector<SrtPacket> provided;
    const string      conf = "fec,rows:1,cols:7";

    const size_t max_clip = SRT_LIVE_MAX_PLSIZE - FECFilterBuiltin::EXTRA_SIZE;

    SrtFilterInitializer ok_init = {54321, 123455, 123455, max_clip, CSrtConfig::DEF_BUFFER_SIZE};
    EXPECT_NO_THROW(delete new FECFilterBuiltin(ok_init, provided, conf));

    // 1452 + 16 (AES-GCM tag) is the overflowing configuration from #3391.
    const size_t bad_sizes[] = {max_clip + 1, max_clip + 16, SRT_LIVE_MAX_PLSIZE};
    for (size_t i = 0; i < sizeof(bad_sizes) / sizeof(bad_sizes[0]); ++i)
    {
        SrtFilterInitializer bad_init = {54321, 123455, 123455, bad_sizes[i], CSrtConfig::DEF_BUFFER_SIZE};
        EXPECT_THROW(delete new FECFilterBuiltin(bad_init, provided, conf), CUDTException) << bad_sizes[i];
    }
}

class TestFECRebuildingMaxPayload : public TestFECRebuilding
{
protected:
    TestFECRebuildingMaxPayload() { plsize = SRT_LIVE_MAX_PLSIZE - FECFilterBuiltin::EXTRA_SIZE; }
};

TEST_F(TestFECRebuildingMaxPayload, RebuildFullSizePacket)
{
    // Make the lost packet occupy the whole allowed payload.
    source[4]->setLength(plsize);
    for (size_t b = 0; b < plsize; ++b)
        source[4]->data()[b] = char(b * 7 + 3);

    int32_t seq = 0;
    for (int i = 0; i < 7; ++i)
    {
        fec->feedSource(*source[i]);
        seq = source[i]->getSeqNo();
    }

    SrtPacket fec_ctl(SRT_LIVE_MAX_PLSIZE);
    ASSERT_TRUE(fec->packControlPacket(fec_ctl, seq));
    EXPECT_EQ(fec_ctl.length, size_t(SRT_LIVE_MAX_PLSIZE));

    FECFilterBuiltin::loss_seqs_t loss;
    for (int i = 0; i < 7; ++i)
    {
        if (i == 4)
            continue;
        EXPECT_TRUE(fec->receive(*source[i], loss));
    }

    unique_ptr<CPacket> fecpkt(new CPacket);
    memcpy(fecpkt->getHeader(), fec_ctl.hdr, SRT_PH_E_SIZE * sizeof(uint32_t));
    fecpkt->m_pcData = fec_ctl.buffer;
    fecpkt->setLength(fec_ctl.length);
    fecpkt->set_msgflags(MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO));
    fecpkt->setMsgCryptoFlags(EncryptionKeySpec(0));

    EXPECT_FALSE(fec->receive(*fecpkt, loss));

    EXPECT_EQ(loss.size(), 0U);
    ASSERT_EQ(provided.size(), 1U);

    SrtPacket& rebuilt = provided[0];
    ASSERT_EQ(rebuilt.size(), plsize);
    EXPECT_EQ(memcmp(source[4]->data(), rebuilt.data(), plsize), 0);
}

#if defined(ENABLE_AEAD_API_PREVIEW) && defined(SRT_ENABLE_ENCRYPTION)

static int getPayloadSize(SRTSOCKET s)
{
    int val = -1;
    int len = sizeof val;
    if (srt_getsockflag(s, SRTO_PAYLOADSIZE, &val, &len) == SRT_ERROR)
        return -1;
    return val;
}

// FEC header (4 bytes) and AES-GCM tag (16 bytes) leave 1436 bytes of user payload.
static const int FEC_GCM_MAX_PAYLOAD = SRT_LIVE_MAX_PLSIZE - 4 - 16;

TEST(TestFECGCM, PayloadSizeOptionOrder)
{
    srt::TestInit srtinit;
    if (!HaiCrypt_IsAESGCM_Supported())
        GTEST_SKIP() << "AES-GCM is not supported by the crypto library";

    const char fec_config[] = "fec,cols:10,rows:10";
    const int  gcm          = 2;
    const int  large        = SRT_LIVE_MAX_PLSIZE;

    // PAYLOADSIZE -> PACKETFILTER -> CRYPTOMODE
    {
        SRTSOCKET s = srt_create_socket();
        ASSERT_NE(srt_setsockflag(s, SRTO_PAYLOADSIZE, &large, sizeof large), SRT_ERROR);
        ASSERT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config, sizeof fec_config - 1), SRT_ERROR);
        EXPECT_EQ(getPayloadSize(s), SRT_LIVE_MAX_PLSIZE - 4);
        ASSERT_NE(srt_setsockflag(s, SRTO_CRYPTOMODE, &gcm, sizeof gcm), SRT_ERROR);
        EXPECT_EQ(getPayloadSize(s), FEC_GCM_MAX_PAYLOAD);
        srt_close(s);
    }

    // PAYLOADSIZE -> CRYPTOMODE -> PACKETFILTER
    {
        SRTSOCKET s = srt_create_socket();
        ASSERT_NE(srt_setsockflag(s, SRTO_PAYLOADSIZE, &large, sizeof large), SRT_ERROR);
        ASSERT_NE(srt_setsockflag(s, SRTO_CRYPTOMODE, &gcm, sizeof gcm), SRT_ERROR);
        EXPECT_EQ(getPayloadSize(s), SRT_LIVE_MAX_PLSIZE - 16);
        ASSERT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config, sizeof fec_config - 1), SRT_ERROR);
        EXPECT_EQ(getPayloadSize(s), FEC_GCM_MAX_PAYLOAD);
        srt_close(s);
    }

    // CRYPTOMODE -> PACKETFILTER -> PAYLOADSIZE
    {
        SRTSOCKET s = srt_create_socket();
        ASSERT_NE(srt_setsockflag(s, SRTO_CRYPTOMODE, &gcm, sizeof gcm), SRT_ERROR);
        ASSERT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config, sizeof fec_config - 1), SRT_ERROR);
        EXPECT_EQ(getPayloadSize(s), SRT_LIVE_DEF_PLSIZE);

        const int too_large = FEC_GCM_MAX_PAYLOAD + 1;
        EXPECT_EQ(srt_setsockflag(s, SRTO_PAYLOADSIZE, &too_large, sizeof too_large), SRT_ERROR);
        const int max = FEC_GCM_MAX_PAYLOAD;
        EXPECT_NE(srt_setsockflag(s, SRTO_PAYLOADSIZE, &max, sizeof max), SRT_ERROR);
        EXPECT_EQ(getPayloadSize(s), FEC_GCM_MAX_PAYLOAD);
        srt_close(s);
    }
}

// The listener uses AUTO crypto mode and no packet filter, so both AES-GCM
// and FEC are imposed by the caller during the handshake. The accepted socket
// must reduce its payload size accordingly.
TEST(TestFECGCM, ConnectionReducesPayloadSize)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt::TestInit srtinit;
    if (!HaiCrypt_IsAESGCM_Supported())
        GTEST_SKIP() << "AES-GCM is not supported by the crypto library";

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    const char passphrase[] = "fec-gcm-passphrase";
    const char fec_config[] = "fec,cols:4,rows:1";
    const int  gcm          = 2;
    const int  large        = SRT_LIVE_MAX_PLSIZE;

    ASSERT_NE(srt_setsockflag(l, SRTO_PAYLOADSIZE, &large, sizeof large), SRT_ERROR);
    ASSERT_NE(srt_setsockflag(l, SRTO_PASSPHRASE, passphrase, sizeof passphrase - 1), SRT_ERROR);

    ASSERT_NE(srt_setsockflag(s, SRTO_PASSPHRASE, passphrase, sizeof passphrase - 1), SRT_ERROR);
    ASSERT_NE(srt_setsockflag(s, SRTO_CRYPTOMODE, &gcm, sizeof gcm), SRT_ERROR);
    ASSERT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config, sizeof fec_config - 1), SRT_ERROR);
    const int caller_payload = FEC_GCM_MAX_PAYLOAD;
    ASSERT_NE(srt_setsockflag(s, SRTO_PAYLOADSIZE, &caller_payload, sizeof caller_payload), SRT_ERROR);

    ASSERT_NE(srt_bind(l, (sockaddr*)&sa, sizeof sa), SRT_ERROR);
    ASSERT_NE(srt_listen(l, 1), SRT_ERROR);

    auto connect_res = spawn_connect(s, sa);

    SRTSOCKET la[] = {l};
    SRTSOCKET a    = srt_accept_bond(la, 1, 5000);
    ASSERT_NE(a, SRT_ERROR);
    ASSERT_EQ(connect_res.get(), SRT_SUCCESS);

    int mode = 0;
    int len  = sizeof mode;
    EXPECT_NE(srt_getsockflag(a, SRTO_CRYPTOMODE, &mode, &len), SRT_ERROR);
    EXPECT_EQ(mode, gcm);
    EXPECT_EQ(getPayloadSize(a), FEC_GCM_MAX_PAYLOAD);

    // A message larger than the reduced payload size must be rejected.
    vector<char> buf(SRT_LIVE_MAX_PLSIZE, 'x');
    EXPECT_EQ(srt_sendmsg2(a, &buf[0], FEC_GCM_MAX_PAYLOAD + 1, NULL), SRT_ERROR);

    // A maximum-size message must be delivered intact through FEC and AES-GCM.
    const int rcvtimeo = 3000;
    ASSERT_NE(srt_setsockflag(a, SRTO_RCVTIMEO, &rcvtimeo, sizeof rcvtimeo), SRT_ERROR);

    for (int i = 0; i < FEC_GCM_MAX_PAYLOAD; ++i)
        buf[i] = char(i * 13 + 1);
    EXPECT_EQ(srt_sendmsg2(s, &buf[0], FEC_GCM_MAX_PAYLOAD, NULL), FEC_GCM_MAX_PAYLOAD);

    vector<char> rbuf(SRT_LIVE_MAX_PLSIZE);
    EXPECT_EQ(srt_recvmsg2(a, &rbuf[0], int(rbuf.size()), NULL), FEC_GCM_MAX_PAYLOAD);
    EXPECT_EQ(memcmp(&buf[0], &rbuf[0], FEC_GCM_MAX_PAYLOAD), 0);

    srt_close(a);
    srt_close(s);
    srt_close(l);
}

// Same as above over IPv6, whose headers are 20 bytes longer than IPv4 ones:
// FEC header and AES-GCM tag must be subtracted from 1436 bytes, not 1456.
TEST(TestFECGCM, IPv6ConnectionReducesPayloadSize)
{
    srt::TestInit srtinit;
    SRTST_REQUIRES(IPv6);
    if (!HaiCrypt_IsAESGCM_Supported())
        GTEST_SKIP() << "AES-GCM is not supported by the crypto library";

    const int ipv6_gcm_fec_max = FEC_GCM_MAX_PAYLOAD - 20;

    sockaddr_in6 sa;
    memset(&sa, 0, sizeof sa);
    sa.sin6_family = AF_INET6;
    sa.sin6_port   = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET6, "::1", &sa.sin6_addr), 1);

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    const char passphrase[] = "fec-gcm-passphrase";
    const char fec_config[] = "fec,cols:4,rows:1";
    const int  gcm          = 2;
    const int  large        = SRT_LIVE_MAX_PLSIZE;
    const int  yes          = 1;

    ASSERT_NE(srt_setsockflag(l, SRTO_PAYLOADSIZE, &large, sizeof large), SRT_ERROR);
    ASSERT_NE(srt_setsockflag(l, SRTO_PASSPHRASE, passphrase, sizeof passphrase - 1), SRT_ERROR);
    ASSERT_NE(srt_setsockflag(l, SRTO_IPV6ONLY, &yes, sizeof yes), SRT_ERROR);

    ASSERT_NE(srt_setsockflag(s, SRTO_PASSPHRASE, passphrase, sizeof passphrase - 1), SRT_ERROR);
    ASSERT_NE(srt_setsockflag(s, SRTO_CRYPTOMODE, &gcm, sizeof gcm), SRT_ERROR);
    ASSERT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config, sizeof fec_config - 1), SRT_ERROR);
    const int caller_payload = FEC_GCM_MAX_PAYLOAD;
    ASSERT_NE(srt_setsockflag(s, SRTO_PAYLOADSIZE, &caller_payload, sizeof caller_payload), SRT_ERROR);

    ASSERT_NE(srt_bind(l, (sockaddr*)&sa, sizeof sa), SRT_ERROR);
    ASSERT_NE(srt_listen(l, 1), SRT_ERROR);

    auto connect_res = std::async(std::launch::async, [s, &sa]() {
        return srt_connect(s, (sockaddr*)&sa, sizeof sa);
    });

    SRTSOCKET a = srt_accept(l, NULL, NULL);
    ASSERT_NE(a, SRT_ERROR);
    ASSERT_EQ(connect_res.get(), SRT_SUCCESS);

    EXPECT_EQ(getPayloadSize(s), ipv6_gcm_fec_max);
    EXPECT_EQ(getPayloadSize(a), ipv6_gcm_fec_max);

    vector<char> buf(SRT_LIVE_MAX_PLSIZE, 'x');
    EXPECT_EQ(srt_sendmsg2(a, &buf[0], ipv6_gcm_fec_max + 1, NULL), SRT_ERROR);

    const int rcvtimeo = 3000;
    ASSERT_NE(srt_setsockflag(a, SRTO_RCVTIMEO, &rcvtimeo, sizeof rcvtimeo), SRT_ERROR);

    for (int i = 0; i < ipv6_gcm_fec_max; ++i)
        buf[i] = char(i * 13 + 1);
    EXPECT_EQ(srt_sendmsg2(s, &buf[0], ipv6_gcm_fec_max, NULL), ipv6_gcm_fec_max);

    vector<char> rbuf(SRT_LIVE_MAX_PLSIZE);
    EXPECT_EQ(srt_recvmsg2(a, &rbuf[0], int(rbuf.size()), NULL), ipv6_gcm_fec_max);
    EXPECT_EQ(memcmp(&buf[0], &rbuf[0], ipv6_gcm_fec_max), 0);

    srt_close(a);
    srt_close(s);
    srt_close(l);
}

#endif // ENABLE_AEAD_API_PREVIEW && SRT_ENABLE_ENCRYPTION

// processCtrlAck has two OOB-read sites for intermediate payload sizes:
//  - ackdata[ACKD_RCVLASTACK] (index 0) is read up front, OOB for 0-3 byte payloads;
//  - ackdata[ACKD_BUFFERLEFT] (index 3) is read in the slow path, OOB for 5-15 byte
//    payloads (the lite-ACK fast path matches exactly 4 bytes).
// Valid payloads are LITE (4 B) or SMALL+ (>=16 B). The guard at the top of the
// handler rejects everything else.
TEST(TestCUDT, AckRejectsIntermediatePayload)
{
    srt::TestInit srtinit;

    CUDTSocket* s1 = NULL;
    SRTSOCKET sid1 = CUDT::uglobal().newSocket(&s1);

    TestMockCUDT m1;
    m1.core = &s1->core();

    const int sentinel = 0x5A5A5A5A;
    m1.setFlowWindowSize(sentinel);

    CPacket pkt;
    pkt.allocate(1500);

    // Fill the payload with bytes that would be plausible ack-seqnos if interpreted
    // as int32 (non-negative), so the ackdata_seqno < 0 early return doesn't mask
    // the bug for the 0-3 byte cases.
    std::memset(pkt.m_pcData, 0x01, 1500);

    const size_t bad_lens[] = { 0, 1, 3, 5, 8, 12, 15 };
    const sync::steady_clock::time_point now = sync::steady_clock::now();
    for (size_t i = 0; i < sizeof(bad_lens) / sizeof(bad_lens[0]); ++i)
    {
        pkt.setLength(bad_lens[i]);
        m1.processCtrlAck(pkt, now);
        EXPECT_EQ(m1.flowWindowSize(), sentinel)
            << "ACK with payload " << bad_lens[i] << " bytes must not corrupt m_iFlowWindowSize";
    }

    srt_close(sid1);
}
