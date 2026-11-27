/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2026 maxolgi.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

// Microbenchmark of CChannel send dispatch: a virtual sendto via the base
// pointer vs a statically bound call on the same object. The no-op
// callback keeps OS datagram and syscall cost out of the measurement.
// Timings are reported; DISPATCH_CEILING is the sole assertion. Run:
// SRT_RUN_BENCH=1 test-srt --gtest_filter='*ChannelBench*' - full CI
// legs, emulated runners included, must not pay the measurement time.

#include "gtest/gtest.h"
#include "test_env.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>

#include "srt.h"

#include "channel.h" // CChannel, CChannelVirtual
#include "common.h"  // CNetworkInterface
#include "packet.h"  // CPacket

namespace
{

// The payload of the packet every call "sends": a real payload block,
// small enough to keep the rig light. Its bytes travel nowhere - the
// send callback is a no-op.
const size_t PAYLOAD_SIZE = 64;

// Every arm is measured ROUNDS times, CALLS_PER_ROUND calls each, after
// one unmeasured warm-up round per arm. The best round of an arm is its
// reported figure: a scheduler hiccup can only worsen, never improve, a
// reported number. A round must span ~100 ms (at ~10 ns/call) so a
// single favorable scheduling window cannot dominate a whole round's
// figure, while the whole test stays in the low seconds.
const int CALLS_PER_ROUND = 10000000;
const int ROUNDS          = 3;

// Regression ceiling: sendto through the base pointer may cost at most
// DISPATCH_CEILING times the qualified call. The nominal delta stays
// within a few percent, so a breach means real per-call work entered
// the dispatch path, not noise in the best-of figures.
const double DISPATCH_CEILING = 1.5;

// The sink that publishes the accumulated send results out of the
// compiler's reach, so the send loops cannot be discarded as dead code.
volatile long long g_bench_sink = 0;

// How many calls actually reached the send callback - the execution
// sanity evidence for both arms.
long long g_callback_calls = 0;

// The no-op owner transport: the send callback returns the exact total
// it was handed (the success path of the full-length-or--1 contract)
// without touching the datagram. waitread and deliver are never invoked.
int BenchSend(void*, const struct sockaddr*, int, const SRT_VTBuf* vec, int nvec)
{
    ++g_callback_calls;
    int total = 0;
    for (int i = 0; i < nvec; ++i)
        total += vec[i].len;
    return total;
}

SRT_VTWAIT BenchWaitRead(void*, int)
{
    return SRT_VTWAIT_TIMEOUT;
}

int BenchDeliver(void*, struct sockaddr*, int*, SRT_VTBuf*, int)
{
    return -1;
}

} // namespace

class TestChannelBench: public srt::Test
{
protected:
    void setup() override {}

    void teardown() override {}
};

TEST_F(TestChannelBench, SendtoDispatchCost)
{
    // Opt-in gate: without the flag the test skips rather than make
    // every default test run pay the ~80M-call measurement.
    const char* run_bench = ::getenv("SRT_RUN_BENCH");
    if (run_bench == NULL || std::string(run_bench) != "1")
        GTEST_SKIP() << "SRT_RUN_BENCH is not 1 - skipping the dispatch benchmark";

    using namespace srt;
    using clock = std::chrono::steady_clock;

    // The channel under measurement. The label and the destination
    // address are arbitrary zero IPv4 addresses: nothing is ever bound
    // or sent in the OS, and the send callback ignores both.
    SRT_VirtualTransportConfig vt_cfg;
    vt_cfg.send           = &BenchSend;
    vt_cfg.waitread       = &BenchWaitRead;
    vt_cfg.deliver        = &BenchDeliver;
    vt_cfg.opaq           = NULL;
    vt_cfg.initial_rtt_ms = 0;

    const sockaddr_any label(AF_INET);
    CChannelVirtual channel(vt_cfg, label, false);

    // The packet every call hands to sendto: the fixed 16-byte header
    // block plus a payload block. With a zeroed header it is a data
    // packet, so sendto's toNetworkByteOrder/toHostByteOrder round trip
    // swaps only the four header words and restores them exactly - every
    // iteration of both arms sees the identical packet state.
    CPacket packet;
    packet.allocate(PAYLOAD_SIZE);
    packet.setLength(PAYLOAD_SIZE);
    for (size_t i = 0; i < PAYLOAD_SIZE; ++i)
        packet.getData()[i] = (char)(i & 0xff);

    const sockaddr_any     dst(AF_INET);
    const CNetworkInterface no_source;

    // Volatile laundering pointers: the compiler must reload them and
    // cannot devirtualize. Both arms pay the same volatile load, so
    // their delta is the dispatch cost alone. The direct arm must
    // qualify the call - the override is not final.
    CChannelVirtual* volatile via_direct = &channel;
    CChannel* volatile        via_base   = &channel;

    // What every call must report: the full datagram, header plus
    // payload, taken by the callback as a whole.
    const int expected_total = (int)(CPacket::HDR_SIZE + PAYLOAD_SIZE);

    // One measurement: CALLS_PER_ROUND timed calls of the given send
    // closure. Reports ns/call and the number of calls that did not
    // return the full datagram size.
    struct ArmRun
    {
        double ns_per_call;
        int    failures;
    };
    const auto measure = [&](auto&& send) {
        long long acc       = 0;
        int       failures  = 0;
        const clock::time_point t0 = clock::now();
        for (int i = 0; i < CALLS_PER_ROUND; ++i)
        {
            const int res = send();
            acc += res;
            if (res != expected_total)
                ++failures;
        }
        const clock::time_point t1 = clock::now();
        const long long total_ns   = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        g_bench_sink = acc;
        return ArmRun{ double(total_ns) / double(CALLS_PER_ROUND), failures };
    };

    double ns_direct[ROUNDS];
    double ns_virtual[ROUNDS];
    int    fails_direct = 0, fails_virtual = 0;

    // Warm-up round per arm (page faults, caches, branch predictors); its
    // sends still count for the sanity checks, its timing does not.
    const ArmRun warm_direct  = measure([&] { return via_direct->CChannelVirtual::sendto(dst, packet, no_source); });
    const ArmRun warm_virtual = measure([&] { return via_base->sendto(dst, packet, no_source); });
    fails_direct += warm_direct.failures;
    fails_virtual += warm_virtual.failures;

    for (int r = 0; r < ROUNDS; ++r)
    {
        const ArmRun run_direct  = measure([&] { return via_direct->CChannelVirtual::sendto(dst, packet, no_source); });
        const ArmRun run_virtual = measure([&] { return via_base->sendto(dst, packet, no_source); });
        ns_direct[r]  = run_direct.ns_per_call;
        ns_virtual[r] = run_virtual.ns_per_call;
        fails_direct += run_direct.failures;
        fails_virtual += run_virtual.failures;
    }

    // Execution sanity: both arms ran all their calls - each reached
    // the send callback - and every call returned the full datagram
    // size. With no OS entity involved this is deterministic.
    const long long calls_per_arm = (ROUNDS + 1) * (long long)CALLS_PER_ROUND;
    EXPECT_EQ(g_callback_calls, 2 * calls_per_arm);
    EXPECT_EQ(fails_direct, 0);
    EXPECT_EQ(fails_virtual, 0);
    EXPECT_NE(g_bench_sink, 0);

    // The best round of every arm, the numbers the report carries.
    const auto best_of = [&](const double (&ns)[ROUNDS]) {
        double b = ns[0];
        for (int r = 1; r < ROUNDS; ++r)
            if (ns[r] < b)
                b = ns[r];
        return b;
    };
    const double best_direct  = best_of(ns_direct);
    const double best_virtual = best_of(ns_virtual);
    const double delta        = best_virtual - best_direct;

    // The regression bound, on the same best-of figures the report
    // carries: scheduler hiccups can only worsen them, never improve
    // them, so the ceiling separates a real dispatch regression from
    // run-to-run noise.
    EXPECT_LT(best_virtual, best_direct * DISPATCH_CEILING);

    std::ostringstream rep;
    rep << "\n"
        << "channel sendto dispatch cost, ns/call (best of " << ROUNDS << " rounds of " << CALLS_PER_ROUND
        << " calls per arm, no-syscall rig)\n";
    for (int r = 0; r < ROUNDS; ++r)
        rep << "  round " << r << ": direct=" << ns_direct[r] << "  virtual=" << ns_virtual[r]
            << "  delta=" << (ns_virtual[r] - ns_direct[r]) << " ns\n";
    rep << "  direct  (qualified CChannelVirtual::sendto):        " << best_direct << " ns\n"
        << "  virtual (sendto through CChannel*):                 " << best_virtual << " ns\n"
        << "  virtual - direct = " << delta << " ns ("
        << (best_direct > 0 ? 100.0 * delta / best_direct : 0.0)
        << "% - either sign is a real result; only the virtual < "
        << DISPATCH_CEILING << " x direct ceiling is asserted)\n"
        << "  send callback reached " << g_callback_calls << " times; failed sends: direct=" << fails_direct
        << " virtual=" << fails_virtual;
    // Printed straight to stdout: gtest does not display SUCCEED()
    // messages by default, and these numbers are the point of the test.
    std::cout << rep.str() << std::endl;
}
