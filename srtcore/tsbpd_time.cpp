/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2021 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
#include "tsbpd_time.h"

#include "logging.h"
#include "logger_defs.h"
#include "packet.h"

using namespace srt_logging;
using namespace srt::sync;

namespace srt
{

bool CTsbpdTime::addDriftSample(uint32_t                  usPktTimestamp,
                                steady_clock::duration&   w_udrift,
                                steady_clock::time_point& w_newtimebase)
{
    if (!m_bTsbPdMode)
        return false;
    
    const time_point tsNow = steady_clock::now();

    ScopedLock lck(m_mtxRW);
    const steady_clock::duration tdDrift = tsNow - getPktTsbPdBaseTime(usPktTimestamp);

    const bool updated = m_DriftTracer.update(count_microseconds(tdDrift));

    if (updated)
    {
        IF_HEAVY_LOGGING(const steady_clock::time_point oldbase = m_tsTsbPdTimeBase);
        steady_clock::duration overdrift = microseconds_from(m_DriftTracer.overdrift());
        m_tsTsbPdTimeBase += overdrift;

        HLOGC(brlog.Debug,
              log << "DRIFT=" << FormatDuration(tdDrift) << " AVG=" << (m_DriftTracer.drift() / 1000.0)
                  << "ms, TB: " << FormatTime(oldbase) << " EXCESS: " << FormatDuration(overdrift)
                  << " UPDATED TO: " << FormatTime(m_tsTsbPdTimeBase));
    }
    else
    {
        HLOGC(brlog.Debug,
              log << "DRIFT=" << FormatDuration(tdDrift) << " TB REMAINS: " << FormatTime(m_tsTsbPdTimeBase));
    }

    w_udrift      = tdDrift;
    w_newtimebase = m_tsTsbPdTimeBase;

    return updated;
}

void CTsbpdTime::setTsbPdMode(const steady_clock::time_point& timebase, bool wrap, duration delay)
{
    m_bTsbPdMode      = true;
    m_bTsbPdWrapCheck = wrap;

    // Timebase passed here comes is calculated as:
    // Tnow - hspkt.m_iTimeStamp
    // where hspkt is the packet with SRT_CMD_HSREQ message.
    //
    // This function is called in the HSREQ reception handler only.
    m_tsTsbPdTimeBase = timebase;
    m_tdTsbPdDelay    = delay;
}

void CTsbpdTime::applyGroupTime(const steady_clock::time_point& timebase,
                                bool                            wrp,
                                uint32_t                        delay,
                                const steady_clock::duration&   udrift)
{
    // Same as setTsbPdMode, but predicted to be used for group members.
    // This synchronizes the time from the INTERNAL TIMEBASE of an existing
    // socket's internal timebase. This is required because the initial time
    // base stays always the same, whereas the internal timebase undergoes
    // adjustment as the 32-bit timestamps in the sockets wrap. The socket
    // newly added to the group must get EXACTLY the same internal timebase
    // or otherwise the TsbPd time calculation will ship different results
    // on different member sockets.

    m_bTsbPdMode = true;

    m_tsTsbPdTimeBase = timebase;
    m_bTsbPdWrapCheck = wrp;
    m_tdTsbPdDelay    = microseconds_from(delay);
    m_DriftTracer.forceDrift(count_microseconds(udrift));
}

void CTsbpdTime::applyGroupDrift(const steady_clock::time_point& timebase,
                                 bool                            wrp,
                                 const steady_clock::duration&   udrift)
{
    // This is only when a drift was updated on one of the group members.
    HLOGC(brlog.Debug,
          log << "rcv-buffer: group synch uDRIFT: " << m_DriftTracer.drift() << " -> " << FormatDuration(udrift)
              << " TB: " << FormatTime(m_tsTsbPdTimeBase) << " -> " << FormatTime(timebase));

    m_tsTsbPdTimeBase = timebase;
    m_bTsbPdWrapCheck = wrp;

    m_DriftTracer.forceDrift(count_microseconds(udrift));
}

CTsbpdTime::time_point CTsbpdTime::getTsbPdTimeBase(uint32_t timestamp_us) const
{
    const uint64_t carryover_us =
        (m_bTsbPdWrapCheck && timestamp_us < TSBPD_WRAP_PERIOD) ? uint64_t(CPacket::MAX_TIMESTAMP) + 1 : 0;

    return (m_tsTsbPdTimeBase + microseconds_from(carryover_us));
}

CTsbpdTime::time_point CTsbpdTime::getPktTsbPdTime(uint32_t usPktTimestamp) const
{
    return getPktTsbPdBaseTime(usPktTimestamp) + m_tdTsbPdDelay + microseconds_from(m_DriftTracer.drift());
}

CTsbpdTime::time_point CTsbpdTime::getPktTsbPdBaseTime(uint32_t usPktTimestamp) const
{
    return getTsbPdTimeBase(usPktTimestamp) + microseconds_from(usPktTimestamp);
}

void CTsbpdTime::updateTsbPdTimeBase(uint32_t usPktTimestamp)
{
    if (m_bTsbPdWrapCheck)
    {
        // Wrap check period.
        if ((usPktTimestamp >= TSBPD_WRAP_PERIOD) && (usPktTimestamp <= (TSBPD_WRAP_PERIOD * 2)))
        {
            /* Exiting wrap check period (if for packet delivery head) */
            m_bTsbPdWrapCheck = false;
            m_tsTsbPdTimeBase += microseconds_from(int64_t(CPacket::MAX_TIMESTAMP) + 1);
            LOGC(tslog.Debug,
                 log << "tsbpd wrap period ends with ts=" << usPktTimestamp << " - NEW TIME BASE: "
                     << FormatTime(m_tsTsbPdTimeBase) << " drift: " << m_DriftTracer.drift() << "us");
        }
        return;
    }

    // Check if timestamp is in the last 30 seconds before reaching the MAX_TIMESTAMP.
    if (usPktTimestamp > (CPacket::MAX_TIMESTAMP - TSBPD_WRAP_PERIOD))
    {
        // Approching wrap around point, start wrap check period (if for packet delivery head)
        m_bTsbPdWrapCheck = true;
        LOGC(tslog.Debug,
             log << "tsbpd wrap period begins with ts=" << usPktTimestamp << " drift: " << m_DriftTracer.drift()
                 << "us.");
    }
}

void CTsbpdTime::getInternalTimeBase(time_point& w_tb, bool& w_wrp, duration& w_udrift) const
{
    ScopedLock lck(m_mtxRW);
    w_tb     = m_tsTsbPdTimeBase;
    w_udrift = microseconds_from(m_DriftTracer.drift());
    w_wrp    = m_bTsbPdWrapCheck;
}

} // namespace srt
