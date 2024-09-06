#include <string>
#include <iterator>
#include <vector>
#include <sstream>

#include "gtest/gtest.h"
#include "test_env.h"

#include "srt.h"
#include "netinet_any.h"

using namespace std;

int main(int argc, char **argv)
{
    string command_line_arg(argc == 2 ? argv[1] : "");
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new srt::TestEnv(argc, argv));
    return RUN_ALL_TESTS();
}

namespace srt
{

TestEnv* TestEnv::me = 0;

void TestEnv::FillArgMap()
{
    // The rule is:
    // - first arguments go to an empty string key
    // - if an argument has - in the beginning, name the key
    // - key followed by args collected in a list
    // - double dash prevents interpreting further args as option keys

    string key;
    bool expectkey = true;

    argmap[""];

    for (auto& a: args)
    {
        if (a.size() > 1)
        {
            if (expectkey && a[0] == '-')
            {
                if (a[1] == '-')
                    expectkey = false;
                else if (a[1] == '/')
                    key = "";
                else
                {
                    key = a.substr(1);
                    argmap[key]; // Make sure it exists even empty
                }

                continue;
            }
        }
        argmap[key].push_back(a);
    }

    return;
}

std::string TestEnv::OptionValue(const std::string& key)
{
    std::ostringstream out;

    auto it = argmap.find(key);
    if (it != argmap.end() && !it->second.empty())
    {
        auto iv = it->second.begin();
        out << (*iv);
        while (++iv != it->second.end())
        {
            out << " " << (*iv);
        }
    }

    return out.str();
}

// Specific functions
bool TestEnv::Allowed_IPv6()
{
    if (TestEnv::me->OptionPresent("disable-ipv6"))
    {
        std::cout << "TEST: IPv6 testing disabled, FORCED PASS\n";
        return false;
    }
    return true;
}


void TestInit::start(int& w_retstatus)
{
    ASSERT_GE(w_retstatus = srt_startup(), 0);
}

void TestInit::stop()
{
    EXPECT_NE(srt_cleanup(), -1);
}

// This function finds some interesting options among command
// line arguments and does specific things.
void TestInit::HandlePerTestOptions()
{
    // As a short example:
    // use '-logdebug' option to turn on debug logging.

    if (TestEnv::me->OptionPresent("logdebug"))
    {
        srt_setloglevel(LOG_DEBUG);
    }

    if (TestEnv::me->OptionPresent("lognote"))
    {
        srt_setloglevel(LOG_NOTICE);
    }
}

// Copied from ../apps/apputil.cpp, can't really link this file here.
sockaddr_any CreateAddr(const std::string& name, unsigned short port, int pref_family)
{
    using namespace std;

    // Handle empty name.
    // If family is specified, empty string resolves to ANY of that family.
    // If not, it resolves to IPv4 ANY (to specify IPv6 any, use [::]).
    if (name == "")
    {
        sockaddr_any result(pref_family == AF_INET6 ? pref_family : AF_INET);
        result.hport(port);
        return result;
    }

    bool first6 = pref_family != AF_INET;
    int families[2] = {AF_INET6, AF_INET};
    if (!first6)
    {
        families[0] = AF_INET;
        families[1] = AF_INET6;
    }

    for (int i = 0; i < 2; ++i)
    {
        int family = families[i];
        sockaddr_any result (family);

        // Try to resolve the name by pton first
        if (inet_pton(family, name.c_str(), result.get_addr()) == 1)
        {
            result.hport(port); // same addr location in ipv4 and ipv6
            return result;
        }
    }

    // If not, try to resolve by getaddrinfo
    // This time, use the exact value of pref_family

    sockaddr_any result;
    addrinfo fo = {
        0,
        pref_family,
        0, 0,
        0, 0,
        NULL, NULL
    };

    addrinfo* val = nullptr;
    int erc = getaddrinfo(name.c_str(), nullptr, &fo, &val);
    if (erc == 0)
    {
        result.set(val->ai_addr);
        result.len = result.size();
        result.hport(port); // same addr location in ipv4 and ipv6
    }
    freeaddrinfo(val);

    return result;
}

UniqueSocket::~UniqueSocket()
{
    // Could be closed explicitly
    if (sock != -1)
        close();
}

void UniqueSocket::close()
{
    int close_result = srt_close(sock);
    int close_error = srt_getlasterror(nullptr);

    // XXX SRT_EINVSOCK is reported when the socket
    // has been already wiped out, which may happen to a broken socket.
    // This isn't exactly intended, although trying to close a nonexistent
    // socket is not a problem, as long as it happens before the id value rollover
    // (that is, when it's closed immediately after getting broken).
    // This solution is still slick though and should be fixed.
    //
    // Restore this, when fixed
    // EXPECT_NE(srt_close(sock), SRT_ERROR) << lab << " CREATED: "<< f << ":" << l;
    if (close_result == SRT_ERROR)
    {
        EXPECT_NE(close_error, SRT_EINVSOCK) << lab << " CREATED: "<< f << ":" << l;
    }
    else
    {
        EXPECT_EQ(close_result, 0) << lab << " CREATED: "<< f << ":" << l;
    }
}

}
