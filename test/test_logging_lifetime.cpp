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
