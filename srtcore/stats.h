/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2021 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_STATS_H
#define INC_SRT_STATS_H

#include <cstdint>
#include <cstddef>

#include "packet.h"

namespace srt
{
namespace stats
{

class Packets
{
public:
    Packets() : m_count(0) {}

    Packets(uint32_t num) : m_count(num) {}

    void reset()
    {
        m_count = 0;
    }

    Packets& operator+= (const Packets& other)
    {
        m_count   += other.m_count;
        return *this;
    }

    uint32_t count() const 
    {
        return m_count;
    }

private:
    uint32_t m_count;
};

class BytesPackets
{
public:
    BytesPackets()
        : m_bytes(0)
        , m_packets(0)
    {}

    BytesPackets(uint64_t bytes, size_t n = 1)
        : m_bytes(bytes)
        , m_packets(n)
    {}

    BytesPackets& operator+= (const BytesPackets& other)
    {
        m_bytes   += other.m_bytes;
        m_packets += other.m_packets;
        return *this;
    }

public:
    void reset()
    {
        m_packets = 0;
        m_bytes = 0;
    }

    void count(uint64_t bytes, size_t n = 1)
    {
        m_packets += (uint32_t) n;
        m_bytes += bytes;
    }

    uint64_t bytes() const 
    {
        return m_bytes;
    }

    uint32_t count() const 
    {
        return m_packets;
    }

    uint64_t bytesWithHdr() const
    {
        static const int PKT_HDR_SIZE = CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE;
        return m_bytes + m_packets * PKT_HDR_SIZE;
    }

private:
    uint64_t m_bytes;
    uint32_t m_packets;
};

template <class METRIC_TYPE>
struct Metric
{
    METRIC_TYPE trace;
    METRIC_TYPE total;

    void count(METRIC_TYPE val)
    {
        trace += val;
        total += val;
    }

    void reset()
    {
        trace.reset();
        total.reset();
    }

    void resetTrace()
    {
        trace.reset();
    }
};

} // namespace stats
} // namespace srt

#endif // INC_SRT_STATS_H


