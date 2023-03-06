#include <string>
#include <iterator>
#include <vector>

#include "gtest/gtest.h"
#include "test_env.h"

using namespace std;

SrtTestEnv* SrtTestEnv::me = 0;

int main(int argc, char **argv)
{
    cout << "CUSTOM MAIN\n";

    string command_line_arg(argc == 2 ? argv[1] : "");
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new SrtTestEnv(argc, argv));
    return RUN_ALL_TESTS();
}
