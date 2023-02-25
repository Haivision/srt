#ifndef INC_SRT_TESTENV_H
#define INC_SRT_TESTENV_H

namespace srt
{
class TestEnv
{
private:
    int srtStartupVal;

    int testEnvSetup();
    int testEnvTearDown();

public:
    
    TestEnv();
    ~TestEnv();

    // Exposes return value from srt_startup() call in testEnvSetup() 
    int getSrtStartupVal();

};

} //namespace

#endif