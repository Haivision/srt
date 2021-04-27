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

    // Clock drift correction.
    // TsbPD time slowly drift over long period depleting receiver buffer or raising buffering latency
    // Re-evaluate the time adjustment value using a receiver control packet (ACK-ACK).
    // ACK-ACK timestamp is ~RTT/2 ago (in sender's time base).
    // Data packet have origin time stamp which is older when retransmitted so not suitable for this.
    //
    // Every TSBPD_DRIFT_MAX_SAMPLES packets, the average drift is calculated
    // if -TSBPD_DRIFT_MAX_VALUE < avgTsbPdDrift < TSBPD_DRIFT_MAX_VALUE uSec, pass drift value to RcvBuffer to adjust
    // delevery time. if outside this range, adjust this->TsbPdTimeOffset and RcvBuffer->TsbPdTimeBase by
    // +-TSBPD_DRIFT_MAX_VALUE uSec to maintain TsbPdDrift values in reasonable range (-5ms .. +5ms).
    ///

    // Note important thing: this function is being called _EXCLUSIVELY_ in the handler
    // of UMSG_ACKACK command reception. This means that the timestamp used here comes
    // from the CONTROL domain, not DATA domain (timestamps from DATA domain may be
    // either schedule time or a time supplied by the application).

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
    // >>> CTimer::getTime() - ctrlpkt->m_iTimeStamp
    // where ctrlpkt is the packet with SRT_CMD_HSREQ message.
    //
    // This function is called in the HSREQ reception handler only.
    m_tsTsbPdTimeBase = timebase;
    // XXX Seems like this may not work correctly.
    // At least this solution this way won't work with application-supplied
    // timestamps. For that case the timestamps should be taken exclusively
    // from the data packets because in case of application-supplied timestamps
    // they come from completely different server and undergo different rules
    // of network latency and drift.
    m_tdTsbPdDelay = delay;
}

void CTsbpdTime::applyGroupTime(const steady_clock::time_point& timebase,
                                bool                            wrp,
                                uint32_t                        delay,
                                const steady_clock::duration&   udrift)
{
    // Same as setRcvTsbPdMode, but predicted to be used for group members.
    // This synchronizes the time from the INTERNAL TIMEBASE of an existing
    // socket's internal timebase. This is required because the initial time
    // base stays always the same, whereas the internal timebase undergoes
    // adjustment as the 32-bit timestamps in the sockets wrap. The socket
    // newly added to the group must get EXACTLY the same internal timebase
    // or otherwise the TsbPd time calculation will ship different results
    // on different sockets.

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
    // Packet timestamps wrap around every 01h11m35s (32-bit in usec)
    // When added to the peer start time (base time),
    // wrapped around timestamps don't provide a valid local packet delevery time.
    //
    // A wrap check period starts 30 seconds before the wrap point.
    // In this period, timestamps smaller than 30 seconds are considered to have wrapped around (then adjusted).
    // The wrap check period ends 30 seconds after the wrap point, afterwhich time base has been adjusted.

    // This function should generally return the timebase for the given timestamp.
    // It's assumed that the timestamp, for which this function is being called,
    // is received as monotonic clock. This function then traces the changes in the
    // timestamps passed as argument and catches the moment when the 64-bit timebase
    // should be increased by a "segment length" (MAX_TIMESTAMP+1).

    // The checks will be provided for the following split:
    // [INITIAL30][FOLLOWING30]....[LAST30] <-- == CPacket::MAX_TIMESTAMP
    //
    // The following actions should be taken:
    // 1. Check if this is [LAST30]. If so, ENTER TSBPD-wrap-check state
    // 2. Then, it should turn into [INITIAL30] at some point. If so, use carryover MAX+1.
    // 3. Then it should switch to [FOLLOWING30]. If this is detected,
    //    - EXIT TSBPD-wrap-check state
    //    - save the carryover as the current time base.

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
