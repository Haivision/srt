/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2021 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#ifndef INC_SRT_TSBPD_TIME_H
#define INC_SRT_TSBPD_TIME_H

#include "platform_sys.h"
#include "sync.h"
#include "utilities.h"

namespace srt
{

class CTsbpdTime
{
    typedef srt::sync::steady_clock  steady_clock;
    typedef steady_clock::time_point time_point;
    typedef steady_clock::duration   duration;
    typedef srt::sync::Mutex         Mutex;

public:
    CTsbpdTime()
        : m_bTsbPdMode(false)
        , m_tdTsbPdDelay(0)
        , m_bTsbPdWrapCheck(false)
    {
    }

    /// Set TimeStamp-Based Packet Delivery Rx Mode
    /// @param [in] timebase localtime base (uSec) of packet time stamps including buffering delay
    /// @param [in] wrap Is in wrapping period
    /// @param [in] delay aggreed TsbPD delay
    void setTsbPdMode(const time_point& timebase, bool wrap, duration delay);

    bool isEnabled() const { return m_bTsbPdMode; }

    void applyGroupTime(const time_point& timebase, bool wrp, uint32_t delay, const duration& udrift);

    void applyGroupDrift(const time_point& timebase, bool wrp, const duration& udrift);

    bool addDriftSample(uint32_t                  pktTimestamp,
                        steady_clock::duration&   w_udrift,
                        steady_clock::time_point& w_newtimebase);

    /// @brief Get packet TSBPD time with buffering delay.
    /// The target time when to deliver the packet to an upstream application.
    /// @param [in] usPktTimestamp 32-bit value of packet timestamp field (microseconds).
    ///
    /// @returns Packet TSBPD base time with buffering delay.
    time_point getPktTsbPdTime(uint32_t usPktTimestamp) const;

    /// @brief Get packet TSBPD base time without buffering delay.
    /// @param [in] usPktTimestamp 32-bit value of packet timestamp field (microseconds).
    ///
    /// @returns Packet TSBPD base time without buffering delay.
    time_point getPktTsbPdBaseTime(uint32_t usPktTimestamp) const;

    /// @brief Get TSBPD base time considering possible carryover
    /// when packet timestamp is overflown and continues from 0.
    /// @param [in] usPktTimestamp 32-bit value of packet timestamp field (microseconds).
    ///
    /// @returns TSBPD base time for a provided packet timestamp.
    time_point getTsbPdTimeBase(uint32_t usPktTimestamp) const;

    void updateTsbPdTimeBase(uint32_t usPktTimestamp);

    int64_t    drift() const { return m_DriftTracer.drift(); }
    int64_t    overdrift() const { return m_DriftTracer.overdrift(); }
    time_point get_time_base() const { return m_tsTsbPdTimeBase; }

    /// @brief Get internal state
    /// @param w_tb TsbPd base time
    /// @param w_udrift drift value
    /// @param w_wrp wrap check
    void getInternalTimeBase(time_point& w_tb, bool& w_wrp, duration& w_udrift) const;

private:
    bool       m_bTsbPdMode;      // Apply receiver buffer latency
    duration   m_tdTsbPdDelay;    // aggreed delay
    time_point m_tsTsbPdTimeBase; // localtime base for TsbPd mode
    // Note: m_tsTsbPdTimeBase cumulates values from:
    // 1. Initial SRT_CMD_HSREQ packet returned value diff to current time:
    //    == (NOW - PACKET_TIMESTAMP), at the time of HSREQ reception
    // 2. Timestamp overflow (@c CRcvBuffer::getTsbPdTimeBase), when overflow on packet detected
    //    += CPacket::MAX_TIMESTAMP+1 (it's a hex round value, usually 0x1*e8).
    // 3. Time drift (CRcvBuffer::addRcvTsbPdDriftSample, executed exclusively
    //    from UMSG_ACKACK handler). This is updated with (positive or negative) TSBPD_DRIFT_MAX_VALUE
    //    once the value of average drift exceeds this value in whatever direction.
    //    += (+/-)CRcvBuffer::TSBPD_DRIFT_MAX_VALUE
    //
    // XXX Application-supplied timestamps won't work therefore. This requires separate
    // calculation of all these things above.

    bool                  m_bTsbPdWrapCheck;                  // true: check packet time stamp wrap around
    static const uint32_t TSBPD_WRAP_PERIOD = (30 * 1000000); // 30 seconds (in usec)

    /// Max drift (usec) above which TsbPD Time Offset is adjusted
    static const int TSBPD_DRIFT_MAX_VALUE = 5000;
    /// Number of samples (UMSG_ACKACK packets) to perform drift caclulation and compensation
    static const int                                            TSBPD_DRIFT_MAX_SAMPLES = 1000;
    DriftTracer<TSBPD_DRIFT_MAX_SAMPLES, TSBPD_DRIFT_MAX_VALUE> m_DriftTracer;

    // Protect simultaneous change of state (read/write).
    mutable Mutex m_mtxRW;
};

} // namespace srt

#endif // INC_SRT_TSBPD_TIME_H
