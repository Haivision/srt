#include <vector>
#include <algorithm>
#include <future>

#include "gtest/gtest.h"
#include "packet.h"
#include "fec.h"
#include "core.h"
#include "packetfilter.h"
#include "packetfilter_api.h"

// For direct imp access
#include "api.h"

using namespace std;
using namespace srt;

class TestFECRebuilding: public testing::Test
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
        PacketFilter::globalInit();
    }

    void SetUp() override
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
            int divergence = plsize - minsize - 1;
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

    void TearDown() override
    {
        delete fec;
    }
};

namespace srt {
    class TestMockCUDT
    {
    public:
        CUDT* core;

        bool checkApplyFilterConfig(const string& s)
        {
            return core->checkApplyFilterConfig(s);
        }
    };
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
    srt_startup();

    CUDTSocket* s1;

    SRTSOCKET sid1 = CUDT::uglobal().newSocket(&s1);

    TestMockCUDT m1;
    m1.core = &s1->core();

    // Can't access the configuration storage without
    // accessing the private fields, so let's use the official API

    char fec_config1 [] = "fec,cols:10,rows:10";

    srt_setsockflag(sid1, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1);

    EXPECT_TRUE(m1.checkApplyFilterConfig("fec,cols:10,arq:never"));

    char fec_configback[200];
    int fec_configback_size = 200;
    srt_getsockflag(sid1, SRTO_PACKETFILTER, fec_configback, &fec_configback_size);

    // Order of parameters may differ, so store everything in a vector and sort it.

    string exp_config = "fec,cols:10,rows:10,arq:never,layout:staircase";

    EXPECT_TRUE(filterConfigSame(fec_configback, exp_config));
    srt_cleanup();
}

TEST(TestFEC, ConfigExchangeFaux)
{
    srt_startup();

    CUDTSocket* s1;

    SRTSOCKET sid1 = CUDT::uglobal().newSocket(&s1);

    const char* fec_config_wrong [] = {
        "FEC,Cols:20", // D: unknown filter
        "fec,cols:-10", // E1: invalid value for cols
        "fec,cols:10,rows:0", // E2: invalid value for rows
        "fec,cols:10,rows:-1", // E3: invalid value for rows
        "fec,cols:10,layout:stairwars", // E4: invalid value for layout
        "fec,cols:10,arq:sometimes", // E5: invalid value for arq
        "fec,cols:10,weight:2" // F: invalid parameter name
    };

    for (auto badconfig: fec_config_wrong)
    {
        ASSERT_EQ(srt_setsockflag(sid1, SRTO_PACKETFILTER, badconfig, strlen(badconfig)), -1);
    }

    TestMockCUDT m1;
    m1.core = &s1->core();

    // Can't access the configuration storage without
    // accessing the private fields, so let's use the official API

    char fec_config1 [] = "fec,cols:20,rows:10";

    EXPECT_NE(srt_setsockflag(sid1, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);

    cout << "(NOTE: expecting a failure message)\n";
    EXPECT_FALSE(m1.checkApplyFilterConfig("fec,cols:10,arq:never"));

    srt_cleanup();
}

TEST(TestFEC, Connection)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:10,rows:10";
    const char fec_config2 [] = "fec,cols:10,arq:never";
    const char fec_config_final [] = "fec,cols:10,rows:10,arq:never,layout:staircase";

    ASSERT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);
    ASSERT_NE(srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1), -1);

    srt_listen(l, 1);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

    SRTSOCKET la[] = { l };
    // Given 2s timeout for accepting as it has occasionally happened with Travis
    // that 1s might not be enough.
    SRTSOCKET a = srt_accept_bond(la, 1, 2000);
    ASSERT_NE(a, SRT_ERROR);
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

    srt_cleanup();
}

TEST(TestFEC, ConnectionReorder)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:10,rows:10";
    const char fec_config2 [] = "fec,rows:10,cols:10";
    const char fec_config_final [] = "fec,cols:10,rows:10,arq:onreq,layout:staircase";

    ASSERT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);
    ASSERT_NE(srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1), -1);

    srt_listen(l, 1);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

    SRTSOCKET la[] = { l };
    SRTSOCKET a = srt_accept_bond(la, 1, 2000);
    ASSERT_NE(a, SRT_ERROR);
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

    srt_cleanup();
}

TEST(TestFEC, ConnectionFull1)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:10,rows:20,arq:never,layout:even";
    const char fec_config2 [] = "fec,layout:even,rows:20,cols:10,arq:never";
    const char fec_config_final [] = "fec,cols:10,rows:20,arq:never,layout:even";

    ASSERT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);
    ASSERT_NE(srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1), -1);

    srt_listen(l, 1);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

    SRTSOCKET la[] = { l };
    SRTSOCKET a = srt_accept_bond(la, 1, 2000);
    ASSERT_NE(a, SRT_ERROR);
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

    srt_cleanup();
}
TEST(TestFEC, ConnectionFull2)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:10,rows:20,arq:always,layout:even";
    const char fec_config2 [] = "fec,layout:even,rows:20,cols:10,arq:always";
    const char fec_config_final [] = "fec,cols:10,rows:20,arq:always,layout:even";

    ASSERT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);
    ASSERT_NE(srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1), -1);

    srt_listen(l, 1);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

    SRTSOCKET la[] = { l };
    SRTSOCKET a = srt_accept_bond(la, 1, 2000);
    ASSERT_NE(a, SRT_ERROR);
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

    srt_cleanup();
}

TEST(TestFEC, ConnectionMess)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:,cols:10";
    const char fec_config2 [] = "fec,cols:,rows:10";
    const char fec_config_final [] = "fec,cols:10,rows:10,arq:onreq,layout:staircase";

    ASSERT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);
    ASSERT_NE(srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1), -1);

    srt_listen(l, 1);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

    SRTSOCKET la[] = { l };
    SRTSOCKET a = srt_accept_bond(la, 1, 2000);
    ASSERT_NE(a, SRT_ERROR);
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

    srt_cleanup();
}

TEST(TestFEC, ConnectionForced)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,rows:20,cols:20";
    const char fec_config_final [] = "fec,cols:20,rows:20";

    ASSERT_NE(srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1), -1);

    srt_listen(l, 1);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

    SRTSOCKET la[] = { l };
    SRTSOCKET a = srt_accept_bond(la, 1, 2000);
    ASSERT_NE(a, SRT_ERROR);
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

    srt_cleanup();
}

TEST(TestFEC, RejectionConflict)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,cols:10,rows:10";
    const char fec_config2 [] = "fec,cols:20,arq:never";

    srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1);
    srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1);

    srt_listen(l, 1);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

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

    srt_cleanup();
}

TEST(TestFEC, RejectionIncompleteEmpty)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,rows:10";
    srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1);

    srt_listen(l, 1);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

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

    srt_cleanup();
}


TEST(TestFEC, RejectionIncomplete)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));

    const char fec_config1 [] = "fec,rows:10";
    const char fec_config2 [] = "fec,arq:never";

    srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, (sizeof fec_config1)-1);
    srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, (sizeof fec_config2)-1);

    srt_listen(l, 1);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

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

    srt_cleanup();
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
    fecpkt->m_iMsgNo = MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO);

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
    fecpkt->m_iMsgNo = MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO);

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
    skipped.m_iMsgNo |= MSGNO_REXMIT::wrap(true);

    // Compare the header
    EXPECT_EQ(skipped.getHeader()[SRT_PH_SEQNO], rebuilt.hdr[SRT_PH_SEQNO]);
    EXPECT_EQ(skipped.getHeader()[SRT_PH_MSGNO], rebuilt.hdr[SRT_PH_MSGNO]);
    EXPECT_EQ(skipped.getHeader()[SRT_PH_ID], rebuilt.hdr[SRT_PH_ID]);
    EXPECT_EQ(skipped.getHeader()[SRT_PH_TIMESTAMP], rebuilt.hdr[SRT_PH_TIMESTAMP]);

    // Compare sizes and contents
    ASSERT_EQ(skipped.size(), rebuilt.size());

    EXPECT_EQ(memcmp(skipped.data(), rebuilt.data(), rebuilt.size()), 0);
}
