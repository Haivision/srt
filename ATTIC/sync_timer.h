#include "sync.h" // Requires CEvent

namespace srt
{
namespace sync
{

class CTimer
{
public:
    CTimer();
    ~CTimer();

public:
    /// Causes the current thread to block until
    /// the specified time is reached.
    /// Sleep can be interrupted by calling interrupt()
    /// or woken up to recheck the scheduled time by tick()
    /// @param tp target time to sleep until
    ///
    /// @return true  if the specified time was reached
    ///         false should never happen
    bool sleep_until(steady_clock::time_point tp);

    /// Resets target wait time and interrupts waiting
    /// in sleep_until(..)
    void interrupt();

    /// Wakes up waiting thread (sleep_until(..)) without
    /// changing the target waiting time to force a recheck
    /// of the current time in comparison to the target time.
    void tick();

private:
    CEvent m_event;
    sync::AtomicClock<steady_clock> m_tsSchedTime;

    void wait_busy();
    void wait_stalled();
};


}
}
