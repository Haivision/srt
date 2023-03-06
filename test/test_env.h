#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include "gtest/gtest.h"

class SrtTestEnv: public testing::Environment
{
public:
    static SrtTestEnv* me;

    std::vector<std::string> args;
    std::map<std::string, std::vector<std::string>> argmap;

    explicit SrtTestEnv(int argc, char** argv)
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
};
