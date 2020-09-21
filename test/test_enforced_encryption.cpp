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
#include <thread>
#include <condition_variable> 
#include <mutex>

#include "srt.h"
#include "sync.h"



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


struct TestResultNonBlocking
{
    int     connect_ret;
    int     accept_ret;
    int     epoll_wait_ret;
    int     epoll_event;
    int     socket_state[CHECK_SOCKET_COUNT];
    int     km_state    [CHECK_SOCKET_COUNT];
};


struct TestResultBlocking
{
    int     connect_ret;
    int     accept_ret;
    int     socket_state[CHECK_SOCKET_COUNT];
    int     km_state[CHECK_SOCKET_COUNT];
};


template<typename TResult>
struct TestCase
{
    bool                enforcedenc [PEER_COUNT];
    const std::string  (&password)[PEER_COUNT];
    TResult             expected_result;
};

typedef TestCase<TestResultNonBlocking>  TestCaseNonBlocking;
typedef TestCase<TestResultBlocking>     TestCaseBlocking;



static const std::string s_pwd_a ("s!t@r#i$c^t");
static const std::string s_pwd_b ("s!t@r#i$c^tu");
static const std::string s_pwd_no("");



/*
 * TESTING SCENARIO
 * Both peers exchange HandShake v5.
 * Listener is sender   in a non-blocking mode
 * Caller   is receiver in a non-blocking mode

 * Cases B.2-B.4 are specific. Here we have incompatible password settings, but
 * listener accepts it, while caller rejects it. In this case we have a short-living
 * confusion state: The connection is accepted on the listener side, and the listener
 * sends back the conclusion handshake, but caller will reject it.
 *
 * Because of that, we should ignore what will happen in the listener as this is
 * just a matter of luck: if the listener thread is lucky, it will report the socket
 * to accept, so epoll will signal it and accept will report it, and moreover, further
 * good luck on this socket would make the state check return SRTS_CONNECTED. Without
 * this good luck, the caller might be quick enough to reject the handshake and send
 * the UMSG_SHUTDOWN packet to the peer. If it gets with it before acceptance, it will
 * withdraw the socket before it could be reported by accept.
 *
 * Still, we check predictable things here, so we accept two possibilities:
 * - The accepted socket wasn't reported at all
 * - The accepted socket was reported, and after `srt_connect` is done, it should turn to SRTS_BROKEN.
 *
 * This embraces both cases when the accepted socket was broken in the beginning, and when it was CONNECTED
 * in the beginning, but broke soon thereafter.
 *
 * This behavior is predicted and accepted - it's also the reason that setting ENFORCEDENC to false is
 * NOT RECOMMENDED on a listener socket that isn't intended to accept only connections from known callers
 * that are known to have set this flag also to false.
 *
 * In the cases C.2-C.4 it is the listener who rejects the connection, so we don't have an accepted socket
 * and the situation is always the same and clear in the beginning. The caller cannot continue with the
 * connection after listener accepted it, even if it tolerates incompatible password settings.
 */

const int IGNORE_EPOLL = -2;
const int IGNORE_SRTS = -1;

const TestCaseNonBlocking g_test_matrix_non_blocking[] =
{
        // ENFORCEDENC       |  Password           |                                | EPoll wait                       | socket_state                            |  KM State
        // caller | listener |  caller  | listener |  connect_ret   accept_ret      |  ret         | event             | caller              accepted |  caller              listener
/*A.1 */ { {true,     true  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*A.2 */ { {true,     true  }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
/*A.3 */ { {true,     true  }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
/*A.4 */ { {true,     true  }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
/*A.5 */ { {true,     true  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

/*B.1 */ { {true,    false  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*B.2 */ { {true,    false  }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS,                0,  IGNORE_EPOLL,  0,             {SRTS_CONNECTING,   SRTS_BROKEN}, {SRT_KM_S_BADSECRET, SRT_KM_S_BADSECRET}}},
/*B.3 */ { {true,    false  }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS,                0,  IGNORE_EPOLL,  0,             {SRTS_CONNECTING,   SRTS_BROKEN}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
/*B.4 */ { {true,    false  }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS,                0,  IGNORE_EPOLL,  0,             {SRTS_CONNECTING,   SRTS_BROKEN}, {SRT_KM_S_UNSECURED,  SRT_KM_S_NOSECRET}}},
/*B.5 */ { {true,    false  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

/*C.1 */ { {false,    true  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*C.2 */ { {false,    true  }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
/*C.3 */ { {false,    true  }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
/*C.4 */ { {false,    true  }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS, SRT_INVALID_SOCK,             0,  0,             {SRTS_BROKEN,       IGNORE_SRTS}, {SRT_KM_S_UNSECURED,        IGNORE_SRTS}}},
/*C.5 */ { {false,    true  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

/*D.1 */ { {false,   false  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*D.2 */ { {false,   false  }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_BADSECRET, SRT_KM_S_BADSECRET}}},
/*D.3 */ { {false,   false  }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
/*D.4 */ { {false,   false  }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_NOSECRET,   SRT_KM_S_NOSECRET}}},
/*D.5 */ { {false,   false  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                0,             1,  SRT_EPOLL_IN,  {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
};


/*
 * TESTING SCENARIO
 * Both peers exchange HandShake v5.
 * Listener is sender   in a blocking mode
 * Caller   is receiver in a blocking mode
 *
 * In the cases B.2-B.4 the caller will reject the connection due to the enforced encryption check
 * of the HS response from the listener on the stage of the KM response check.
 * While the listener accepts the connection with the connected state. So the caller sends UMSG_SHUTDOWN
 * to notify the listener that it has closed the connection. The accepted socket gets the SRTS_BROKEN states.
 * For these cases a special accept_ret = -2 is used, that allows the accepted socket to be broken or already closed.
 *
 * In the cases C.2-C.4 it is the listener who rejects the connection, so we don't have an accepted socket.
 */
const TestCaseBlocking g_test_matrix_blocking[] =
{
        // ENFORCEDENC       |  Password           |                                      | socket_state                   |  KM State
        // caller | listener |  caller  | listener |  connect_ret         accept_ret      | caller                accepted |  caller              listener
/*A.1 */ { {true,     true  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*A.2 */ { {true,     true  }, {s_pwd_a,   s_pwd_b}, { SRT_INVALID_SOCK, SRT_INVALID_SOCK, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*A.3 */ { {true,     true  }, {s_pwd_a,  s_pwd_no}, { SRT_INVALID_SOCK, SRT_INVALID_SOCK, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*A.4 */ { {true,     true  }, {s_pwd_no,  s_pwd_b}, { SRT_INVALID_SOCK, SRT_INVALID_SOCK, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*A.5 */ { {true,     true  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

/*B.1 */ { {true,    false  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*B.2 */ { {true,    false  }, {s_pwd_a,   s_pwd_b}, { SRT_INVALID_SOCK,               -2, {SRTS_OPENED,       SRTS_BROKEN}, {SRT_KM_S_BADSECRET, SRT_KM_S_BADSECRET}}},
/*B.3 */ { {true,    false  }, {s_pwd_a,  s_pwd_no}, { SRT_INVALID_SOCK,               -2, {SRTS_OPENED,       SRTS_BROKEN}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
/*B.4 */ { {true,    false  }, {s_pwd_no,  s_pwd_b}, { SRT_INVALID_SOCK,               -2, {SRTS_OPENED,       SRTS_BROKEN}, {SRT_KM_S_UNSECURED,  SRT_KM_S_NOSECRET}}},
/*B.5 */ { {true,    false  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

/*C.1 */ { {false,    true  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*C.2 */ { {false,    true  }, {s_pwd_a,   s_pwd_b}, { SRT_INVALID_SOCK, SRT_INVALID_SOCK, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*C.3 */ { {false,    true  }, {s_pwd_a,  s_pwd_no}, { SRT_INVALID_SOCK, SRT_INVALID_SOCK, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*C.4 */ { {false,    true  }, {s_pwd_no,  s_pwd_b}, { SRT_INVALID_SOCK, SRT_INVALID_SOCK, {SRTS_OPENED,                -1}, {SRT_KM_S_UNSECURED,                 -1}}},
/*C.5 */ { {false,    true  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},

/*D.1 */ { {false,   false  }, {s_pwd_a,   s_pwd_a}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_SECURED,     SRT_KM_S_SECURED}}},
/*D.2 */ { {false,   false  }, {s_pwd_a,   s_pwd_b}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_BADSECRET, SRT_KM_S_BADSECRET}}},
/*D.3 */ { {false,   false  }, {s_pwd_a,  s_pwd_no}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
/*D.4 */ { {false,   false  }, {s_pwd_no,  s_pwd_b}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_NOSECRET,   SRT_KM_S_NOSECRET}}},
/*D.5 */ { {false,   false  }, {s_pwd_no, s_pwd_no}, { SRT_SUCCESS,                     0, {SRTS_CONNECTED, SRTS_CONNECTED}, {SRT_KM_S_UNSECURED, SRT_KM_S_UNSECURED}}},
};



class TestEnforcedEncryption
    : public ::testing::Test
{
protected:
    TestEnforcedEncryption()
    {
        // initialization code here
    }

    ~TestEnforcedEncryption()
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
        ASSERT_NE(srt_setsockopt (m_caller_socket, 0, SRTO_TSBPDMODE, &s_yes, sizeof s_yes), SRT_ERROR);

        m_listener_socket = srt_create_socket();
        ASSERT_NE(m_listener_socket, SRT_INVALID_SOCK);

        ASSERT_NE(srt_setsockflag(m_listener_socket,    SRTO_SENDER,    &s_no,  sizeof s_no),  SRT_ERROR);
        ASSERT_NE(srt_setsockopt (m_listener_socket, 0, SRTO_TSBPDMODE, &s_yes, sizeof s_yes), SRT_ERROR);

        // Will use this epoll to wait for srt_accept(...)
        const int epoll_out = SRT_EPOLL_IN | SRT_EPOLL_ERR;
        ASSERT_NE(srt_epoll_add_usock(m_pollid, m_listener_socket, &epoll_out), SRT_ERROR);
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


    int SetEnforcedEncryption(PEER_TYPE peer, bool value)
    {
        const SRTSOCKET &socket = peer == PEER_CALLER ? m_caller_socket : m_listener_socket;
        return srt_setsockopt(socket, 0, SRTO_ENFORCEDENCRYPTION, value ? &s_yes : &s_no, sizeof s_yes);
    }


    bool GetEnforcedEncryption(PEER_TYPE peer_type)
    {
        const SRTSOCKET socket = peer_type == PEER_CALLER ? m_caller_socket : m_listener_socket;
        int value = -1;
        int value_len = sizeof value;
        EXPECT_EQ(srt_getsockopt(socket, 0, SRTO_ENFORCEDENCRYPTION, (void*)&value, &value_len), SRT_SUCCESS);
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


    template<typename TResult>
    int WaitOnEpoll(const TResult &expect);


    template<typename TResult>
    const TestCase<TResult>& GetTestMatrix(TEST_CASE test_case) const;

    template<typename TResult>
    void TestConnect(TEST_CASE test_case/*, bool is_blocking*/)
    {
        const bool is_blocking = std::is_same<TResult, TestResultBlocking>::value;
        if (is_blocking)
        {
            ASSERT_NE(srt_setsockopt(  m_caller_socket, 0, SRTO_RCVSYN, &s_yes, sizeof s_yes), SRT_ERROR);
            ASSERT_NE(srt_setsockopt(  m_caller_socket, 0, SRTO_SNDSYN, &s_yes, sizeof s_yes), SRT_ERROR);
            ASSERT_NE(srt_setsockopt(m_listener_socket, 0, SRTO_RCVSYN, &s_yes, sizeof s_yes), SRT_ERROR);
            ASSERT_NE(srt_setsockopt(m_listener_socket, 0, SRTO_SNDSYN, &s_yes, sizeof s_yes), SRT_ERROR);
        }
        else
        {
            ASSERT_NE(srt_setsockopt(  m_caller_socket, 0, SRTO_RCVSYN, &s_no, sizeof s_no), SRT_ERROR); // non-blocking mode
            ASSERT_NE(srt_setsockopt(  m_caller_socket, 0, SRTO_SNDSYN, &s_no, sizeof s_no), SRT_ERROR); // non-blocking mode
            ASSERT_NE(srt_setsockopt(m_listener_socket, 0, SRTO_RCVSYN, &s_no, sizeof s_no), SRT_ERROR); // non-blocking mode
            ASSERT_NE(srt_setsockopt(m_listener_socket, 0, SRTO_SNDSYN, &s_no, sizeof s_no), SRT_ERROR); // non-blocking mode
        }

        // Prepare input state
        const TestCase<TResult> &test = GetTestMatrix<TResult>(test_case);
        ASSERT_EQ(SetEnforcedEncryption(PEER_CALLER, test.enforcedenc[PEER_CALLER]), SRT_SUCCESS);
        ASSERT_EQ(SetEnforcedEncryption(PEER_LISTENER, test.enforcedenc[PEER_LISTENER]), SRT_SUCCESS);

        ASSERT_EQ(SetPassword(PEER_CALLER, test.password[PEER_CALLER]), SRT_SUCCESS);
        ASSERT_EQ(SetPassword(PEER_LISTENER, test.password[PEER_LISTENER]), SRT_SUCCESS);

        const TResult &expect = test.expected_result;

        // Start testing
        volatile bool caller_done = false;
        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(5200);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);
        sockaddr* psa = (sockaddr*)&sa;
        ASSERT_NE(srt_bind(m_listener_socket, psa, sizeof sa), SRT_ERROR);
        ASSERT_NE(srt_listen(m_listener_socket, 4), SRT_ERROR);

        auto accepting_thread = std::thread([&] {
            const int epoll_event = WaitOnEpoll(expect);

            // In a blocking mode we expect a socket returned from srt_accept() if the srt_connect succeeded.
            // In a non-blocking mode we expect a socket returned from srt_accept() if the srt_connect succeeded,
            // otherwise SRT_INVALID_SOCKET after the listening socket is closed.
            sockaddr_in client_address;
            int length = sizeof(sockaddr_in);
            SRTSOCKET accepted_socket = -1;
            if (epoll_event == SRT_EPOLL_IN)
            {
                accepted_socket = srt_accept(m_listener_socket, (sockaddr*)&client_address, &length);
                std::cout << "ACCEPT: done, result=" << accepted_socket << std::endl;
            }
            else
            {
                std::cout << "ACCEPT: NOT done\n";
            }

            if (accepted_socket == SRT_INVALID_SOCK)
            {
                std::cerr << "[T] ACCEPT ERROR: " << srt_getlasterror_str() << std::endl;
            }
            else
            {
                std::cerr << "[T] ACCEPT SUCCEEDED: @" << accepted_socket << "\n";
            }

            EXPECT_NE(accepted_socket, 0);
            if (expect.accept_ret == SRT_INVALID_SOCK)
            {
                EXPECT_EQ(accepted_socket, SRT_INVALID_SOCK);
            }
            else if (expect.accept_ret != -2)
            {
                EXPECT_NE(accepted_socket, SRT_INVALID_SOCK);
            }

            if (accepted_socket != SRT_INVALID_SOCK && expect.socket_state[CHECK_SOCKET_ACCEPTED] != IGNORE_SRTS)
            {
                if (m_is_tracing)
                {
                    std::cerr << "EARLY Socket state accepted: " << m_socket_state[srt_getsockstate(accepted_socket)]
                        << " (expected: " << m_socket_state[expect.socket_state[CHECK_SOCKET_ACCEPTED]] << ")\n";
                    std::cerr << "KM State accepted:     " << m_km_state[GetKMState(accepted_socket)] << '\n';
                    std::cerr << "RCV KM State accepted:     " << m_km_state[GetSocetkOption(accepted_socket, SRTO_RCVKMSTATE)] << '\n';
                    std::cerr << "SND KM State accepted:     " << m_km_state[GetSocetkOption(accepted_socket, SRTO_SNDKMSTATE)] << '\n';
                }

                // We have to wait some time for the socket to be able to process the HS responce from the caller.
                // In test cases B2 - B4 the socket is expected to change its state from CONNECTED to BROKEN
                // due to KM mismatches
                do
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                } while (!caller_done);

                const SRT_SOCKSTATUS status = srt_getsockstate(accepted_socket);
                if (m_is_tracing)
                {
                    std::cerr << "LATE Socket state accepted: " << m_socket_state[status]
                        << " (expected: " << m_socket_state[expect.socket_state[CHECK_SOCKET_ACCEPTED]] << ")\n";
                }

                if (expect.socket_state[CHECK_SOCKET_ACCEPTED] == SRTS_BROKEN)
                {
                    EXPECT_TRUE(accepted_socket == -1 || status == SRTS_BROKEN || status == SRTS_CLOSED);
                }
                else
                {
                    EXPECT_EQ(status, expect.socket_state[CHECK_SOCKET_ACCEPTED]);
                    EXPECT_EQ(GetSocetkOption(accepted_socket, SRTO_SNDKMSTATE), expect.km_state[CHECK_SOCKET_ACCEPTED]);
                }
            }
        });

        const int connect_ret = srt_connect(m_caller_socket, psa, sizeof sa);
        EXPECT_EQ(connect_ret, expect.connect_ret);

        if (connect_ret == SRT_ERROR && connect_ret != expect.connect_ret)
        {
            std::cerr << "UNEXPECTED! srt_connect returned error: "
                << srt_getlasterror_str() << " (code " << srt_getlasterror(NULL) << ")\n";
        }

        caller_done = true;

        if (is_blocking == false)
            accepting_thread.join();

        if (m_is_tracing)
        {
            std::cerr << "Socket state caller:   " << m_socket_state[srt_getsockstate(m_caller_socket)] << "\n";
            std::cerr << "Socket state listener: " << m_socket_state[srt_getsockstate(m_listener_socket)] << "\n";
            std::cerr << "KM State caller:       " << m_km_state[GetKMState(m_caller_socket)] << '\n';
            std::cerr << "RCV KM State caller:   " << m_km_state[GetSocetkOption(m_caller_socket, SRTO_RCVKMSTATE)] << '\n';
            std::cerr << "SND KM State caller:   " << m_km_state[GetSocetkOption(m_caller_socket, SRTO_SNDKMSTATE)] << '\n';
            std::cerr << "KM State listener:     " << m_km_state[GetKMState(m_listener_socket)] << '\n';
        }

        // If a blocking call to srt_connect() returned error, then the state is not valid,
        // but we still check it because we know what it should be. This way we may see potential changes in the core behavior.
        EXPECT_EQ(srt_getsockstate(m_caller_socket), expect.socket_state[CHECK_SOCKET_CALLER]);
        EXPECT_EQ(GetSocetkOption(m_caller_socket, SRTO_RCVKMSTATE), expect.km_state[CHECK_SOCKET_CALLER]);

        EXPECT_EQ(srt_getsockstate(m_listener_socket), SRTS_LISTENING);
        EXPECT_EQ(GetKMState(m_listener_socket), SRT_KM_S_UNSECURED);

        if (is_blocking)
        {
            // srt_accept() has no timeout, so we have to close the socket and wait for the thread to exit.
            // Just give it some time and close the socket.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ASSERT_NE(srt_close(m_listener_socket), SRT_ERROR);
            accepting_thread.join();
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
    static const char* const* m_socket_state;
};



template<>
int TestEnforcedEncryption::WaitOnEpoll<TestResultBlocking>(const TestResultBlocking &)
{
    return SRT_EPOLL_IN;
}

static std::ostream& PrintEpollEvent(std::ostream& os, int events, int et_events)
{
    using namespace std;

    static pair<int, const char*> const namemap [] = {
        make_pair(SRT_EPOLL_IN, "R"),
        make_pair(SRT_EPOLL_OUT, "W"),
        make_pair(SRT_EPOLL_ERR, "E"),
        make_pair(SRT_EPOLL_UPDATE, "U")
    };

    int N = Size(namemap);

    for (int i = 0; i < N; ++i)
    {
        if (events & namemap[i].first)
        {
            os << "[";
            if (et_events & namemap[i].first)
                os << "^";
            os << namemap[i].second << "]";
        }
    }

    return os;
}

template<>
int TestEnforcedEncryption::WaitOnEpoll<TestResultNonBlocking>(const TestResultNonBlocking &expect)
{
    const int default_len = 3;
    SRT_EPOLL_EVENT ready[default_len];
    const int epoll_res = srt_epoll_uwait(m_pollid, ready, default_len, 500);
    std::cerr << "Epoll wait result: " << epoll_res;
    if (epoll_res > 0)
    {
        std::cerr << " FOUND: @" << ready[0].fd << " in ";
        PrintEpollEvent(std::cerr, ready[0].events, 0);
    }
    else
    {
        std::cerr << " NOTHING READY";
    }
    std::cerr << std::endl;

    // Expect: -2 means that 
    if (expect.epoll_wait_ret != IGNORE_EPOLL)
    {
        EXPECT_EQ(epoll_res, expect.epoll_wait_ret);
    }

    if (epoll_res == SRT_ERROR)
    {
        std::cerr << "Epoll returned error: " << srt_getlasterror_str() << " (code " << srt_getlasterror(NULL) << ")\n";
        return 0;
    }

    // We have exactly one socket here and we expect to return
    // only this one, or nothing.
    if (epoll_res != 0)
    {
        EXPECT_EQ(epoll_res, 1);
        EXPECT_EQ(ready[0].fd, m_listener_socket);
    }

    return epoll_res == 0 ? 0 : int(ready[0].events);
}


template<>
const TestCase<TestResultBlocking>& TestEnforcedEncryption::GetTestMatrix<TestResultBlocking>(TEST_CASE test_case) const
{
    return g_test_matrix_blocking[test_case];
}

template<>
const TestCase<TestResultNonBlocking>& TestEnforcedEncryption::GetTestMatrix<TestResultNonBlocking>(TEST_CASE test_case) const
{
    return g_test_matrix_non_blocking[test_case];
}



const char* TestEnforcedEncryption::m_km_state[] = {
    "SRT_KM_S_UNSECURED (0)",      //No encryption
    "SRT_KM_S_SECURING  (1)",      //Stream encrypted, exchanging Keying Material
    "SRT_KM_S_SECURED   (2)",      //Stream encrypted, keying Material exchanged, decrypting ok.
    "SRT_KM_S_NOSECRET  (3)",      //Stream encrypted and no secret to decrypt Keying Material
    "SRT_KM_S_BADSECRET (4)"       //Stream encrypted and wrong secret, cannot decrypt Keying Material        
};


static const char* const socket_state_array[] = {
    "IGNORE_SRTS",
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

// A trick that allows the array to be indexed by -1
const char* const* TestEnforcedEncryption::m_socket_state = socket_state_array+1;

/** 
 * @fn TEST_F(TestEnforcedEncryption, PasswordLength)
 * @brief The password length should belong to the interval of [10; 80]
 */
TEST_F(TestEnforcedEncryption, PasswordLength)
{
#ifdef SRT_ENABLE_ENCRYPTION
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
#else
    EXPECT_EQ(SetPassword(PEER_CALLER, "whateverpassword"), SRT_ERROR);
#endif
}


/**
 * @fn TEST_F(TestEnforcedEncryption, SetGetDefault)
 * @brief The default value for the enforced encryption should be ON
 */
TEST_F(TestEnforcedEncryption, SetGetDefault)
{
    EXPECT_EQ(GetEnforcedEncryption(PEER_CALLER),   true);
    EXPECT_EQ(GetEnforcedEncryption(PEER_LISTENER), true);

    EXPECT_EQ(SetEnforcedEncryption(PEER_CALLER,    false), SRT_SUCCESS);
    EXPECT_EQ(SetEnforcedEncryption(PEER_LISTENER,  false), SRT_SUCCESS);

    EXPECT_EQ(GetEnforcedEncryption(PEER_CALLER),   false);
    EXPECT_EQ(GetEnforcedEncryption(PEER_LISTENER), false);
}


#define CREATE_TEST_CASE_BLOCKING(CASE_NUMBER, DESC) TEST_F(TestEnforcedEncryption, CASE_NUMBER##_Blocking_##DESC)\
{\
    TestConnect<TestResultBlocking>(TEST_##CASE_NUMBER);\
}

#define CREATE_TEST_CASE_NONBLOCKING(CASE_NUMBER, DESC) TEST_F(TestEnforcedEncryption, CASE_NUMBER##_NonBlocking_##DESC)\
{\
    TestConnect<TestResultNonBlocking>(TEST_##CASE_NUMBER);\
}


#define CREATE_TEST_CASES(CASE_NUMBER, DESC) \
    CREATE_TEST_CASE_NONBLOCKING(CASE_NUMBER, DESC) \
    CREATE_TEST_CASE_BLOCKING(CASE_NUMBER, DESC)

#ifdef SRT_ENABLE_ENCRYPTION
CREATE_TEST_CASES(CASE_A_1, Enforced_On_On_Pwd_Set_Set_Match)
CREATE_TEST_CASES(CASE_A_2, Enforced_On_On_Pwd_Set_Set_Mismatch)
CREATE_TEST_CASES(CASE_A_3, Enforced_On_On_Pwd_Set_None)
CREATE_TEST_CASES(CASE_A_4, Enforced_On_On_Pwd_None_Set)
#endif
CREATE_TEST_CASES(CASE_A_5, Enforced_On_On_Pwd_None_None)

#ifdef SRT_ENABLE_ENCRYPTION
CREATE_TEST_CASES(CASE_B_1, Enforced_On_Off_Pwd_Set_Set_Match)
CREATE_TEST_CASES(CASE_B_2, Enforced_On_Off_Pwd_Set_Set_Mismatch)
CREATE_TEST_CASES(CASE_B_3, Enforced_On_Off_Pwd_Set_None)
CREATE_TEST_CASES(CASE_B_4, Enforced_On_Off_Pwd_None_Set)
#endif
CREATE_TEST_CASES(CASE_B_5, Enforced_On_Off_Pwd_None_None)

#ifdef SRT_ENABLE_ENCRYPTION
CREATE_TEST_CASES(CASE_C_1, Enforced_Off_On_Pwd_Set_Set_Match)
CREATE_TEST_CASES(CASE_C_2, Enforced_Off_On_Pwd_Set_Set_Mismatch)
CREATE_TEST_CASES(CASE_C_3, Enforced_Off_On_Pwd_Set_None)
CREATE_TEST_CASES(CASE_C_4, Enforced_Off_On_Pwd_None_Set)
#endif
CREATE_TEST_CASES(CASE_C_5, Enforced_Off_On_Pwd_None_None)

#ifdef SRT_ENABLE_ENCRYPTION
CREATE_TEST_CASES(CASE_D_1, Enforced_Off_Off_Pwd_Set_Set_Match)
CREATE_TEST_CASES(CASE_D_2, Enforced_Off_Off_Pwd_Set_Set_Mismatch)
CREATE_TEST_CASES(CASE_D_3, Enforced_Off_Off_Pwd_Set_None)
CREATE_TEST_CASES(CASE_D_4, Enforced_Off_Off_Pwd_None_Set)
#endif
CREATE_TEST_CASES(CASE_D_5, Enforced_Off_Off_Pwd_None_None)

