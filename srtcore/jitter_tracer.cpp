/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2020 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include "jitter_tracer.h"

namespace srt {
using namespace sync;


void CJitterTracer::onDataPktArrival(const CPacket& pkt, const time_point& tsbpdBaseTime)
{
    const uint32_t timestamp_us = pkt.getMsgTimeStamp();

    // RFC 3550 suggests to calculate the relative transit time.
    // The relative transit time is the difference between a packet's
    // timestamp and the receiver's clock at the time of arrival,
    // measured in the same units.
    // SRT Data packet does not have an absolute time, the relative time is used instead.
    // The timestamp of SRT data packet added to the TSBPD base time is the prediction
    // of the current time on the receiver.
    // Therefore we calculate the difference between the prediction and the actual value.
    // Note the measure difference also includes packet processing delay.
    const steady_clock::duration delay =
        steady_clock::now() - (tsbpdBaseTime + microseconds_from(timestamp_us));

    const uint64_t di = abs(count_microseconds(delay - m_dPrevArrivalDelay));
    m_uiJitter = avg_iir<16>(m_uiJitter, di);
    m_dPrevArrivalDelay = delay;
}

void CJitterTracer::onDataPktDelivery(const time_point& tsbpdTargetTime)
{
    // RFC 3550 suggests to calculate the relative transit time.
    // The relative transit time is the difference between a packet's
    // timestamp and the receiver's clock at the time of arrival,
    // measured in the same units.
    // SRT Data packet does not have an absolute time, the relative time is used instead.
    // The timestamp of SRT data packet added to the TSBPD base time is the prediction
    // of the current time on the receiver.
    // Therefore we calculate the difference between the prediction and the actual value.
    // Note the measure difference also includes packet processing delay.
    const steady_clock::duration delay =
        steady_clock::now() - tsbpdTargetTime;

    const uint64_t di = abs(count_microseconds(delay - m_dPrevDeliverDelay));
    m_uiDeliverJitter = avg_iir<16>(m_uiDeliverJitter, di);
    m_dPrevDeliverDelay = delay;
}

void CJitterTracer::onDataPktSent(const time_point& tsPktOrigin)
{
    const steady_clock::duration delay =
        steady_clock::now() - tsPktOrigin;

    const uint64_t di = abs(count_microseconds(delay - m_dPrevSendDelay));
    m_uiSendJitter = avg_iir<16>(m_uiSendJitter, di);
    m_dPrevSendDelay = delay;
}

} // namespace srt
