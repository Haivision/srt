#include "sync_timer.h"

namespace srt
{
namespace sync
{


////////////////////////////////////////////////////////////////////////////////
//
// Timer
//
////////////////////////////////////////////////////////////////////////////////

CTimer::CTimer()
{
}


CTimer::~CTimer()
{
}

// This function sleeps up to the given time, then exits.
// Meanwhile it can be influenced from another thread by calling:
// - tick(): exit waiting, but re-check the end time and fall back to sleep if not reached
// - interrupt(): exit waiting with setting wait time to now() so that it exits immediately
//
// This function returns true if it has exit on the originally set time.
// If the time was changed due to being interrupted and it did really exit before
// that time, false is returned.
bool CTimer::sleep_until(TimePoint<steady_clock> tp)
{
    // The class member m_sched_time can be used to interrupt the sleep.
    // Refer to Timer::interrupt().
    enterCS(m_event.mutex());
    m_tsSchedTime = tp;
    leaveCS(m_event.mutex());

#if SRT_BUSY_WAITING
    wait_busy();
#else
    wait_stalled();
#endif

    // Returning false means that sleep was early interrupted
    return m_tsSchedTime.load() >= tp;
}

void CTimer::wait_stalled()
{
    TimePoint<steady_clock> cur_tp = steady_clock::now();
    {
        UniqueLock elk (m_event.mutex());
        while (cur_tp < m_tsSchedTime.load())
        {
            m_event.wait_until(elk, m_tsSchedTime);
            cur_tp = steady_clock::now();
        }
    }
}

void srt::sync::CTimer::wait_busy()
{
#if defined(_WIN32)
    // 10 ms on Windows: bad accuracy of timers
    const steady_clock::duration
        td_threshold = milliseconds_from(10);
#else
    // 1 ms on non-Windows platforms
    const steady_clock::duration
        td_threshold = milliseconds_from(1);
#endif

    TimePoint<steady_clock> cur_tp = steady_clock::now();
    {
        UniqueLock elk (m_event.mutex());
        while (cur_tp < m_tsSchedTime.load())
        {
            steady_clock::duration td_wait = m_tsSchedTime.load() - cur_tp;
            if (td_wait <= 2 * td_threshold)
                break;

            td_wait -= td_threshold;
            m_event.wait_for(elk, td_wait);

            cur_tp = steady_clock::now();
        }

        while (cur_tp < m_tsSchedTime.load())
        {
            InvertedLock ulk (m_event.mutex());
#ifdef IA32
            __asm__ volatile ("pause; rep; nop; nop; nop; nop; nop;");
#elif IA64
            __asm__ volatile ("nop 0; nop 0; nop 0; nop 0; nop 0;");
#elif AMD64
            __asm__ volatile ("nop; nop; nop; nop; nop;");
#elif defined(_WIN32) && !defined(__MINGW32__)
            __nop();
            __nop();
            __nop();
            __nop();
            __nop();
#endif
            cur_tp = steady_clock::now();
        }
    }
}


void CTimer::interrupt()
{
    UniqueLock lck(m_event.mutex());
    m_tsSchedTime = steady_clock::now();
    m_event.notify_all();
}


void CTimer::tick()
{
    m_event.notify_one();
}

}
}
