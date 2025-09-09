/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
Copyright (c) 2001 - 2009, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 05/05/2009
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_BUFFER_TOOLS_H
#define INC_SRT_BUFFER_TOOLS_H

#include "common.h"

namespace srt
{

/// The AvgBufSize class is used to calculate moving average of the buffer (RCV or SND)
class AvgBufSize
{
    typedef sync::steady_clock::time_point time_point;

public:
    AvgBufSize()
        : m_dBytesCountMAvg(0.0)
        , m_dCountMAvg(0.0)
        , m_dTimespanMAvg(0.0)
    {
    }

public:
    bool isTimeToUpdate(const time_point& now) const;
    void update(const time_point& now, int pkts, int bytes, int timespan_ms);

public:
    inline double pkts() const { return m_dCountMAvg; }
    inline double timespan_ms() const { return m_dTimespanMAvg; }
    inline double bytes() const { return m_dBytesCountMAvg; }

private:
    time_point m_tsLastSamplingTime;
    double     m_dBytesCountMAvg;
    double     m_dCountMAvg;
    double     m_dTimespanMAvg;
};

/// The class to estimate source bitrate based on samples submitted to the buffer.
/// Is currently only used by the CSndBuffer.
class CRateEstimator
{
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration   duration;
public:
    CRateEstimator(int family);

public:
    uint64_t getInRatePeriod() const { return m_InRatePeriod; }

    /// Retrieve input bitrate in bytes per second
    int getInputRate() const { return m_iInRateBps; }

    void setInputRateSmpPeriod(int period);

    /// Update input rate calculation.
    /// @param [in] time   current time
    /// @param [in] pkts   number of packets newly added to the buffer
    /// @param [in] bytes  number of payload bytes in those newly added packets
    void updateInputRate(const time_point& time, int pkts = 0, int bytes = 0);

    void resetInputRateSmpPeriod(bool disable = false) { setInputRateSmpPeriod(disable ? 0 : INPUTRATE_FAST_START_US); }

private:                                                       // Constants
    static const uint64_t INPUTRATE_FAST_START_US   = 500000;  //  500 ms
    static const uint64_t INPUTRATE_RUNNING_US      = 1000000; // 1000 ms
    static const int64_t  INPUTRATE_MAX_PACKETS     = 2000;    // ~ 21 Mbps of 1316 bytes payload
    static const int      INPUTRATE_INITIAL_BYTESPS = BW_INFINITE;

private:
    int        m_iInRatePktsCount;  // number of payload packets added since InRateStartTime.
    int        m_iInRateBytesCount; // number of payload bytes added since InRateStartTime.
    time_point m_tsInRateStartTime;
    uint64_t   m_InRatePeriod; // usec
    int        m_iInRateBps;   // Input Rate in Bytes/sec
    int        m_iFullHeaderSize;
};


class CSndRateEstimator
{
    typedef sync::steady_clock::time_point time_point;

public:
    CSndRateEstimator(const time_point& tsNow);

    /// Add sample.
    /// @param [in] time   sample (sending) time.
    /// @param [in] pkts   number of packets in the sample.
    /// @param [in] bytes  number of payload bytes in the sample.
    void addSample(const time_point& time, int pkts = 0, size_t bytes = 0);

    /// Retrieve estimated bitrate in bytes per second with 16-byte packet header.
    int getRate(const time_point &now);

private:
    static const int NUM_PERIODS        = 11;
    static const int SAMPLE_DURATION_MS = 100; // 100 ms
    struct Sample
    {
        int m_iPktsCount;  // number of payload packets
        int m_iBytesCount; // number of payload bytes

        void reset()
        {
            m_iPktsCount  = 0;
            m_iBytesCount = 0;
        }

        Sample()
            : m_iPktsCount(0)
            , m_iBytesCount(0)
        {
        }

        Sample(int iPkts, int iBytes)
            : m_iPktsCount(iPkts)
            , m_iBytesCount(iBytes)
        {
        }

        Sample operator+(const Sample& other)
        {
            return Sample(m_iPktsCount + other.m_iPktsCount, m_iBytesCount + other.m_iBytesCount);
        }

        Sample& operator+=(const Sample& other)
        {
            *this = *this + other;
            return *this;
        }

        
        bool empty() const { return m_iPktsCount == 0; }
    };

    int indexForTime(const time_point &now) { return ((int) count_milliseconds(now - m_tsFirstSampleTime) / SAMPLE_DURATION_MS) % NUM_PERIODS;}
    int incSampleIdx(int val, int inc = 1) const;
    void reset(const time_point& now);
    void cleanup(const time_point& now);

    Sample m_Samples[NUM_PERIODS];

    time_point m_tsFirstSampleTime; //< Start time of the first sample.
    time_point m_tsSampleTime;      //< Last sample time.
};

class CShaper 
{
    public: 
    static constexpr double SHAPER_RESOLUTION   = 1000000.; // micro seconds
    static constexpr double SHAPER_UNIT         = 1.; // 1. bytes ; 8. bits
    static constexpr double SHAPER_MTU          = 1500.;
    static constexpr double SHAPER_KB           = 1000.;
    static constexpr double BURSTPERIOD_DEFAULT = 10;
    static constexpr double INITIAL_TOKENS = SHAPER_MTU * SHAPER_UNIT;

    typedef sync::steady_clock::time_point time_point;
    CShaper () 
        : m_BurstPeriod_ms(BURSTPERIOD_DEFAULT)
          , m_bitrate(0)
          , m_tokens(INITIAL_TOKENS)
          , m_maxTokens(INITIAL_TOKENS)
    {

    }
    private:  
    double m_BurstPeriod_ms; // in ms
    double m_bitrate;      // current_bitrate in kb
    double m_tokens;       // in bytes
    double m_maxTokens;   // in bytes
    time_point m_time;
    void setMaxTokens(double tokens) { m_maxTokens = std::max<double>(SHAPER_MTU, tokens); }
    void setTokens(double tokens) { m_tokens = std::min<double>(std::max<double>(m_maxTokens, 0.), tokens); }
    void updateMaxTokens() { setMaxTokens((m_bitrate * SHAPER_KB * m_BurstPeriod_ms) / (SHAPER_UNIT * SHAPER_RESOLUTION)); }
    public:

    // TOKENS = BITRATE * SHAPER_KB * BURST_PERIOD / (SHAPER_UNIT * SHAPER_RESOLUTION)
    // TOKENS * SHAPER_UNIT * SHAPER_RESOLUTION = BITRATE * SHAPER_KB * BURST_PERIOD
    // BITRATE = (TOKENS * SHAPER_UNIT * SHAPER_RESOLUTION) / (SHAPER_KB * BURST_PERIOD)

    double tokenRate(double tokens) const { return (tokens * SHAPER_UNIT * SHAPER_RESOLUTION) / (SHAPER_KB * m_BurstPeriod_ms); }
    double availRate() const { return tokenRate(m_tokens); }
    double usedRate() const { return tokenRate(m_maxTokens - m_tokens); }

    void setBitrate(double bw) { if (bw != m_bitrate) { m_bitrate = bw ; updateMaxTokens(); }}
    void setBurstPeriod(double bp) { if (bp != m_BurstPeriod_ms) { m_BurstPeriod_ms = bp ; updateMaxTokens(); }}
    bool check(double len) { return len > m_tokens; }
    void tick(const time_point &now) { double delta = (double) count_microseconds(now - m_time); m_time = now ; setTokens((m_bitrate * delta) / (SHAPER_UNIT * SHAPER_RESOLUTION));}
    void update(double len) { setTokens(m_tokens - len); }

    // For debug purposes
    size_t ntokens() const { return m_tokens; }

    bool consume(double tokens)
    {
        if (!check(tokens))
            return false;
        update(tokens);
        return true;
    }
};
} // namespace srt

#endif
