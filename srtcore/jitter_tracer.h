/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2020 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
#pragma once
#ifndef INC_JITTER_TRACER_H
#define INC_JITTER_TRACER_H

#include "packet.h"
#include "sync.h"

namespace srt {

class CJitterTracer
{
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration duration;

public:
    CJitterTracer()
        : m_uiJitter(0)
    {}

public:
    void onDataPktArrival(const CPacket& pkt, const time_point& tsbpdBaseTime);
    void onDataPktDelivery(const time_point& tsbpdTargetTime);
    void onDataPktSent(const time_point& tsPktOrigin);

    uint64_t jitter() const { return m_uiJitter; }
    uint64_t deliveryJitter() const { return m_uiDeliverJitter; }
    uint64_t sendingJitter() const { return m_uiSendJitter; }

private:
    duration m_dPrevArrivalDelay;
    duration m_dPrevDeliverDelay;
    duration m_dPrevSendDelay;
    uint64_t m_uiJitter;
    uint64_t m_uiDeliverJitter;
    uint64_t m_uiSendJitter;
};


} // namespace srt

#endif // INC_JITTER_TRACER_H
