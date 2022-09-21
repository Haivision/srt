#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <map>

#ifdef _WIN32
#define INC_SRT_WIN_WINTIME // exclude gettimeofday from srt headers
#endif

#include "srt.h"
#include "utilities.h"

int OnUserData(void* opaq, SRTSOCKET ns SRT_ATR_UNUSED, const char* buf, int len, const SRT_USERDATACTRL* ctrl);


TEST(Core, Userdata) {

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
    EXPECT_EQ(srt_userdata_callback(server_sock, OnUserData, nullptr), SRT_SUCCESS);

    // Create client to connect to the above server
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);
    sockaddr* psa = (sockaddr*)&sa;

    SRTSOCKET client_sock = srt_create_socket();
    ASSERT_GT(client_sock, 0);    // socket_id should be > 0

    // EXPECTED RESULT: connected successfully
    EXPECT_NE(srt_connect(client_sock, psa, sizeof sa), SRT_ERROR);

    // TODO: align message on 32 bits (handle internally).
    const string userdata = "Custom message";
    EXPECT_EQ(srt_senduserdata(client_sock, userdata.c_str(), (int) userdata.size(), NULL), userdata.size());

    // Close the socket
    EXPECT_EQ(srt_close(client_sock), SRT_SUCCESS);

    (void)srt_cleanup();
}

int OnUserData(void* opaq, SRTSOCKET ns SRT_ATR_UNUSED, const char* buf, int len, const SRT_USERDATACTRL* ctrl)
{
    using namespace std;

    if (opaq)
    {
        cerr << "ERROR: opaq expected NULL, as passed\n";
        return -1; // enforce EXPECT to fail
    }

    cerr << "OnUserData: " << buf << endl;
    return 0;
}



