#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <string>
#include <map>

#ifdef _WIN32
#define INC_SRT_WIN_WINTIME // exclude gettimeofday from srt headers
#endif

#include "srt.h"
#include "utilities.h"

srt_listen_callback_fn SrtTestListenCallback;

class ListenerCallback
    : public testing::Test
{
protected:
    ListenerCallback()
    {
        // initialization code here
    }

    ~ListenerCallback()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }
public:

    SRTSOCKET server_sock, client_sock;
    std::thread accept_thread;
    sockaddr_in sa;
    sockaddr* psa;

    void SetUp()
    {
        ASSERT_EQ(srt_startup(), 0);

        // Create server on 127.0.0.1:5555

        server_sock = srt_create_socket();
        ASSERT_GT(server_sock, 0);    // socket_id should be > 0

        sockaddr_in bind_sa;
        memset(&bind_sa, 0, sizeof bind_sa);
        bind_sa.sin_family = AF_INET;
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &bind_sa.sin_addr), 1);
        bind_sa.sin_port = htons(5555);

        ASSERT_NE(srt_bind(server_sock, (sockaddr*)&bind_sa, sizeof bind_sa), -1);
        ASSERT_NE(srt_listen(server_sock, 5), -1);
        (void)srt_listen_callback(server_sock, &SrtTestListenCallback, NULL);

        accept_thread = std::thread([this] { this->AcceptLoop(); });

        // Prepare client socket

        client_sock = srt_create_socket();
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(5555);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);
        psa = (sockaddr*)&sa;

        ASSERT_GT(client_sock, 0);    // socket_id should be > 0

        auto awhile = std::chrono::milliseconds(20);
        std::this_thread::sleep_for(awhile);
    }

    void AcceptLoop()
    {
        // Setup EID in order to pick up either readiness or error.
        // THis is only to make a formal response side, nothing here is to be tested.

        int eid = srt_epoll_create();

        // Subscribe to R | E

        int re = SRT_EPOLL_IN | SRT_EPOLL_ERR;
        srt_epoll_add_usock(eid, server_sock, &re);

        SRT_EPOLL_EVENT results[2];

        for (;;)
        {
            auto state = srt_getsockstate(server_sock);
            if (int(state) > int(SRTS_CONNECTED))
            {
                std::cout << "[T] Listener socket closed, exitting\n";
                break;
            }

            std::cout << "[T] Waiting for epoll to accept\n";
            int res = srt_epoll_uwait(eid, results, 2, 1000);
            if (res == 1)
            {
                if (results[0].events == SRT_EPOLL_IN)
                {
                    int acp = srt_accept(server_sock, NULL, NULL);
                    if (acp == SRT_ERROR)
                    {
                        std::cout << "[T] Accept failed, so exitting\n";
                        break;
                    }
                    srt_close(acp);
                    continue;
                }

                // Then it can only be SRT_EPOLL_ERR, which
                // can be done by having the socket closed
                break;
            }

            if (res == 0) // probably timeout, just repeat
            {
                std::cout << "[T] (NOTE: epoll timeout, still waiting)\n";
                continue;
            }
        }

        srt_epoll_release(eid);
    }

    void TearDown()
    {
        std::cout << "TeadDown: closing all sockets\n";
        // Close the socket
        EXPECT_EQ(srt_close(client_sock), SRT_SUCCESS);
        EXPECT_EQ(srt_close(server_sock), SRT_SUCCESS);

        // After that, the thread should exit
        std::cout << "TearDown: joining accept thread\n";
        accept_thread.join();
        std::cout << "TearDown: SRT exit\n";

        srt_cleanup();
    }

};

int SrtTestListenCallback(void* opaq, SRTSOCKET ns SRT_ATR_UNUSED, int hsversion, const struct sockaddr* peeraddr, const char* streamid)
{
    using namespace std;

    if (opaq)
    {
        cerr << "ERROR: opaq expected NULL, as passed\n";
        return -1; // enforce EXPECT to fail
    }

    if (hsversion != 5)
    {
        cerr << "ERROR: hsversion expected 5\n";
        return -1;
    }

    if (!peeraddr)
    {
        // XXX Might be better to check the content, too.
        cerr << "ERROR: null peeraddr\n";
        return -1;
    }

    static const map<string, string> passwd {
        {"admin", "thelocalmanager"},
        {"user", "verylongpassword"}
    };

    // Try the "standard interpretation" with username at key u
    string username;

    static const char stdhdr [] = "#!::";
    uint32_t* pattern = (uint32_t*)stdhdr;
    bool found = false;

    if (strlen(streamid) > 4 && *(uint32_t*)streamid == *pattern)
    {
        vector<string> items;
        Split(streamid+4, ',', back_inserter(items));
        for (auto& i: items)
        {
            vector<string> kv;
            Split(i, '=', back_inserter(kv));
            if (kv.size() == 2 && kv[0] == "u")
            {
                username = kv[1];
                found = true;
            }
        }

        if (!found)
        {
            cerr << "TEST: USER NOT FOUND, returning false.\n";
            return -1;
        }
    }
    else
    {
        // By default the whole streamid is username
        username = streamid;
    }

    // This hook sets the password to the just accepted socket
    // depending on the user

    // When not found, it will throw an exception
    cerr << "TEST: Accessing user '" << username << "', might throw if not found\n";
    string exp_pw = passwd.at(username);

#if SRT_ENABLE_ENCRYPTION
    cerr << "TEST: Setting password '" << exp_pw << "' as per user '" << username << "'\n";
    EXPECT_EQ(srt_setsockflag(ns, SRTO_PASSPHRASE, exp_pw.c_str(), exp_pw.size()), SRT_SUCCESS);
#endif

    // Checking that SRTO_RCVLATENCY (PRE option) can be altered in the listener callback.
    int optval = 200;
    EXPECT_EQ(srt_setsockflag(ns, SRTO_RCVLATENCY, &optval, sizeof optval), SRT_SUCCESS);
    return 0;
}


/**
 * This test makes a service and a client connecting to it.
 * The service sets up a callback function on the listener.
 * The listener sets up different passwords depending on the user.
 * The test tests:
 *  - correct connection with correct password (SecureSuccess)
 *  - rejected connection with wrong password (FauxPass)
 *  - rejected connection on nonexistent user (FauxUser)
*/

using namespace std;


TEST_F(ListenerCallback, SecureSuccess)
{
    string username_spec = "#!::u=admin";
    string password = "thelocalmanager";

    ASSERT_NE(srt_setsockflag(client_sock, SRTO_STREAMID, username_spec.c_str(), username_spec.size()), -1);
#if SRT_ENABLE_ENCRYPTION
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_PASSPHRASE, password.c_str(), password.size()), -1);
#endif

    // EXPECTED RESULT: connected successfully
    EXPECT_NE(srt_connect(client_sock, psa, sizeof sa), SRT_ERROR);
}

#if SRT_ENABLE_ENCRYPTION
TEST_F(ListenerCallback, FauxPass)
{
    string username_spec = "#!::u=admin";
    string password = "thelokalmanager"; // (typo :D)

    ASSERT_NE(srt_setsockflag(client_sock, SRTO_STREAMID, username_spec.c_str(), username_spec.size()), -1);
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_PASSPHRASE, password.c_str(), password.size()), -1);

    // EXPECTED RESULT: connection rejected
    EXPECT_EQ(srt_connect(client_sock, psa, sizeof sa), SRT_ERROR);
}
#endif

TEST_F(ListenerCallback, FauxUser)
{
    string username_spec = "#!::u=haivision";
    string password = "thelocalmanager"; // (typo :D)

    ASSERT_NE(srt_setsockflag(client_sock, SRTO_STREAMID, username_spec.c_str(), username_spec.size()), -1);
#if SRT_ENABLE_ENCRYPTION
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_PASSPHRASE, password.c_str(), password.size()), -1);
#endif

    // EXPECTED RESULT: connection rejected
    EXPECT_EQ(srt_connect(client_sock, psa, sizeof sa), SRT_ERROR);
}



