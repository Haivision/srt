#include <iostream>

#include "srt.h"
#include "test_env.h"

int srt::TestEnv::testEnvSetup()
{
    return srt_startup();
}

int srt::TestEnv::testEnvTearDown()
{
    return srt_cleanup();
}

srt::TestEnv::TestEnv() : srtStartupVal{testEnvSetup()}
{

}

srt::TestEnv::~TestEnv()
{
    testEnvTearDown();
}

int srt::TestEnv::getSrtStartupVal()
{
    return srtStartupVal;
}