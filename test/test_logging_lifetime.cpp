#include <sstream>

#include "gtest/gtest.h"

#include "logging.h"
#include "logger_fas.h"

using namespace hvu::logging;

// A Logger destroyed before the code that still uses it must be inert.
TEST(LoggingLifetime, DestroyedLoggerIsDisabled)
{
    LogConfig& config = srt::logging::logger_config();
    const size_t nfa = config.size();

    alignas(Logger) char storage[sizeof(Logger)];
    Logger* lg = new (storage) Logger("lifetime-test", config, true, "TEST");
    lg->~Logger();

    // Memory is still readable, but the dispatcher must be disabled
    // and must stay so after the config updates its subscribers.
    config.set_maxlevel(LogLevel::debug);
    EXPECT_FALSE(lg->Debug.IsEnabled());
    EXPECT_FALSE(lg->Fatal.IsEnabled());
    EXPECT_EQ(config.size(), nfa + 1);
}

TEST(LoggingLifetime, ResetOutputAtExit)
{
    LogConfig& config = srt::logging::logger_config();
    std::ostringstream sout;
    config.set_stream(sout);
    config.reset_output_at_exit();

    EXPECT_FALSE(config.is_output_closed());

    // Output must no longer go to the application's stream
    config.set_maxlevel(LogLevel::warning);
    srt::logging::inlog.Error("must not appear in the app stream");
    EXPECT_TRUE(sout.str().empty());
}

// Independent configurations, and a closed one remains valid and inert.
TEST(LoggingLifetime, IndependentConfigClose)
{
    static LogConfigSingleton si1, si2;
    LogConfig& c1 = si1.instance();
    LogConfig& c2 = si2.instance();
    EXPECT_NE(&c1, &c2);

    Logger lg("indep", c1, true, "IND");
    EXPECT_EQ(c1.size(), 2u); // GENERAL + indep
    EXPECT_EQ(c2.size(), 1u);
    EXPECT_NE(&c1, &srt::logging::logger_config());

    c1.close();
    EXPECT_EQ(c1.size(), 0u);
    EXPECT_TRUE(c1.is_output_closed());
    EXPECT_FALSE(lg.Fatal.IsEnabled());

    // Operations on the closed config must not crash.
    c1.set_maxlevel(LogLevel::debug);
    c1.enable_fa(NULL, 0, false);
    c1.enable_fa("indep", true);
    EXPECT_FALSE(lg.Fatal.IsEnabled());

    // The other one is not affected.
    EXPECT_FALSE(c2.is_output_closed());
    c2.set_maxlevel(LogLevel::debug);
    EXPECT_TRUE(c2.general.Fatal.IsEnabled());
}
