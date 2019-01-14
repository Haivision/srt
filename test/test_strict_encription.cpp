/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 * Written by:
 *             Haivision Systems Inc.
 */

#include <gtest/gtest.h>

#include "srt.h"


enum PEER_TYPE
{
    PEER_CALLER   = 0,
    PEER_LISTENER = 1,
    PEER_COUNT    = 2,  // Number of peers
};


enum CHECK_SOCKET_TYPE
{
    CHECK_SOCKET_CALLER   = 0,
    CHECK_SOCKET_ACCEPTED = 1,
    CHECK_SOCKET_COUNT    = 2,  // Number of peers
};


enum TEST_CASE
{
    TEST_CASE_A_1 = 0,
    TEST_CASE_A_2,
    TEST_CASE_A_3,
    TEST_CASE_A_4,
    TEST_CASE_A_5,
    TEST_CASE_B_1,
    TEST_CASE_B_2,
    TEST_CASE_B_3,
    TEST_CASE_B_4,
    TEST_CASE_B_5,
    TEST_CASE_C_1,
    TEST_CASE_C_2,
    TEST_CASE_C_3,
    TEST_CASE_C_4,
    TEST_CASE_C_5,
    TEST_CASE_D_1,
    TEST_CASE_D_2,
    TEST_CASE_D_3,
    TEST_CASE_D_4,
    TEST_CASE_D_5,
};


struct TestResult
{
    int     connect_ret;
    int     accept_ret;
    int     epoll_wait_ret;
    int     epoll_wait_error;   // error code set internally by SRT
    int     rnum;               //< set by srt_epoll_wait
    int     wnum;               //< set by srt_epoll_wait
    int     socket_state[CHECK_SOCKET_COUNT];
    int     km_state    [CHECK_SOCKET_COUNT];
};


struct TestCase
{
    bool                strictenc [PEER_COUNT];
    const std::string  (&password)[PEER_COUNT];
    TestResult          expected_result;
};


static const std::string s_pwd_a ("s!t@r#i$c^t");
static const std::string s_pwd_b ("s!t@r#i$c^tu");
static const std::string s_pwd_no("");


static const int SRT_E_TIMEOUT = MJ_AGAIN * 1000 + MN_XMTIMEOUT;
static const int SRT_E_REJECT  = MJ_SETUP * 1000 + MN_RDAVAIL;


/*
 * TESTING SCENARIO
 * Both peers exchange HandShake v5.
 * Listener is sender   in a non-blocking mode
 * Caller   is receiver in a non-blocking mode
 *
 * In the cases B.2-B.4 the caller will reject the connection due to the strict encryption check
 * of the HS response from the listener on the stage of the KM response check.
 * While the listener accepts the connection with the connected state. So the caller sends UMSG_SHUTDOWN
 * to notify the listener that he has closed the connection. Both get the SRTS_BROKEN states.
 * 
 * In the cases C.2-C.4 it is the listener who rejects the connection, so we don't have an accepted socket.
 */
const TestCase g_test_matrix[] =
{
        // STRICTENC         |  Password           |                                |EPoll wait                       | socket_state                            |  KM State
        // caller | listener |  caller  | listener |  connect_ret   accept_ret      |ret | error          | rnum|wnum | caller              accepted |  caller              listener
/*A.1 */ { {true,     true  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                0,  1,  0,               0,   1,   {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*A.2 */ { {true,     true  }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK, -1,  SRT_E_TIMEOUT,  -1,  -1,   {SRTS_BROKEN,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*A.3 */ { {true,     true  }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS, SRT_INVALID_SOCK, -1,  SRT_E_TIMEOUT,  -1,  -1,   {SRTS_BROKEN,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*A.4 */ { {true,     true  }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK, -1,  SRT_E_TIMEOUT,  -1,  -1,   {SRTS_BROKEN,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*A.5 */ { {true,     true  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                0,  1,  0,               0,   1,   {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

/*B.1 */ { {true,    false  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                0,  1,  0,               0,   1,   {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*B.2 */ { {true,    false  }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS,                0, -1,  SRT_E_TIMEOUT,  -1,  -1,   {SRTS_BROKEN,       SRTS_BROKEN}, {SRT_KM_S_BADSECRET, SRT_KM_S_BADSECRET}}},
/*B.3 */ { {true,    false  }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS,                0, -1,  SRT_E_TIMEOUT,  -1,  -1,   {SRTS_BROKEN,       SRTS_BROKEN}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
/*B.4 */ { {true,    false  }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS,                0, -1,  SRT_E_TIMEOUT,  -1,  -1,   {SRTS_BROKEN,       SRTS_BROKEN}, {SRT_KM_S_UNSECURED,  SRT_KM_S_NOSECRET}}},
/*B.5 */ { {true,    false  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                0,  1,  0,               0,   1,   {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

/*C.1 */ { {false,    true  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                0,  1,  0,               0,   1,   {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*C.2 */ { {false,    true  }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK, -1,  SRT_E_TIMEOUT,  -1,  -1,   {SRTS_BROKEN,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*C.3 */ { {false,    true  }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS, SRT_INVALID_SOCK, -1,  SRT_E_TIMEOUT,  -1,  -1,   {SRTS_BROKEN,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*C.4 */ { {false,    true  }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK, -1,  SRT_E_TIMEOUT,  -1,  -1,   {SRTS_BROKEN,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*C.5 */ { {false,    true  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                0,  1,  0,               0,   1,   {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

/*D.1 */ { {false,   false  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                0,  1,  0,               0,   1,   {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*D.2 */ { {false,   false  }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS,                0,  1,  0,               0,   1,   {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_BADSECRET, SRT_KM_S_BADSECRET}}},
/*D.3 */ { {false,   false  }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS,                0,  1,  0,               0,   1,   {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
/*D.4 */ { {false,   false  }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS,                0,  1,  0,               0,   1,   {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_NOSECRET,   SRT_KM_S_NOSECRET}}},
/*D.5 */ { {false,   false  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                0,  1,  0,               0,   1,   {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
};



class TestStrictEncryption
    : public ::testing::Test
{
protected:
    TestStrictEncryption()
    {
        // initialization code here
    }

    ~TestStrictEncryption()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:

    // SetUp() is run immediately before a test starts.
    void SetUp()
    {
        ASSERT_EQ(srt_startup(), 0);

        m_pollid = srt_epoll_create();
        ASSERT_GE(m_pollid, 0);

        m_caller_socket = srt_create_socket();
        ASSERT_NE(m_caller_socket, SRT_INVALID_SOCK);

        ASSERT_NE(srt_setsockflag(m_caller_socket,    SRTO_SENDER,    &s_yes, sizeof s_yes), SRT_ERROR);
        ASSERT_NE(srt_setsockopt (m_caller_socket, 0, SRTO_RCVSYN,    &s_no,  sizeof s_no),  SRT_ERROR); // non-blocking mode
        ASSERT_NE(srt_setsockopt (m_caller_socket, 0, SRTO_SNDSYN,    &s_no,  sizeof s_no),  SRT_ERROR); // non-blocking mode
        ASSERT_NE(srt_setsockopt (m_caller_socket, 0, SRTO_TSBPDMODE, &s_yes, sizeof s_yes), SRT_ERROR);

        m_listener_socket = srt_create_socket();
        ASSERT_NE(m_listener_socket, SRT_INVALID_SOCK);

        ASSERT_NE(srt_setsockflag(m_listener_socket,    SRTO_SENDER,    &s_no,  sizeof s_no),  SRT_ERROR);
        ASSERT_NE(srt_setsockopt (m_listener_socket, 0, SRTO_RCVSYN,    &s_no,  sizeof s_no),  SRT_ERROR); // non-blocking mode
        ASSERT_NE(srt_setsockopt (m_listener_socket, 0, SRTO_SNDSYN,    &s_no,  sizeof s_no),  SRT_ERROR); // non-blocking mode
        ASSERT_NE(srt_setsockopt (m_listener_socket, 0, SRTO_TSBPDMODE, &s_yes, sizeof s_yes), SRT_ERROR);

        // Will use this epoll to wait for srt_accept(...)
        const int epoll_out = SRT_EPOLL_OUT;
        ASSERT_NE(srt_epoll_add_usock(m_pollid, m_caller_socket, &epoll_out), SRT_ERROR);
    }

    void TearDown()
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        ASSERT_NE(srt_close(m_caller_socket),   SRT_ERROR);
        ASSERT_NE(srt_close(m_listener_socket), SRT_ERROR);
        srt_cleanup();
    }


public:


    int SetStrictEncryption(PEER_TYPE peer, bool value)
    {
        const SRTSOCKET &socket = peer == PEER_CALLER ? m_caller_socket : m_listener_socket;
        return srt_setsockopt(socket, 0, SRTO_STRICTENC, value ? &s_yes : &s_no, sizeof s_yes);
    }


    bool GetStrictEncryption(PEER_TYPE peer_type)
    {
        const SRTSOCKET socket = peer_type == PEER_CALLER ? m_caller_socket : m_listener_socket;
        int value = -1;
        int value_len = sizeof value;
        EXPECT_EQ(srt_getsockopt(socket, 0, SRTO_STRICTENC, (void*)&value, &value_len), SRT_SUCCESS);
        return value ? true : false;
    }


    int SetPassword(PEER_TYPE peer_type, const std::basic_string<char> &pwd)
    {
        const SRTSOCKET socket = peer_type == PEER_CALLER ? m_caller_socket : m_listener_socket;
        return srt_setsockopt(socket, 0, SRTO_PASSPHRASE, pwd.c_str(), (int) pwd.size());
    }


    int GetKMState(SRTSOCKET socket)
    {
        int km_state = 0;
        int opt_size = sizeof km_state;
        EXPECT_EQ(srt_getsockopt(socket, 0, SRTO_KMSTATE, reinterpret_cast<void*>(&km_state), &opt_size), SRT_SUCCESS);
        
        return km_state;
    }


    int GetSocetkOption(SRTSOCKET socket, SRT_SOCKOPT opt)
    {
        int val = 0;
        int size = sizeof val;
        EXPECT_EQ(srt_getsockopt(socket, 0, opt, reinterpret_cast<void*>(&val), &size), SRT_SUCCESS);

        return val;
    }


    void TestConnect(TEST_CASE test_case)
    {
        // Prepare input state
        const TestCase &test = g_test_matrix[test_case];
        ASSERT_EQ(SetStrictEncryption(PEER_CALLER,   test.strictenc[PEER_CALLER]),   SRT_SUCCESS);
        ASSERT_EQ(SetStrictEncryption(PEER_LISTENER, test.strictenc[PEER_LISTENER]), SRT_SUCCESS);
        ASSERT_EQ(SetPassword(PEER_CALLER,   test.password[PEER_CALLER]),   SRT_SUCCESS);
        ASSERT_EQ(SetPassword(PEER_LISTENER, test.password[PEER_LISTENER]), SRT_SUCCESS);

        const TestResult &expect = test.expected_result;
        // Start testing
        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(5200);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);
        sockaddr* psa = (sockaddr*)&sa;
        
        ASSERT_NE(srt_bind  (m_listener_socket, psa, sizeof sa), SRT_ERROR);
        ASSERT_NE(srt_listen(m_listener_socket, 4),              SRT_ERROR);

        //EXPECT_EQ(srt_accept(m_listener_socket, (sockaddr*)&client_address, &length), expect.accept_ret);

        const int connect_ret = srt_connect(m_caller_socket, psa, sizeof sa);
        EXPECT_EQ(connect_ret, expect.connect_ret);

        if (connect_ret == SRT_ERROR && connect_ret != expect.connect_ret)
        {
            std::cerr << "UNEXPECTED! srt_connect returned error: "
                      << srt_getlasterror_str() << " (code " << srt_getlasterror(NULL) << ")\n";
        }

        const int default_len = 3;
        int rlen = default_len;
        SRTSOCKET read[default_len];

        int wlen = default_len;
        SRTSOCKET write[default_len];

        const int epoll_res = srt_epoll_wait(m_pollid, read, &rlen,
                                             write, &wlen,
                                             500, /* timeout */
                                             0, 0, 0, 0);

        EXPECT_EQ(epoll_res, expect.epoll_wait_ret);
        if (epoll_res == SRT_ERROR)
        {
            EXPECT_EQ(srt_getlasterror(NULL), expect.epoll_wait_error);
            std::cerr << "Epoll returned error: " << srt_getlasterror_str() << " (code " << srt_getlasterror(NULL) << ")\n";
        }

        // In non-blocking mode we expect a socket returned from srt_accept() if the srt_connect succeeded
        sockaddr_in client_address;
        int length = sizeof(sockaddr_in);
        const int accepted_socket = srt_accept(m_listener_socket, (sockaddr*)&client_address, &length);
        EXPECT_NE(accepted_socket, 0);
        if (expect.accept_ret == SRT_INVALID_SOCK)
            EXPECT_EQ(accepted_socket, SRT_INVALID_SOCK);
        else
            EXPECT_NE(accepted_socket, SRT_INVALID_SOCK);

        if (accepted_socket != SRT_INVALID_SOCK)
        {
            EXPECT_EQ(srt_getsockstate(accepted_socket), expect.socket_state[CHECK_SOCKET_ACCEPTED]);
            EXPECT_EQ(GetSocetkOption(accepted_socket, SRTO_SNDKMSTATE), expect.km_state[CHECK_SOCKET_ACCEPTED]);
        }

        if (m_is_tracing)
        {
            if (accepted_socket != SRT_INVALID_SOCK)
                std::cout << "Socket state accepted: " << m_socket_state[srt_getsockstate(accepted_socket)] << "\n";
            std::cout << "Socket state caller:   " << m_socket_state[srt_getsockstate(m_caller_socket)]   << "\n";
            std::cout << "Socket state listener: " << m_socket_state[srt_getsockstate(m_listener_socket)] << "\n";

            if (accepted_socket != SRT_INVALID_SOCK)
            {
                std::cout << "KM State accepted:     " << m_km_state[GetKMState(accepted_socket)] << '\n';
                std::cout << "RCV KM State accepted:     " << m_km_state[GetSocetkOption(accepted_socket, SRTO_RCVKMSTATE)] << '\n';
                std::cout << "SND KM State accepted:     " << m_km_state[GetSocetkOption(accepted_socket, SRTO_SNDKMSTATE)] << '\n';
            }
            std::cout << "KM State caller:       " << m_km_state[GetKMState(m_caller_socket)] << '\n';
            std::cout << "RCV KM State caller:   " << m_km_state[GetSocetkOption(m_caller_socket, SRTO_RCVKMSTATE)] << '\n';
            std::cout << "SND KM State caller:   " << m_km_state[GetSocetkOption(m_caller_socket, SRTO_SNDKMSTATE)] << '\n';
            std::cout << "KM State listener:     " << m_km_state[GetKMState(m_listener_socket)] << '\n';

            std::cout << "wlen: " << wlen << " (write[0] " << write[0] << ", listener " << m_listener_socket << ")\n";
            std::cout << "rlen: " << rlen << " (read[0]  " << read[0]  << ", caller "   << m_caller_socket << ")\n";
        }

        EXPECT_EQ(srt_getsockstate(m_caller_socket),   expect.socket_state[CHECK_SOCKET_CALLER]);
        EXPECT_EQ(GetSocetkOption(m_caller_socket, SRTO_RCVKMSTATE), expect.km_state[CHECK_SOCKET_CALLER]);

        EXPECT_EQ(srt_getsockstate(m_listener_socket), SRTS_LISTENING);
        EXPECT_EQ(GetKMState(m_listener_socket),       SRT_KM_S_UNSECURED);

        EXPECT_EQ(rlen, expect.rnum >= 0 ? expect.rnum : default_len);
        EXPECT_EQ(wlen, expect.wnum >= 0 ? expect.wnum : default_len);
        if (rlen != 0 && rlen != 3)
        {
            EXPECT_EQ(read[0],  m_caller_socket);
        }
        if (wlen != 0 && wlen != 3)
        {
            EXPECT_EQ(write[0], m_caller_socket);
        }

    }


private:
    // put in any custom data members that you need

    SRTSOCKET m_caller_socket   = SRT_INVALID_SOCK;
    SRTSOCKET m_listener_socket = SRT_INVALID_SOCK;

    int       m_pollid          = 0;

    const int s_yes = 1;
    const int s_no  = 0;

    const bool          m_is_tracing = false;
    static const char*  m_km_state[];
    static const char*  m_socket_state[];
};



const char* TestStrictEncryption::m_km_state[] = {
    "SRT_KM_S_UNSECURED (0)",      //No encryption
    "SRT_KM_S_SECURING  (1)",      //Stream encrypted, exchanging Keying Material
    "SRT_KM_S_SECURED   (2)",      //Stream encrypted, keying Material exchanged, decrypting ok.
    "SRT_KM_S_NOSECRET  (3)",      //Stream encrypted and no secret to decrypt Keying Material
    "SRT_KM_S_BADSECRET (4)"       //Stream encrypted and wrong secret, cannot decrypt Keying Material        
};


const char* TestStrictEncryption::m_socket_state[] = {
    "SRTS_INVALID",
    "SRTS_INIT",
    "SRTS_OPENED",
    "SRTS_LISTENING",
    "SRTS_CONNECTING",
    "SRTS_CONNECTED",
    "SRTS_BROKEN",
    "SRTS_CLOSING",
    "SRTS_CLOSED",
    "SRTS_NONEXIST"
};



/** 
 * @fn TEST_F(TestStrictEncryption, PasswordLength)
 * @brief The password length should belong to the interval of [10; 80]
 */
TEST_F(TestStrictEncryption, PasswordLength)
{
    // Empty string sets password to none
    EXPECT_EQ(SetPassword(PEER_CALLER,   std::string("")), SRT_SUCCESS);
    EXPECT_EQ(SetPassword(PEER_LISTENER, std::string("")), SRT_SUCCESS);

    EXPECT_EQ(SetPassword(PEER_CALLER,   std::string("too_short")), SRT_ERROR);
    EXPECT_EQ(SetPassword(PEER_LISTENER, std::string("too_short")), SRT_ERROR);

    std::string long_pwd;
    const int pwd_len = 81;     // 80 is the maximum password length accepted
    long_pwd.reserve(pwd_len);
    const char start_char = '!';

    // Please ensure to be within the valid ASCII symbols!
    ASSERT_LT(pwd_len + start_char, 126);
    for (int i = 0; i < pwd_len; ++i)
        long_pwd.push_back(static_cast<char>(start_char + i));

    EXPECT_EQ(SetPassword(PEER_CALLER,   long_pwd), SRT_ERROR);
    EXPECT_EQ(SetPassword(PEER_LISTENER, long_pwd), SRT_ERROR);

    EXPECT_EQ(SetPassword(PEER_CALLER,   std::string("proper_len")),     SRT_SUCCESS);
    EXPECT_EQ(SetPassword(PEER_LISTENER, std::string("proper_length")),  SRT_SUCCESS);
}


/**
 * @fn TEST_F(TestStrictEncryption, SetGetDefault)
 * @brief The default value for the strict encryption should be ON
 */
TEST_F(TestStrictEncryption, SetGetDefault)
{
    EXPECT_EQ(GetStrictEncryption(PEER_CALLER),   true);
    EXPECT_EQ(GetStrictEncryption(PEER_LISTENER), true);

    EXPECT_EQ(SetStrictEncryption(PEER_CALLER,    false), SRT_SUCCESS);
    EXPECT_EQ(SetStrictEncryption(PEER_LISTENER,  false), SRT_SUCCESS);

    EXPECT_EQ(GetStrictEncryption(PEER_CALLER),   false);
    EXPECT_EQ(GetStrictEncryption(PEER_LISTENER), false);
}


TEST_F(TestStrictEncryption, Case_A_1_Strict_On_On_Pwd_Set_Set_Match)
{
    TestConnect(TEST_CASE_A_1);
}


TEST_F(TestStrictEncryption, Case_A_2_Strict_On_On_Pwd_Set_Set_Mismatch)
{
    TestConnect(TEST_CASE_A_2);
}


TEST_F(TestStrictEncryption, Case_A_3_Strict_On_On_Pwd_Set_None)
{
    TestConnect(TEST_CASE_A_3);
}


TEST_F(TestStrictEncryption, Case_A_4_Strict_On_On_Pwd_None_Set)
{
    TestConnect(TEST_CASE_A_4);
}


TEST_F(TestStrictEncryption, Case_A_5_Strict_On_On_Pwd_None_None)
{
    TestConnect(TEST_CASE_A_5);
}


TEST_F(TestStrictEncryption, Case_B_1_Strict_On_Off_Pwd_Set_Set_Match)
{
    TestConnect(TEST_CASE_B_1);
}


TEST_F(TestStrictEncryption, Case_B_2_Strict_On_Off_Pwd_Set_Set_Mismatch)
{
    TestConnect(TEST_CASE_B_2);
}


TEST_F(TestStrictEncryption, Case_B_3_Strict_On_Off_Pwd_Set_None)
{
    TestConnect(TEST_CASE_B_3);
}


TEST_F(TestStrictEncryption, Case_B_4_Strict_On_Off_Pwd_None_Set)
{
    TestConnect(TEST_CASE_B_4);
}


TEST_F(TestStrictEncryption, Case_B_5_Strict_On_Off_Pwd_None_None)
{
    TestConnect(TEST_CASE_B_5);
}


TEST_F(TestStrictEncryption, Case_C_1_Strict_Off_On_Pwd_Set_Set_Match)
{
    TestConnect(TEST_CASE_C_1);
}


TEST_F(TestStrictEncryption, Case_C_2_Strict_Off_On_Pwd_Set_Set_Mismatch)
{
    TestConnect(TEST_CASE_C_2);
}


TEST_F(TestStrictEncryption, Case_C_3_Strict_Off_On_Pwd_Set_None)
{
    TestConnect(TEST_CASE_C_3);
}


TEST_F(TestStrictEncryption, Case_C_4_Strict_Off_On_Pwd_None_Set)
{
    TestConnect(TEST_CASE_C_4);
}


TEST_F(TestStrictEncryption, Case_C_5_Strict_Off_On_Pwd_None_None)
{
    TestConnect(TEST_CASE_C_5);
}


TEST_F(TestStrictEncryption, Case_D_1_Strict_Off_Off_Pwd_Set_Set_Match)
{
    TestConnect(TEST_CASE_D_1);
}


TEST_F(TestStrictEncryption, Case_D_2_Strict_Off_Off_Pwd_Set_Set_Mismatch)
{
    TestConnect(TEST_CASE_D_2);
}


TEST_F(TestStrictEncryption, Case_D_3_Strict_Off_Off_Pwd_Set_None)
{
    TestConnect(TEST_CASE_D_3);
}


TEST_F(TestStrictEncryption, Case_D_4_Strict_Off_Off_Pwd_None_Set)
{
    TestConnect(TEST_CASE_D_4);
}


TEST_F(TestStrictEncryption, Case_D_5_Strict_Off_Off_Pwd_None_None)
{
    TestConnect(TEST_CASE_D_5);
}

