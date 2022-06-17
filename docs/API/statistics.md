
# SRT Statistics

1. [SRT Socket Statistics](#srt-socket-statistics)
   - [Summary Table](#summary-table)
   - [Accumulated Statistics](#accumulated-statistics)
   - [Interval-Based Statistics](#interval-based-statistics)
   - [Instantaneous Statistics](#instantaneous-statistics)
2. [SRT Group Statistics](#srt-group-statistics)
   - [Summary Table](#group-summary-table)
   - [Accumulated Statistics](#group-accumulated-statistics)
   - [Interval-Based Statistics](#group-interval-based-statistics)
   - [Formulas](#group-formulas)

## SRT Socket Statistics

SRT provides a powerful set of statistical data on a socket. This data can be used to keep an eye on a socket's health and track faulty behavior.

Statistics are calculated independently on each side (receiver and sender) and are not exchanged between peers unless explicitly stated.

The following API functions can be used to retrieve statistics on an SRT socket:

* `int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear)`
* `int srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`

Refer to the documentation of the [SRT API Functions](API-functions.md) for usage instructions.

### Summary Table

The table below provides a summary of SRT socket statistics: name, type, unit of measurement, data type, and whether it is calculated by the sender or receiver.

There are three types of statistics:

- **Accumulated:** the statistic is accumulated since the time an SRT socket has been created (after the successful call to `srt_connect(...)` or `srt_bind(...)` function), e.g., [pktSentTotal](#pktSentTotal), etc.,
- **Interval-based:** the statistic is accumulated during a specified time interval (e.g., 100 milliseconds if SRT statistics is collected each 100 milliseconds) from the time an SRT socket has been created, e.g., [pktSent](#pktSent), etc. The value of the statistic can be reset by calling the `srt_bstats(..., int clear)` function with `clear = 1`, 
- **Instantaneous:** the statistic is obtained at the moment the `srt_bistats()` function is called, e.g., [msRTT](#msRTT), etc.

See sections [Accumulated Statistics](#accumulated-statistics), [Interval-Based Statistics](#interval-based-statistics), and [Instantaneous Statistics](#instantaneous-statistics) for a detailed description of each statistic.


| Statistic                                           | Type of Statistic | Unit of Measurement | Available for Sender | Available for Receiver | Data Type |
| --------------------------------------------------- | ----------------- | ------------------- | -------------------- | ---------------------- | --------- |
| [msTimeStamp](#msTimeStamp)                         | accumulated       | ms (milliseconds)   | ✓                    | ✓                      | int64_t   |
| [pktSentTotal](#pktSentTotal)                       | accumulated       | packets             | ✓                    | -                      | int64_t   |
| [pktRecvTotal](#pktRecvTotal)                       | accumulated       | packets             | -                    | ✓                      | int64_t   |
| [pktSentUniqueTotal](#pktSentUniqueTotal)           | accumulated       | packets             | ✓                    | -                      | int64_t   |
| [pktRecvUniqueTotal](#pktRecvUniqueTotal)           | accumulated       | packets             | -                    | ✓                      | int64_t   |
| [pktSndLossTotal](#pktSndLossTotal)                 | accumulated       | packets             | ✓                    | -                      | int32_t   |
| [pktRcvLossTotal](#pktRcvLossTotal)                 | accumulated       | packets             | -                    | ✓                      | int32_t   |
| [pktRetransTotal](#pktRetransTotal)                 | accumulated       | packets             | ✓                    | -                      | int32_t   |
| [pktRcvRetransTotal](#pktRcvRetransTotal)           | accumulated       | packets             | -                    | ✓                      | int32_t   |
| [pktSentACKTotal](#pktSentACKTotal)                 | accumulated       | packets             | -                    | ✓                      | int32_t   |
| [pktRecvACKTotal](#pktRecvACKTotal)                 | accumulated       | packets             | ✓                    | -                      | int32_t   |
| [pktSentNAKTotal](#pktSentNAKTotal)                 | accumulated       | packets             | -                    | ✓                      | int32_t   |
| [pktRecvNAKTotal](#pktRecvNAKTotal)                 | accumulated       | packets             | ✓                    | -                      | int32_t   |
| [usSndDurationTotal](#usSndDurationTotal)           | accumulated       | us (microseconds)   | ✓                    | -                      | int64_t   |
| [pktSndDropTotal](#pktSndDropTotal)                 | accumulated       | packets             | ✓                    | -                      | int32_t   |
| [pktRcvDropTotal](#pktRcvDropTotal)                 | accumulated       | packets             | -                    | ✓                      | int32_t   |
| [pktRcvUndecryptTotal](#pktRcvUndecryptTotal)       | accumulated       | packets             | -                    | ✓                      | int32_t   |
| [pktSndFilterExtraTotal](#pktSndFilterExtraTotal)   | accumulated       | packets             | ✓                    | -                      | int32_t   |
| [pktRcvFilterExtraTotal](#pktRcvFilterExtraTotal)   | accumulated       | packets             | -                    | ✓                      | int32_t   |
| [pktRcvFilterSupplyTotal](#pktRcvFilterSupplyTotal) | accumulated       | packets             | -                    | ✓                      | int32_t   |
| [pktRcvFilterLossTotal](#pktRcvFilterLossTotal)     | accumulated       | packets             | -                    | ✓                      | int32_t   |
| [byteSentTotal](#byteSentTotal)                     | accumulated       | bytes               | ✓                    | -                      | uint64_t  |
| [byteRecvTotal](#byteRecvTotal)                     | accumulated       | bytes               | -                    | ✓                      | uint64_t  |
| [byteSentUniqueTotal](#byteSentUniqueTotal)         | accumulated       | bytes               | ✓                    | -                      | uint64_t  |
| [byteRecvUniqueTotal](#byteRecvUniqueTotal)         | accumulated       | bytes               | -                    | ✓                      | uint64_t  |
| [byteRcvLossTotal](#byteRcvLossTotal)               | accumulated       | bytes               | -                    | ✓                      | uint64_t  |
| [byteRetransTotal](#byteRetransTotal)               | accumulated       | bytes               | ✓                    | -                      | uint64_t  |
| [byteSndDropTotal](#byteSndDropTotal)               | accumulated       | bytes               | ✓                    | -                      | uint64_t  |
| [byteRcvDropTotal](#byteRcvDropTotal)               | accumulated       | bytes               | -                    | ✓                      | uint64_t  |
| [byteRcvUndecryptTotal](#byteRcvUndecryptTotal)     | accumulated       | bytes               | -                    | ✓                      | uint64_t  |
| [pktSent](#pktSent)                                 | interval-based    | packets             | ✓                    | -                      | int64_t   |
| [pktRecv](#pktRecv)                                 | interval-based    | packets             | -                    | ✓                      | int64_t   |
| [pktSentUnique](#pktSentUnique)                     | interval-based    | packets             | ✓                    | -                      | int64_t   |
| [pktRecvUnique](#pktRecvUnique)                     | interval-based    | packets             | -                    | ✓                      | int64_t   |
| [pktSndLoss](#pktSndLoss)                           | interval-based    | packets             | ✓                    | -                      | int32_t   |
| [pktRcvLoss](#pktRcvLoss)                           | interval-based    | packets             | -                    | ✓                      | int32_t   |
| [pktRetrans](#pktRetrans)                           | interval-based    | packets             | ✓                    | -                      | int32_t   |
| [pktRcvRetrans](#pktRcvRetrans)                     | interval-based    | packets             | -                    | ✓                      | int32_t   |
| [pktSentACK](#pktSentACK)                           | interval-based    | packets             | -                    | ✓                      | int32_t   |
| [pktRecvACK](#pktRecvACK)                           | interval-based    | packets             | ✓                    | -                      | int32_t   |
| [pktSentNAK](#pktSentNAK)                           | interval-based    | packets             | -                    | ✓                      | int32_t   |
| [pktRecvNAK](#pktRecvNAK)                           | interval-based    | packets             | ✓                    | -                      | int32_t   |
| [pktSndFilterExtra](#pktSndFilterExtra)             | interval-based    | packets             | ✓                    | -                      | int32_t   |
| [pktRcvFilterExtra](#pktRcvFilterExtra)             | interval-based    | packets             | -                    | ✓                      | int32_t   |
| [pktRcvFilterSupply](#pktRcvFilterSupply)           | interval-based    | packets             | -                    | ✓                      | int32_t   |
| [pktRcvFilterLoss](#pktRcvFilterLoss)               | interval-based    | packets             | -                    | ✓                      | int32_t   |
| [mbpsSendRate](#mbpsSendRate)                       | interval-based    | Mbps                | ✓                    | -                      | double    |
| [mbpsRecvRate](#mbpsRecvRate)                       | interval-based    | Mbps                | -                    | ✓                      | double    |
| [usSndDuration](#usSndDuration)                     | interval-based    | us (microseconds)   | ✓                    | -                      | int64_t   |
| [pktReorderDistance](#pktReorderDistance)           | interval-based    | packets             | -                    | ✓                      | int32_t   |
| [pktRcvBelated](#pktRcvBelated)                     | interval-based    | packets             | -                    | ✓                      | int64_t   |
| [pktSndDrop](#pktSndDrop)                           | interval-based    | packets             | ✓                    | -                      | int32_t   |
| [pktRcvDrop](#pktRcvDrop)                           | interval-based    | packets             | -                    | ✓                      | int32_t   |
| [pktRcvUndecrypt](#pktRcvUndecrypt)                 | interval-based    | packets             | -                    | ✓                      | int32_t   |
| [byteSent](#byteSent)                               | interval-based    | bytes               | ✓                    | -                      | uint64_t  |
| [byteRecv](#byteRecv)                               | interval-based    | bytes               | -                    | ✓                      | uint64_t  |
| [byteSentUnique](#byteSentUnique)                   | interval-based    | bytes               | ✓                    | -                      | uint64_t  |
| [byteRecvUnique](#byteRecvUnique)                   | interval-based    | bytes               | -                    | ✓                      | uint64_t  |
| [byteRcvLoss](#byteRcvLoss)                         | interval-based    | bytes               | -                    | ✓                      | uint64_t  |
| [byteRetrans](#byteRetrans)                         | interval-based    | bytes               | ✓                    | -                      | uint64_t  |
| [byteSndDrop](#byteSndDrop)                         | interval-based    | bytes               | ✓                    | -                      | uint64_t  |
| [byteRcvDrop](#byteRcvDrop)                         | interval-based    | bytes               | -                    | ✓                      | uint64_t  |
| [byteRcvUndecrypt](#byteRcvUndecrypt)               | interval-based    | bytes               | -                    | ✓                      | uint64_t  |
| [usPktSndPeriod](#usPktSndPeriod)                   | instantaneous     | us (microseconds)   | ✓                    | -                      | double    |
| [pktFlowWindow](#pktFlowWindow)                     | instantaneous     | packets             | ✓                    | -                      | int32_t   |
| [pktCongestionWindow](#pktCongestionWindow)         | instantaneous     | packets             | ✓                    | -                      | int32_t   |
| [pktFlightSize](#pktFlightSize)                     | instantaneous     | packets             | ✓                    | -                      | int32_t   |
| [msRTT](#msRTT)                                     | instantaneous     | ms (milliseconds)   | ✓                    | ✓                      | double    |
| [mbpsBandwidth](#mbpsBandwidth)                     | instantaneous     | Mbps                | ✓                    | ✓                      | double    |
| [byteAvailSndBuf](#byteAvailSndBuf)                 | instantaneous     | bytes               | ✓                    | -                      | int32_t   |
| [byteAvailRcvBuf](#byteAvailRcvBuf)                 | instantaneous     | bytes               | -                    | ✓                      | int32_t   |
| [mbpsMaxBW](#mbpsMaxBW)                             | instantaneous     | Mbps                | ✓                    | -                      | double    |
| [byteMSS](#byteMSS)                                 | instantaneous     | bytes               | ✓                    | ✓                      | int32_t   |
| [pktSndBuf](#pktSndBuf)                             | instantaneous     | packets             | ✓                    | -                      | int32_t   |
| [byteSndBuf](#byteSndBuf)                           | instantaneous     | bytes               | ✓                    | -                      | int32_t   |
| [msSndBuf](#msSndBuf)                               | instantaneous     | ms (milliseconds)   | ✓                    | -                      | int32_t   |
| [msSndTsbPdDelay](#msSndTsbPdDelay)                 | instantaneous     | ms (milliseconds)   | ✓                    | -                      | int32_t   |
| [pktRcvBuf](#pktRcvBuf)                             | instantaneous     | packets             | -                    | ✓                      | int32_t   |
| [byteRcvBuf](#byteRcvBuf)                           | instantaneous     | bytes               | -                    | ✓                      | int32_t   |
| [msRcvBuf](#msRcvBuf)                               | instantaneous     | ms (milliseconds)   | -                    | ✓                      | int32_t   |
| [msRcvTsbPdDelay](#msRcvTsbPdDelay)                 | instantaneous     | ms (milliseconds)   | -                    | ✓                      | int32_t   |
| [pktReorderTolerance](#pktReorderTolerance)         | instantaneous     | packets             | -                    | ✓                      | int32_t   |
| [pktRcvAvgBelatedTime](#pktRcvAvgBelatedTime)       | instantaneous     | ms (milliseconds)   | -                    | ✓                      | double    |

### Accumulated Statistics

#### msTimeStamp

The time elapsed, in milliseconds, since the SRT socket has been created (after successful call to `srt_connect(...)` or `srt_bind(...)` function). Available both for sender and receiver.

#### pktSentTotal

The total number of sent DATA packets, including retransmitted packets ([pktRetransTotal](#pktRetransTotal)). Available for sender.

If the `SRTO_PACKETFILTER` socket option is enabled (refer to [SRT API Socket Options](API-socket-options.md)), this statistic counts sent packet filter control packets ([pktSndFilterExtraTotal](#pktSndFilterExtraTotal)) as well. Introduced in SRT v1.4.0.

#### pktRecvTotal

The total number of received DATA packets, including retransmitted packets ([pktRcvRetransTotal](#pktRcvRetransTotal)). Available for receiver.

If the `SRTO_PACKETFILTER` socket option is enabled (refer to [SRT API Socket Options](API-socket-options.md)), this statistic counts received packet filter control packets ([pktRcvFilterExtraTotal](#pktRcvFilterExtraTotal)) as well. Introduced in SRT v1.4.0.

#### pktSentUniqueTotal 

The total number of *unique* DATA packets sent by the SRT sender. Available for sender. 

This value contains only *unique* *original* DATA packets. Retransmitted DATA packets ([pktRetransTotal](#pktRetransTotal)) are not taken into account. If the `SRTO_PACKETFILTER` socket option is enabled (refer to [SRT API Socket Options](API-socket-options.md)), packet filter control packets ([pktSndFilterExtraTotal](#pktSndFilterExtraTotal)) are also not taken into account.

This value corresponds to the number of original DATA packets sent by the SRT sender. It counts every packet sent over the network for the first time, and can be calculated as follows: `pktSentUniqueTotal = pktSentTotal – pktRetransTotal`, or by `pktSentUniqueTotal = pktSentTotal – pktRetransTotal - pktSndFilterExtraTotal` if the  `SRTO_PACKETFILTER` socket option is enabled. The original DATA packets are sent only once.

#### pktRecvUniqueTotal

The total number of *unique* original, retransmitted or recovered by the packet filter DATA packets *received in time*, *decrypted without errors* and, as a result, scheduled for delivery to the upstream application by the SRT receiver. Available for receiver.

Unique means "first arrived" DATA packets. There is no difference whether a packet is original or, in case of loss, retransmitted or recovered by the packet filter. Whichever packet comes first is taken into account. 

This statistic doesn't count

- duplicate packets (retransmitted or sent several times by defective hardware/software),
- arrived too late packets (retransmitted or original packets arrived out of order) that were already dropped by the TLPKTDROP mechanism (see [pktRcvDropTotal](#pktRcvDropTotal) statistic),
- arrived in time packets, but decrypted with errors (see [pktRcvUndecryptTotal](#pktRcvUndecryptTotal) statistic), and, as a result, dropped by the TLPKTDROP mechanism (see [pktRcvDropTotal](#pktRcvDropTotal) statistic).

DATA packets recovered by the packet filter ([pktRcvFilterSupplyTotal](#pktRcvFilterSupplyTotal)) are taken into account if the `SRTO_PACKETFILTER` socket option is enabled (refer to [SRT API Socket Options](API-socket-options.md)). Do not mix up with the control packets received by the packet filter ([pktRcvFilterExtraTotal](#pktRcvFilterExtraTotal)).

#### pktSndLossTotal

The total number of data packets considered or reported as lost at the sender side. Does not correspond to the packets detected as lost at the receiver side. Available for sender.

A packet is considered lost in two cases: 
1. Sender receives a loss report from a receiver,
2. Sender initiates retransmission after not receiving an ACK packet for a certain timeout. Refer to `FASTREXMIT` and `LATEREXMIT` algorithms.

#### pktRcvLossTotal

The total number of SRT DATA packets detected as presently missing (either reordered or lost) at the receiver side. Available for receiver.

The detection of presently missing packets is triggered by a newly received DATA packet with the sequence number `s`. If `s` is greater than the sequence number `next_exp` of the next expected packet (`s > next_exp`), the newly arrived packet `s` is considered in-order and there is a sequence discontinuity of size `s - next_exp` associated with this packet. The presence of sequence discontinuity means that some packets of the original sequence have not yet arrived (presently missing), either reordered or lost. Once the sequence discontinuity is detected, its size `s - next_exp` is added to `pktRcvLossTotal` statistic. Refer to [RFC 4737 - Packet Reordering Metrics](https://tools.ietf.org/html/rfc4737) for details.

If the packet `s` is received out of order (`s < next_exp`), the statistic is not affected.

Note that only original (not retransmitted) SRT DATA packets are taken into account. Refer to [pktRcvRetransTotal](#pktRcvRetransTotal) for the formula for obtaining the total number of lost retransmitted packets.

In SRT v1.4.0, v1.4.1, the `pktRcvLossTotal` statistic includes packets that failed to be decrypted. To receive the number of presently missing packets, substract [pktRcvUndecryptTotal](#pktRcvUndecryptTotal) from the current one. This is going to be fixed in SRT v.1.5.0.

#### pktRetransTotal

The total number of retransmitted packets sent by the SRT sender. Available for sender.

This statistic is not interchangeable with the receiver [pktRcvRetransTotal](#pktRcvRetransTotal) statistic.

#### pktRcvRetransTotal

The total number of retransmitted packets registered at the receiver side. Available for receiver.

This statistic is not interchangeable with the sender [pktRetransTotal](#pktRetransTotal) statistic.

Note that the total number of lost retransmitted packets can be calculated as the total number of retransmitted packets sent by receiver minus the total number of retransmitted packets registered at the receiver side:  `pktRetransTotal - pktRcvRetransTotal`.

This is going to be implemented in SRT v1.5.0, see issue [#1208](https://github.com/Haivision/srt/issues/1208).

#### pktSentACKTotal

The total number of sent ACK (Acknowledgement) control packets. Available for receiver.

#### pktRecvACKTotal

The total number of received ACK (Acknowledgement) control packets. Available for sender.

#### pktSentNAKTotal

The total number of sent NAK (Negative Acknowledgement) control packets. Available for receiver.

#### pktRecvNAKTotal

The total number of received NAK (Negative Acknowledgement) control packets. Available for sender.

#### usSndDurationTotal

The total accumulated time in microseconds, during which the SRT sender has some data to transmit, including packets that have been sent, but not yet acknowledged. In other words, the total accumulated duration in microseconds when there was something to deliver (non-empty senders' buffer). Available for sender.

#### pktSndDropTotal

The total number of _dropped_ by the SRT sender DATA packets that have no chance to be delivered in time (refer to [Too-Late Packet Drop](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-4.6) mechanism). Available for sender.

Packets may be dropped conditionally when both `SRTO_TSBPDMODE` and `SRTO_TLPKTDROP` socket options are enabled, refer to [SRT API Socket Options](API-socket-options.md).

The delay before TLPKTDROP mechanism is triggered is calculated as follows 
`SRTO_PEERLATENCY + SRTO_SNDDROPDELAY + 2 * interval between sending ACKs`,
where `SRTO_PEERLATENCY` is the configured SRT latency, `SRTO_SNDDROPDELAY` adds an extra to `SRTO_PEERLATENCY` delay, the default `interval between sending ACKs` is 10 milliseconds. The minimum delay is `1000 + 2 * interval between sending ACKs` milliseconds. Refer to `SRTO_PEERLATENCY`, `SRTO_SNDDROPDELAY` socket options in [SRT API Socket Options](API-socket-options.md).

#### pktRcvDropTotal

The total number of _dropped_ by the SRT receiver and, as a result, not delivered to the upstream application DATA packets (refer to [Too-Late Packet Drop](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01#section-4.6) mechanism). Available for receiver.

This statistic counts

- not arrived packets including those signalled for dropping by the sender, that were dropped in favor of the subsequent existing packets,
- arrived too late packets (retransmitted or original packets arrived out of order),
- arrived in time packets, but decrypted with errors (see also [pktRcvUndecryptTotal](#pktRcvUndecryptTotal) statistic).

Packets may be dropped conditionally when both `SRTO_TSBPDMODE` and `SRTO_TLPKTDROP` socket options are enabled, refer to [SRT API Socket Options](API-socket-options.md).

#### pktRcvUndecryptTotal

The total number of packets that failed to be decrypted at the receiver side. Available for receiver.

#### pktSndFilterExtraTotal

The total number of packet filter control packets generated by the packet filter (refer to [SRT Packet Filtering & FEC](../features/packet-filtering-and-fec.md)). Available for sender.

Packet filter control packets contain only control information necessary for the packet filter. The type of these packets is DATA.

If the `SRTO_PACKETFILTER` socket option is disabled (refer to [SRT API Socket Options](API-socket-options.md)), this statistic is equal to 0. Introduced in SRT v1.4.0.

#### pktRcvFilterExtraTotal

The total number of packet filter control packets received by the packet filter (refer to [SRT Packet Filtering & FEC](../features/packet-filtering-and-fec.md)). Available for receiver.

Packet filter control packets contain only control information necessary for the packet filter. The type of these packets is DATA.

If the `SRTO_PACKETFILTER` socket option is disabled (refer to [SRT API Socket Options](API-socket-options.md)), this statistic is equal to 0. Introduced in SRT v1.4.0.

#### pktRcvFilterSupplyTotal

The total number of lost DATA packets recovered by the packet filter at the receiver side (e.g., FEC rebuilt packets; refer to [SRT Packet Filtering & FEC](../features/packet-filtering-and-fec.md)). Available for receiver.

If the `SRTO_PACKETFILTER` socket option is disabled (refer to [SRT API Socket Options](API-socket-options.md)), this statistic is equal to 0. Introduced in SRT v1.4.0.

#### pktRcvFilterLossTotal

The total number of lost DATA packets **not** recovered by the packet filter at the receiver side (refer to [SRT Packet Filtering & FEC](../features/packet-filtering-and-fec.md)). Available for receiver.

If the `SRTO_PACKETFILTER` socket option is disabled (refer to [SRT API Socket Options](API-socket-options.md)), this statistic is equal to 0. Introduced in SRT v1.4.0.

#### byteSentTotal

Same as [pktSentTotal](#pktSentTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for sender.

#### byteRecvTotal

Same as [pktRecvTotal](#pktRecvTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for receiver.

#### byteSentUniqueTotal

Same as [pktSentUniqueTotal](#pktSentUniqueTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for sender.

#### byteRecvUniqueTotal

Same as [pktRecvUniqueTotal](#pktRecvUniqueTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for receiver.

#### byteRcvLossTotal

Same as [pktRcvLossTotal](#pktRcvLossTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Bytes for the presently missing (either reordered or lost) packets' payloads are estimated based on the average packet size. Available for receiver.

#### byteRetransTotal

Same as [pktRetransTotal](#pktRetransTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for sender.

#### byteSndDropTotal

Same as [pktSndDropTotal](#pktSndDropTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for sender.

#### byteRcvDropTotal

Same as [pktRcvDropTotal](#pktRcvDropTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Bytes for the dropped packets' payloads are estimated based on the average packet size. Available for receiver.

#### byteRcvUndecryptTotal

Same as [pktRcvUndecryptTotal](#pktRcvUndecryptTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for receiver.


### Interval-Based Statistics

#### pktSent

Same as [pktSentTotal](#pktSentTotal), but for a specified interval.

#### pktRecv

Same as [pktRecvTotal](#pktRecvTotal), but for a specified interval.

#### pktSentUnique

Same as [pktSentUniqueTotal](#pktSentUniqueTotal), but for a specified interval.

#### pktRecvUnique

Same as [pktRecvUniqueTotal](#pktRecvUniqueTotal), but for a specified interval.

#### pktSndLoss

Same as [pktSndLossTotal](#pktSndLossTotal), but for a specified interval.

#### pktRcvLoss

Same as [pktRcvLossTotal](#pktRcvLossTotal), but for a specified interval.

#### pktRetrans

Same as [pktRetransTotal](#pktRetransTotal), but for a specified interval.

#### pktRcvRetrans

Same as [pktRcvRetransTotal](#pktRcvRetransTotal), but for a specified interval.

#### pktSentACK

Same as [pktSentACKTotal](#pktSentACKTotal), but for a specified interval.

#### pktRecvACK

Same as [pktRecvACKTotal](#pktRecvACKTotal), but for a specified interval.

#### pktSentNAK

Same as [pktSentNAKTotal](#pktSentNAKTotal), but for a specified interval.

#### pktRecvNAK

Same as [pktRecvNAKTotal](#pktRecvNAKTotal), but for a specified interval.

#### pktSndFilterExtra

Same as [pktSndFilterExtraTotal](#pktSndFilterExtraTotal), but for a specified interval.

Introduced in v1.4.0. Refer to [SRT Packet Filtering & FEC](../features/packet-filtering-and-fec.md).

#### pktRcvFilterExtra

Same as [pktRcvFilterExtraTotal](#pktRcvFilterExtraTotal), but for a specified interval.

Introduced in v1.4.0. Refer to [SRT Packet Filtering & FEC](../features/packet-filtering-and-fec.md).

#### pktRcvFilterSupply

Same as [pktRcvFilterSupplyTotal](#pktRcvFilterSupplyTotal), but for a specified interval.

Introduced in v1.4.0. Refer to [SRT Packet Filtering & FEC](../features/packet-filtering-and-fec.md).

#### pktRcvFilterLoss

Same as [pktRcvFilterLossTotal](#pktRcvFilterLossTotal), but for a specified interval.

Introduced in v1.4.0. Refer to [SRT Packet Filtering & FEC](../features/packet-filtering-and-fec.md).

#### mbpsSendRate

Sending rate in Mbps. Sender side.

#### mbpsRecvRate

Receiving rate in Mbps. Receiver side.

#### usSndDuration

Same as [usSndDurationTotal](#usSndDurationTotal), but measured on a specified interval.

#### pktReorderDistance

The distance in sequence numbers between the two original (not retransmitted) packets,
that were received out of order. Receiver only.

The traceable distance values are limited by the maximum reorder tolerance set by  `SRTO_LOSSMAXTTL`.

#### pktRcvBelated

The number of packets received but IGNORED due to having arrived too late.

Makes sense only if TSBPD and TLPKTDROP are enabled.

An offset between sequence numbers of the newly arrived DATA packet and latest 
acknowledged DATA packet is calculated.
If the offset is negative, the packet is considered late, meaning that it was 
either already acknowledged or dropped by TSBPD as too late to be delivered.

Retransmitted packets can also be considered late.

#### pktSndDrop

Same as [pktSndDropTotal](#pktSndDropTotal), but for a specified interval.

#### pktRcvDrop

Same as [pktRcvDropTotal](#pktRcvDropTotal), but for a specified interval.

#### pktRcvUndecrypt

Same as [pktRcvUndecryptTotal](#pktRcvUndecryptTotal), but for a specified interval.

#### byteSent

Same as [byteSentTotal](#byteSentTotal), but for a specified interval.

#### byteRecv

Same as [byteRecvTotal](#byteRecvTotal), but for a specified interval.

#### byteSentUnique

Same as [byteSentUniqueTotal](#byteSentUniqueTotal), but for a specified interval.

#### byteRecvUnique

Same as [byteRecvUniqueTotal](#byteRecvUniqueTotal), but for a specified interval.

#### byteRcvLoss

Same as [byteRcvLossTotal](#byteRcvLossTotal), but for a specified interval.

#### byteRetrans

Same as [byteRetransTotal](#byteRetransTotal), but for a specified interval.

#### byteSndDrop

Same as [byteSndDropTotal](#byteSndDropTotal), but for a specified interval.

#### byteRcvDrop

Same as [byteRcvDropTotal](#byteRcvDropTotal), but for a specified interval.

#### byteRcvUndecrypt

Same as [byteRcvUndecryptTotal](#byteRcvUndecryptTotal), but for a specified interval.


### Instantaneous Statistics

#### usPktSndPeriod

Current minimum time interval between which consecutive packets are sent, in 
microseconds. Sender only.

Note that several sockets sharing one outgoing port use the same sending queue.
They may have different pacing of the outgoing packets, but all the packets will
be placed in the same sending queue, which may affect the send timing.

`usPktSndPeriod` is the minimum time (sending period) that must be kept
between two packets sent consecutively over the link used by an SRT socket.
It is not the EXACT time interval between two consecutive packets. In the case where the time spent by an 
application between sending two consecutive packets exceeds `usPktSndPeriod`, the next 
packet will be sent faster, or even immediately, to preserve the average sending rate.

**Note**: Does not apply to probing packets.

#### pktFlowWindow

The maximum number of packets that can be "in flight". Sender only.
See also [pktFlightSize](#pktFlightSize).

The value retrieved on the sender side represents an estimation of the amount
of free space in the buffer of the peer receiver.
The actual amount of available space is periodically reported back by the receiver in ACK packets.
When this value drops to zero, the next packet sent will be dropped by the receiver
without processing. In **file mode** this may cause a slowdown of sending in
order to wait until the receiver has more space available, after it
eventually extracts the packets waiting in its receiver buffer; in **live
mode** the receiver buffer contents should normally occupy not more than half
of the buffer size (default 8192). If `pktFlowWindow` value is less than that
and becomes even less in the next reports, it means that the receiver
application on the peer side cannot process the incoming stream fast enough and
this may lead to a dropped connection.

#### pktCongestionWindow

Congestion window size, in number of packets. Sender only.

Dynamically limits the maximum number of packets that can be in flight.
Congestion control module dynamically changes the value.

In **file mode**  this value starts at 16 and is increased to the number of reported
acknowledged packets. This value is also updated based on the delivery rate, reported by the receiver.
It represents the maximum number of packets that can be safely
sent without causing network congestion. The higher this value is, the faster the
packets can be sent. In **live mode** this field is not used.

#### pktFlightSize

The number of packets in flight. Sender only.

`pktFlightSize <= pktFlowWindow` and `pktFlightSize <= pktCongestionWindow`

This is the distance 
between the packet sequence number that was last reported by an ACK message and 
the sequence number of the latest packet sent (at the moment when the statistics
are being read).

**NOTE:** ACKs are received periodically (at least every 10 ms). This value is most accurate just
after receiving an ACK and becomes a little exaggerated over time until the
next ACK arrives. This is because with a new packet sent,
while the ACK number stays the same for a moment,
the value of `pktFlightSize` increases.
But the exact number of packets arrived since the last ACK report is unknown.
A new statistic might be added which only reports the distance
between the ACK sequence and the sent sequence at the moment when an ACK arrives,
and isn't updated until the next ACK arrives. The difference between this value
and `pktFlightSize` would then reveal the number of packets with an unknown state
at that moment.

#### msRTT

Smoothed round-trip time (SRTT), an exponentially-weighted moving average (EWMA) of an endpoint's RTT samples, in milliseconds.
Available both for sender and receiver.

See [Section 4.10. Round-Trip Time Estimation](https://tools.ietf.org/html/draft-sharabayko-srt-01#section-4.10) of the [Internet Draft](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-01) and [[RFC6298] Paxson, V., Allman, M., Chu, J., and M. Sargent, "Computing TCP's Retransmission Timer"](https://datatracker.ietf.org/doc/html/rfc6298) for more details.

#### mbpsBandwidth

Estimated bandwidth of the network link, in Mbps. Sender only.

The bandwidth is estimated at the receiver.
The estimation is based on the time between two probing DATA packets.
Every 16th data packet is sent immediately after the previous data packet.
By measuring the delay between probe packets on arrival,
it is possible to estimate the maximum available transmission rate,
which is interpreted as the bandwidth of the link.
The receiver then sends back a running average calculation to the sender with an ACK message.

#### byteAvailSndBuf

The available space in the sender's buffer, in bytes. Sender only.

This value decreases with data scheduled for sending by the application, and increases 
with every ACK received from the receiver, after the packets are sent over 
the UDP link.

#### byteAvailRcvBuf

The available space in the receiver's buffer, in bytes. Receiver only.

This value increases after the application extracts the data from the socket
(uses one of `srt_recv*` functions) and decreases with every packet received
from the sender over the UDP link.

#### mbpsMaxBW

Transmission bandwidth limit, in Mbps. Sender only.
Usually this is the setting from 
the `SRTO_MAXBW` option, which may include the value 0 (unlimited). Under certain 
conditions a nonzero value might be be provided by a congestion 
control module, although none of the built-in congestion control modules 
currently use it.

Refer to `SRTO_MAXBW` and `SRTO_INPUTBW` in [SRT API Socket Options](API-socket-options.md).

#### byteMSS

Maximum Segment Size (MSS), in bytes.
Same as the value from the `SRTO_MSS` socket option.
Should not exceed the size of the maximum transmission unit (MTU), in bytes. Sender and Receiver.
The default size of the UDP packet used for transport,
including all possible headers (Ethernet, IP and UDP), is 1500 bytes.

Refer to `SRTO_MSS` in [SRT API Socket Options](API-socket-options.md).

#### pktSndBuf

The number of packets in the sender's buffer that are already 
scheduled for sending or even possibly sent, but not yet acknowledged.
Sender only.

Once the receiver acknowledges the receipt of a packet, or the TL packet drop
is triggered, the packet is removed from the sender's buffer.
Until this happens, the packet is considered as unacknowledged.

A moving average value is reported when the value is retrieved by calling
`srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`
with `instantaneous=false`.

The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

#### byteSndBuf

Instantaneous (current) value of `pktSndBuf`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Sender side.

#### msSndBuf

The timespan (msec) of packets in the sender's buffer (unacknowledged packets). Sender only.

A moving average value is reported when the value is retrieved by calling
`srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`
with `instantaneous=false`.

The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

#### msSndTsbPdDelay

Timestamp-based Packet Delivery Delay value of the peer.
If `SRTO_TSBPDMODE` is on (default for **live mode**), it 
returns the value of `SRTO_PEERLATENCY`, otherwise 0.
The sender reports the TSBPD delay value of the receiver.
The receiver reports the TSBPD delay of the sender.

#### pktRcvBuf

The number of acknowledged packets in receiver's buffer. Receiver only.

This measurement does not include received but not acknowledged packets, stored in the receiver's buffer.

A moving average value is reported when the value is retrieved by calling
`srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`
with `instantaneous=false`.

The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

#### byteRcvBuf

Instantaneous (current) value of `pktRcvBuf`, expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Receiver side.

#### msRcvBuf

The timespan (msec) of acknowledged packets in the receiver's buffer. Receiver side.

If TSBPD mode is enabled (defualt for **live mode**),
a packet can be acknowledged, but not yet ready to play.
This range includes all packets regardless of whether 
they are ready to play or not.

A moving average value is reported when the value is retrieved by calling
`srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`
with `instantaneous=false`.

The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

Instantaneous value is only reported if TSBPD mode is enabled, otherwise 0 is reported (see #900).

#### msRcvTsbPdDelay

Timestamp-based Packet Delivery Delay value set on the socket via `SRTO_RCVLATENCY` or `SRTO_LATENCY`.
The value is used to apply TSBPD delay for reading the received data on the socket. Receiver side.

If `SRTO_TSBPDMODE` is off (default for **file mode**), 0 is returned.

#### pktReorderTolerance

Instant value of the packet reorder tolerance. Receiver side. Refer to [pktReorderDistance](#pktReorderDistance).

`SRTO_LOSSMAXTTL` sets the maximum reorder tolerance value. The value defines the maximum
time-to-live for the original packet, that was received after with a gap in the sequence of incoming packets.
Those missing packets are expected to come out of order, therefore no loss is reported.
The actual TTL value (**pktReorderTolerance**) specifies the number of packets to receive further, before considering
the preceding packets lost, and sending the loss report.

The internal algorithm checks the order of incoming packets and adjusts the tolerance based on the reorder
distance (**pktReorderTolerance**), but not to a value higher than the maximum (`SRTO_LOSSMAXTTL`).

SRT starts from tolerance value set in `SRTO_LOSSMAXTTL` (initial tolerance is set to 0 in SRT v1.4.0 and prior versions).
Once the receiver receives the first reordered packet, it increases the tolerance to the distance in the sequence
discontinuity of the two packets. \
After 10 consecutive original (not retransmitted) packets come in order, the reorder distance
is decreased by 1 for every such packet.

For example, assume packets with the following sequence
numbers are being received: \
1, 2, 4, 3, 5, 7, 6, 10, 8, 9
SRT starts from 0 tolerance. Receiving packet with sequence number 4 has a discontinuity
equal to one packet. The loss is reported to the sender.
With the next packet (sequence number 3) a reordering is detected. Reorder tolerance is increased to 1. \
The next sequence discontinuity is detected when the packet with sequence number 7 is received.
The current tolerance value is 1, which is equal to the gap (between 5 and 7). No loss is reported. \
Next packet with sequence number 10 has a higher sequence discontinuity equal to 2.
Missing packets with sequence numbers 8 and 9 will be reported lost with the next received packet
(reorder distance is still at 1).
The next received packet has sequence number 8. Reorder tolerance value is increased to 2.
The packet with sequence number 9 is reported lost.

#### pktRcvAvgBelatedTime

Accumulated difference between the current time and the time-to-play of a packet 
that is received late.


## SRT Group Statistics

SRT group statistics are implemented for [SRT Connection Bonding](../features/bonding-quick-start.md) feature and available since SRT v1.5.0.

The `srt_bistats(SRTSOCKET u, ...)` function can be used with a socket group ID as the first argument to get statistics for a group. `SRT_TRACEBSTATS` values will mostly be zeros, except for the fields listed in the [Summary Table](#group-summary-table) below. Refer to the [SRT API Functions](../API/API-functions.md#socket-group-management) documentation for usage instructions.

### Summary Table <a name="group-summary-table"></a>

The table below provides a summary of SRT group statistics: name, type, unit of measurement, data type, and whether it is calculated by the sender or receiver. See sections [Accumulated Statistics](#group-accumulated-statistics) and [Interval-Based Statistics](#group-interval-based-statistics) for a detailed description of each statistic.

| Statistic                                         | Type of Statistic | Unit of Measurement | Available for Sender | Available for Receiver | Data Type |
| ------------------------------------------------- | ----------------- | ------------------- | -------------------- | ---------------------- | --------- |
| [msTimeStamp](#group-msTimeStamp)                 | accumulated       | ms (milliseconds)   | ✓                    | ✓                      | int64_t   |
| [pktSentUniqueTotal](#group-pktSentUniqueTotal)   | accumulated       | packets             | ✓                    | -                      | int64_t   |
| [pktRecvUniqueTotal](#group-pktRecvUniqueTotal)   | accumulated       | packets             | -                    | ✓                      | int64_t   |
| [pktRcvDropTotal](#group-pktRcvDropTotal)         | accumulated       | packets             | -                    | ✓                      | int32_t   |
| [byteSentUniqueTotal](#group-byteSentUniqueTotal) | accumulated       | packets             | ✓                    | -                      | int64_t   |
| [byteRecvUniqueTotal](#group-byteRecvUniqueTotal) | accumulated       | packets             | -                    | ✓                      | int64_t   |
| [byteRcvDropTotal](#group-byteRcvDropTotal)       | accumulated       | packets             | -                    | ✓                      | int32_t   |
| [pktSentUnique](#group-pktSentUnique)             | interval-based    | packets             | ✓                    | -                      | int64_t   |
| [pktRecvUnique](#group-pktRecvUnique)             | interval-based    | packets             | -                    | ✓                      | int64_t   |
| [pktRcvDrop](#group-pktRcvDrop)                   | interval-based    | packets             | -                    | ✓                      | int32_t   |
| [byteSentUnique](#group-byteSentUnique)           | interval-based    | packets             | ✓                    | -                      | int64_t   |
| [byteRecvUnique](#group-byteRecvUnique)           | interval-based    | packets             | -                    | ✓                      | int64_t   |
| [byteRcvDrop](#group-byteRcvDrop)                 | interval-based    | packets             | -                    | ✓                      | int32_t   |

### Accumulated Statistics <a name="group-accumulated-statistics"></a>

#### msTimeStamp <a name="group-msTimeStamp"></a>

The time elapsed, in milliseconds, since the time ("connection" time) when the initial group connection has been initiated (the time when the first connection in the group has been made and therefore made the group connected). This "connection" time will be then set in this statistic in every next socket that will become a member of the group as the new connections are established. A new connection to an already connected group doesn’t change the value of "connection" time. Available both for sender and receiver. 

#### pktSentUniqueTotal <a name="group-pktSentUniqueTotal"></a>

The number of *unique original* DATA packets sent by the socket group. Available for sender.

This value counts every *original* DATA packet sent over the network for the first time by the socket group. There is no difference between Connection Bonding modes (broadcast, backup and balancing). For example, sending the packet with a particular sequence number over multiple links in case of broadcast mode (it means sending this packet multiple times) does not affect the statistic and this very packet is taken into account only once.

This statistic does not count retransmitted DATA packets that are individual per socket connection within the group. See the corresponding [pktRetransTotal](#pktRetransTotal) socket statistic.

If the `SRTO_PACKETFILTER` socket option is enabled (refer to [SRT API Socket Options](API-socket-options.md)), this statistic does not count packet filter control packets that are individual per socket connection within the group. See the corresponding [pktSndFilterExtraTotal](#pktSndFilterExtraTotal) socket statistic.

#### pktRecvUniqueTotal <a name="group-pktRecvUniqueTotal"></a>

The number of *unique* DATA packets *received in time* by the socket group and, as a result, scheduled for delivery to the upstream application. Available for receiver.

Unique means "first arrived over multiple links" DATA packets. Whichever packet comes first over whichever link is taken into account.

This statistic doesn't count

- discarded as duplicate by the group reader packets, see [pktRcvDiscardTotal](#group-pktRcvDiscardTotal) statistic,
- dropped by the socket group packets, see [pktRcvDropTotal](#group-pktRcvDropTotal) statistic.

#### pktRcvDropTotal <a name="group-pktRcvDropTotal"></a>

The number of *dropped* and, as a result, *not delivered* to the upstream application by the socket group DATA packets. Available for receiver.

A packet is considered dropped by the socket group if it has been dropped by the TLPKTDROP mechanism over all the links from the group. See the corresponding socket [pktRcvDropTotal](#pktRcvDropTotal) statistic.

For example, if a packet with a particular sequence number has been dropped over one or several links, but has not been dropped over at least one link, it is *not* considered dropped by the socket group and can be delivered to the upstream application. Only if a packet has been dropped over all the links from the group, it is considered dropped by the socket group and can not be delivered to the upstream application.

In fact, only sockets can drop the packets and the group is simply responsible for delivering received over multiple sockets packets to the application.

#### byteSentUniqueTotal <a name="group-byteSentUniqueTotal"></a>

Same as [pktSentUniqueTotal](#group-pktSentUniqueTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for sender.

#### byteRecvUniqueTotal <a name="group-byteRecvUniqueTotal"></a>

Same as [pktRecvUniqueTotal](#group-pktRecvUniqueTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for receiver.

#### byteRcvDropTotal <a name="group-byteRcvDropTotal"></a>

Same as [pktRcvDropTotal](#group-pktRcvDropTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for receiver.

### Interval-Based Statistics <a name="group-interval-based-statistics"></a>

#### pktSentUnique <a name="group-pktSentUnique"></a>

Same as [pktSentUniqueTotal](#group-pktSentUniqueTotal), but for a specified interval.

#### pktRecvUnique <a name="group-pktRecvUnique"></a>

Same as [pktRecvUniqueTotal](#group-pktRecvUniqueTotal), but for a specified interval.

#### pktRcvDrop <a name="group-pktRcvDrop"></a>

Same as [pktRcvDropTotal](#group-pktRcvDropTotal), but for a specified interval.

#### byteSentUnique <a name="group-byteSentUnique"></a>

Same as [byteSentUniqueTotal](#group-byteSentUniqueTotal), but for a specified interval.

#### byteRecvUnique <a name="group-byteRecvUnique"></a>

Same as [byteRecvUniqueTotal](#group-byteRecvUniqueTotal), but for a specified interval.

#### byteRcvDrop <a name="group-byteRcvDrop"></a>

Same as [byteRcvDropTotal](#group-byteRcvDropTotal), but for a specified interval.

### Formulas <a name="group-formulas"></a>

The ratio of unrecovered by the socket group packets `Dropped Packets Ratio` can be calculated as follows:

```
Dropped Packets Ratio = pktRcvDropTotal / pktSentUniqueTotal; in case both sender and receiver statistics is available
Dropped Packets Ratio = pktRcvDropTotal / (pktRecvUniqueTotal + pktRcvDropTotal); in case receiver only statistics is available 
```