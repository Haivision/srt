#include <string>
#include <vector>
#include <stdexcept>
#include "gtest/gtest.h"

class SrtTestEnv: public testing::Environment
{
public:
    static SrtTestEnv* me;

    std::vector<std::string> args;
    explicit SrtTestEnv(int argc, char** argv)
        : args(argv+1, argv+argc)
    {
        if (me)
            throw std::invalid_argument("singleton");

        me = this;
    }
};
