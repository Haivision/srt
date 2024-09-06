#ifndef INC_SRT_TESTENV_H
#define INC_SRT_TESTENV_H

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include "gtest/gtest.h"


namespace srt
{
class TestEnv: public testing::Environment
{
public:
    static TestEnv* me;
    std::vector<std::string> args;
    std::map<std::string, std::vector<std::string>> argmap;

    explicit TestEnv(int argc, char** argv)
        : args(argv+1, argv+argc)
    {
        if (me)
            throw std::invalid_argument("singleton");

        me = this;
        FillArgMap();
    }

    void FillArgMap();

    bool OptionPresent(const std::string& key)
    {
        return argmap.count(key) > 0;
    }

    std::string OptionValue(const std::string& key);

    // Specific test environment options
    // All must be static, return bool. Arguments allowed.
    // The name must start with Allowed_.
    static bool Allowed_IPv6();
};

#define SRTST_REQUIRES(feature,...) if (!srt::TestEnv::Allowed_##feature(__VA_ARGS__)) { return; }


class TestInit
{
public:
    int ninst;

    static void start(int& w_retstatus);
    static void stop();

    TestInit() { start((ninst)); }
    ~TestInit() { stop(); }

    void HandlePerTestOptions();

};

class UniqueSocket
{
    int32_t sock;
    std::string lab, f;
    int l;

public:
    UniqueSocket(int32_t s, const char* label, const char* file, int line): sock(s)
    {
        if (s == -1)
            throw std::invalid_argument("Invalid socket");
        lab = label;
        f = file;
        l = line;
    }

#define MAKE_UNIQUE_SOCK(name, label, expr) srt::UniqueSocket name (expr, label, __FILE__, __LINE__)

    UniqueSocket(): sock(-1), l(0)
    {
    }

    void close();
    ~UniqueSocket();

    operator int32_t() const
    {
        return sock;
    }

    int32_t& ref() { return sock; }

    /*
       IF NEEDED, MOVE to test_main.cpp
    UniqueSocket& operator=(int32_t s)
    {
        if (sock == s)
            return;
        srt_close(sock);
        sock = s;
    }
    */
};

class Test: public testing::Test
{
    std::unique_ptr<TestInit> init_holder;
public:

    virtual void setup() = 0;
    virtual void teardown() = 0;

    void SetUp() override final
    {
        init_holder.reset(new TestInit);
        init_holder->HandlePerTestOptions();
        setup();
    }

    void TearDown() override final
    {
        teardown();
        init_holder.reset();
    }
};

struct sockaddr_any CreateAddr(const std::string& name, unsigned short port, int pref_family);

} //namespace

#endif
