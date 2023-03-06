#include <string>
#include <iterator>
#include <vector>
#include <sstream>

#include "gtest/gtest.h"
#include "test_env.h"

using namespace std;

SrtTestEnv* SrtTestEnv::me = 0;

int main(int argc, char **argv)
{
    string command_line_arg(argc == 2 ? argv[1] : "");
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new SrtTestEnv(argc, argv));
    return RUN_ALL_TESTS();
}

void SrtTestEnv::FillArgMap()
{
    // The rule is:
    // - first arguments go to an empty string key
    // - if an argument has - in the beginning, name the key
    // - key followed by args collected in a list
    // - double dash prevents interpreting further args as option keys

    string key;
    bool expectkey = true;

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
                    key = a.substr(1);

                continue;
            }
        }
        argmap[key].push_back(a);
    }

    return;
}


std::string SrtTestEnv::OptionValue(const std::string& key)
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
