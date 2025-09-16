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
    int getRate() const { return m_iRateBps; }

    /// Retrieve estimated bitrate in bytes per second (with 16-byte packet header)
    /// including the current sampling interval.
    int getCurrentRate() const;

private:
    static const int NUM_PERIODS        = 10;
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

    int incSampleIdx(int val, int inc = 1) const;

    Sample m_Samples[NUM_PERIODS];

    time_point m_tsFirstSampleTime; //< Start time of the first sample.
    int        m_iFirstSampleIdx;   //< Index of the first sample.
    int        m_iCurSampleIdx;     //< Index of the current sample being collected.
    int        m_iRateBps;          //< Rate in Bytes/sec.
};

// Utility class for bandwidth limitation
class CShaper
{
public:
    static constexpr double SHAPER_RESOLUTION_US   = 1000000.; // micro seconds
    static constexpr double SHAPER_UNIT_BYTE    = 1.; // 1. bytes ; 8. bits
    static constexpr double SHAPER_UNIT_BIT     = 8.; // 1. bytes ; 8. bits
    static constexpr double SHAPER_KBYTES       = 1024.;
    static constexpr double SHAPER_BYTES        = 1.;
    static constexpr double SHAPER_KBITS        = 1000.;
    static constexpr double BURSTPERIOD_DEFAULT = 100;
    static constexpr double MIN_TOKENS          = 1500;

    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration duration;
    CShaper ():
        m_BurstPeriod(sync::milliseconds_from(BURSTPERIOD_DEFAULT)),
        m_rate_kBps(0),
        m_tokens(0),
        m_maxTokens(0)
    {

    }

private:
    duration m_BurstPeriod;
    double m_rate_kBps;      // current_bitrate in kb
    double m_tokens;       // in bytes
    double m_maxTokens;   // in bytes
    time_point m_UpdateTime;
    void setMaxTokens(double tokens)
    {
        //m_maxTokens = Bounds(0, tokens, SHAPER_MTU);
        m_maxTokens = std::max(0., tokens);
    }

    void setTokens(double tokens)
    {
        double newtokens = std::min(tokens, m_maxTokens);

        // It is allowed to set the negative number of tokens,
        // just not less than the negative maximum
        m_tokens = std::max(1 - m_maxTokens, newtokens);
    }

    static double periodToTokens(double rate_Bps, duration period)
    {
        double seconds = double(sync::count_microseconds(period)) / SHAPER_RESOLUTION_US;
        return rate_Bps * seconds / SHAPER_UNIT_BYTE;
    }

    static duration tokensToPeriod(double rate_Bps, double tokens)
    {
        double seconds = tokens / (rate_Bps / SHAPER_UNIT_BYTE);  // B / (B/s) = B * (s/B) = B * s / B = B/B * s = s
        return sync::microseconds_from(seconds * SHAPER_RESOLUTION_US);
    }

    void updateLimits()
    {
        double maxtokens = periodToTokens(m_rate_kBps * SHAPER_KBYTES, m_BurstPeriod);
        if (maxtokens < MIN_TOKENS) // XXX MIN_TOKENS should be taken from SRTO_MSS
        {
            // We have a too small value; recalculate the burst period to reach the minimum.
            duration minperiod = tokensToPeriod(m_rate_kBps * SHAPER_KBYTES, MIN_TOKENS);
            SRT_ASSERT(minperiod > m_BurstPeriod);
            //IF_HEAVY_LOGGING(double prevm = maxtokens);
            maxtokens = periodToTokens(m_rate_kBps * SHAPER_KBYTES, minperiod);
            /*
            HLOGC(qslog.Debug, log << "SHAPER: for period=" << FormatDuration<DUNIT_MS>(m_BurstPeriod)
                    << " maxtokens=" << prevm << " too small - fixing period="
                    << FormatDuration<DUNIT_MS>(minperiod)
                    << " for maxtokens=" << +MIN_TOKENS);
                    */
            m_BurstPeriod = minperiod;
        }
        setMaxTokens(maxtokens);
    }

    public:

    // TOKENS = BITRATE * SHAPER_KBYTES * BURST_PERIOD / (SHAPER_UNIT * SHAPER_RESOLUTION_US)
    // TOKENS * SHAPER_UNIT * SHAPER_RESOLUTION_US = BITRATE * SHAPER_KBYTES * BURST_PERIOD
    // BITRATE = (TOKENS * SHAPER_UNIT * SHAPER_RESOLUTION_US) / (SHAPER_KBYTES * BURST_PERIOD)

    double tokenRate_Bps(double tokens) const
    {
        return (tokens * SHAPER_UNIT_BYTE * SHAPER_RESOLUTION_US) / (SHAPER_BYTES * sync::count_microseconds(m_BurstPeriod));
    }
    double availRate_Bps() const { return tokenRate_Bps(m_tokens); }
    double usedRate_Bps() const { return tokenRate_Bps(m_maxTokens - m_tokens); }

    void setBitrate(double bw_Bps)
    {
        double bw_kBps = bw_Bps / SHAPER_KBYTES;
        if (bw_kBps != m_rate_kBps)
        {
            m_rate_kBps = bw_kBps;
            updateLimits();
        }
    }

    void setOptimisticRTT(int rttval, int rttvar)
    {
        int lowrtt = rttval - rttvar;
        if (lowrtt < 0) // bullshit?
            lowrtt = rttval;

        int stt = lowrtt/2;

        // Make sure that burst period has at least this value
        duration stt_td = sync::microseconds_from(stt);

        // Still, prevent from setting enormous STT values
        if (stt_td < sync::seconds_from(4))
        {
            if (m_BurstPeriod < stt_td)
            {
                m_BurstPeriod = stt_td;
                updateLimits();
            }
        }
    }

    //void setBurstPeriod(duration bp) { if (bp != m_BurstPeriod) { m_BurstPeriod = bp; updateLimits(); }}

    // Note that tick() must be always called after setBitrate.
    void tick(const time_point& now)
    {
        duration delta = now - m_UpdateTime;
        m_UpdateTime = now;

        double update_tokens = periodToTokens(m_rate_kBps * SHAPER_KBYTES, delta);
        setTokens(update_tokens + m_tokens);
    }

    bool enoughTokens(double len) const { return len <= m_tokens; }
    //bool enoughTokens() const { return m_tokens > 0; }
    void consumeTokens(double len) { setTokens(m_tokens - len); }

    // For debug purposes
    int ntokens() const { return m_tokens; }
    duration burstPeriod() const { return m_BurstPeriod; }
    int maxTokens() const { return m_maxTokens; }
};
} // namespace srt

#endif
