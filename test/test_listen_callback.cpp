#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <map>

#ifdef _WIN32
#define INC_SRT_WIN_WINTIME // exclude gettimeofday from srt headers
#endif

#include "srt.h"
#include "utilities.h"


srt_listen_callback_fn SrtTestListenCallback;

/**
 * This test makes a service and a client connecting to it.
 * The service sets up a callback function on the listener.
 * The listener sets up different passwords depending on the user.
 * The test tests:
 *  - correct connection with correct password
 *  - rejected connection with wrong password
 *  - rejected connection on nonexistent user
*/
TEST(Core, ListenCallback) {

    using namespace std;

    ASSERT_EQ(srt_startup(), 0);

    // Create server on 127.0.0.1:5555

    const SRTSOCKET server_sock = srt_create_socket();
    ASSERT_GT(server_sock, 0);    // socket_id should be > 0

    sockaddr_in bind_sa;
    memset(&bind_sa, 0, sizeof bind_sa);
    bind_sa.sin_family = AF_INET;
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &bind_sa.sin_addr), 1);
    bind_sa.sin_port = htons(5555);

    ASSERT_NE(srt_bind(server_sock, (sockaddr*)&bind_sa, sizeof bind_sa), -1);
    ASSERT_NE(srt_listen(server_sock, 5), -1);
    (void)srt_listen_callback(server_sock, &SrtTestListenCallback, NULL);

    // Create client to connect to the above server
    SRTSOCKET client_sock;
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);
    sockaddr* psa = (sockaddr*)&sa;


    cerr << "TEST 1: Connect to an encrypted socket correctly (should succeed)\n";

    client_sock = srt_create_socket();
    ASSERT_GT(client_sock, 0);    // socket_id should be > 0

    string username_spec = "#!::u=admin";
    string password = "thelocalmanager";

    ASSERT_NE(srt_setsockflag(client_sock, SRTO_STREAMID, username_spec.c_str(), username_spec.size()), -1);
#if SRT_ENABLE_ENCRYPTION
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_PASSPHRASE, password.c_str(), password.size()), -1);
#endif

    // EXPECTED RESULT: connected successfully
    EXPECT_NE(srt_connect(client_sock, psa, sizeof sa), SRT_ERROR);

    // Close the socket
    EXPECT_EQ(srt_close(client_sock), SRT_SUCCESS);


    cerr << "TEST 2: Connect with a wrong password (should reject the handshake)\n";
#if SRT_ENABLE_ENCRYPTION
    client_sock = srt_create_socket();
    ASSERT_GT(client_sock, 0);    // socket_id should be > 0

    password = "thelokalmanager"; // (typo :D)

    ASSERT_NE(srt_setsockflag(client_sock, SRTO_STREAMID, username_spec.c_str(), username_spec.size()), -1);
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_PASSPHRASE, password.c_str(), password.size()), -1);

    // EXPECTED RESULT: connection rejected
    EXPECT_EQ(srt_connect(client_sock, psa, sizeof sa), SRT_ERROR);

    // Close the socket
    EXPECT_EQ(srt_close(client_sock), SRT_SUCCESS);
#endif


    cerr << "TEST 3: Connect with wrong username (should exit on exception)\n";
    client_sock = srt_create_socket();
    ASSERT_GT(client_sock, 0);    // socket_id should be > 0

    username_spec = "#!::u=haivision";
    password = "thelocalmanager"; // (typo :D)

    ASSERT_NE(srt_setsockflag(client_sock, SRTO_STREAMID, username_spec.c_str(), username_spec.size()), -1);
#if SRT_ENABLE_ENCRYPTION
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_PASSPHRASE, password.c_str(), password.size()), -1);
#endif

    // EXPECTED RESULT: connection rejected
    EXPECT_EQ(srt_connect(client_sock, psa, sizeof sa), SRT_ERROR);

    // Close the socket
    EXPECT_EQ(srt_close(client_sock), SRT_SUCCESS);

    (void)srt_cleanup();
}

int SrtTestListenCallback(void* opaq, SRTSOCKET ns, int hsversion, const struct sockaddr* peeraddr, const char* streamid)
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
    bool found = -1;

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
    srt_setsockflag(ns, SRTO_PASSPHRASE, exp_pw.c_str(), exp_pw.size());
#endif
    return 0;
}


