# SRT Statistics

SRT provides a powerful set of statistical data on a socket. This data can be used to keep an eye on a socket's health and track faulty behavior.

Statistics are calculated independently on each side (receiver and sender) and are not exchanged between peers unless explicitly stated.

The following API functions can be used to retrieve statistics on an SRT socket:

* `int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear)`
* `int srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`

Refer to the documentation of the [API functions](API-functions.md) for usage instructions.

# Summary Table

The table below provides the summary on the SRT statistics: name, type, unit of measurement, the side it's calculated (sender or receiver), and data type. See section ["Detailed Description"](#Detailed Description) for detailed description of each statistic.

There are three types of statistics:

- Accumulated that means the statistic is accumulated since the time an SRT socket has been created (after the successful call to `srt_connect(...)` or `srt_bind(...)` function), e.g., `pktSentTotal`, etc.,
- Interval-based that means the statistic is accumulated during the specified time interval, e.g., 100 milliseconds if SRT statistics is collected each 100 milliseconds, since the time an SRT socket has been created, e.g., `pktSent` , etc.. The value of the statistic can be reset by calling the `srt_bstats(..., int clear)` function with `clear = 1`, 
- Instantaneous that means the statistic is obtained at the moment `srt_bstats()` function is called.

| Statistic               | Type of Statistic | Unit of Measurement | Available for Sender | Available for Receiver | Data Type |
| ----------------------- | ----------------- | ------------------- | -------------------- | ---------------------- | --------- |
| msTimeStamp             | accumulated       | ms (milliseconds)   | ✓                    | ✓                      | int64_t   |
| pktSentTotal            | accumulated       | packets             | ✓                    | -                      | int64_t   |
| pktRecvTotal            | accumulated       | packets             | -                    | ✓                      | int64_t   |
| pktSndLossTotal         | accumulated       | packets             | ✓                    | -                      | int32_t   |
| pktRcvLossTotal         | accumulated       | packets             | -                    | ✓                      | int32_t   |
| pktRetransTotal         | accumulated       | packets             | ✓                    | -                      | int32_t   |
| pktRcvRetransTotal      | accumulated       | packets             | -                    | ✓                      | int32_t   |
| pktSentACKTotal         | accumulated       | packets             | -                    | ✓                      | int32_t   |
| pktRecvACKTotal         | accumulated       | packets             | ✓                    | -                      | int32_t   |
| pktSentNAKTotal         | accumulated       | packets             | -                    | ✓                      | int32_t   |
| pktRecvNAKTotal         | accumulated       | packets             | ✓                    | -                      | int32_t   |
| usSndDurationTotal      | accumulated       | us (microseconds)   | ✓                    | -                      | int64_t   |
| pktSndDropTotal         | accumulated       | packets             | ✓                    | -                      | int32_t   |
| pktRcvDropTotal         | accumulated       | packets             | -                    | ✓                      | int32_t   |
| pktRcvUndecryptTotal    | accumulated       | packets             | -                    | ✓                      | int32_t   |
| pktSndFilterExtraTotal  | accumulated       | packets             | ✓                    | -                      | int32_t   |
| pktRcvFilterExtraTotal  | accumulated       | packets             | -                    | ✓                      | int32_t   |
| pktRcvFilterSupplyTotal | accumulated       | packets             | -                    | ✓                      | int32_t   |
| pktRcvFilterLossTotal   | accumulated       | packets             | -                    | ✓                      | int32_t   |
| byteSentTotal           | accumulated       | bytes               | ✓                    | -                      | uint64_t  |
| byteRecvTotal           | accumulated       | bytes               | -                    | ✓                      | uint64_t  |
| byteRcvLossTotal        | accumulated       | bytes               | -                    | ✓                      | uint64_t  |
| byteRetransTotal        | accumulated       | bytes               | ✓                    | -                      | uint64_t  |
| byteSndDropTotal        | accumulated       | bytes               | ✓                    | -                      | uint64_t  |
| byteRcvDropTotal        | accumulated       | bytes               | -                    | ✓                      | uint64_t  |
| byteRcvUndecryptTotal   | accumulated       | bytes               | -                    | ✓                      | uint64_t  |
| pktSent                 | interval-based    | packets             | ✓                    | -                      | int64_t   |
| pktRecv                 | interval-based    | packets             | -                    | ✓                      | int64_t   |
| pktSndLoss              | interval-based    | packets             | ✓                    | -                      | int32_t   |
| pktRcvLoss              | interval-based    | packets             | -                    | ✓                      | int32_t   |
| pktRetrans              | interval-based    | packets             | ✓                    | -                      | int32_t   |
| pktRcvRetrans           |                   |                     |                      |                        | int32_t   |
| pktSentACK              | interval-based    | packets             |                      |                        | int32_t   |
| pktRecvACK              | interval-based    | packets             |                      |                        | int32_t   |
| pktSentNAK              | interval-based    | packets             |                      |                        | int32_t   |
| pktRecvNAK              | interval-based    | packets             |                      |                        | int32_t   |
| pktSndFilterExtra       | interval-based    | packets             | ✓                    | -                      | int32_t   |
| pktRcvFilterExtra       | interval-based    | packets             | -                    | ✓                      | int32_t   |
| pktRcvFilterSupply      | interval-based    | packets             | -                    | ✓                      | int32_t   |
| pktRcvFilterLoss        | interval-based    | packets             | -                    | ✓                      | int32_t   |
| mbpsSendRate            | interval-based    | Mbps                | ✓                    | -                      | double    |
| mbpsRecvRate            | interval-based    | Mbps                | -                    | ✓                      | double    |
| usSndDuration           | interval-based    | us (microseconds)   | ✓                    | -                      | int64_t   |
| pktReorderDistance      | interval-based    | packets             | -                    | ✓                      | int32_t   |
| pktReorderTolerance     | interval-based    |                     |                      |                        | int32_t   |
| pktRcvAvgBelatedTime    |                   |                     |                      |                        | double    |
| pktRcvBelated           |                   |                     |                      |                        | int64_t   |
| pktSndDrop              | interval-based    | packets             | ✓                    | -                      | int32_t   |
| pktRcvDrop              | interval-based    | packets             | -                    | ✓                      | int32_t   |
| pktRcvUndecrypt         | interval-based    | packets             | -                    | ✓                      | int32_t   |
| byteSent                | interval-based    | bytes               | ✓                    | -                      | uint64_t  |
| byteRecv                | interval-based    | bytes               | -                    | ✓                      | uint64_t  |
| byteRcvLoss             | interval-based    | bytes               | -                    | ✓                      | uint64_t  |
| byteRetrans             | interval-based    | bytes               | ✓                    | -                      | uint64_t  |
| byteSndDrop             | interval-based    | bytes               | ✓                    | -                      | uint64_t  |
| byteRcvDrop             | interval-based    | bytes               | -                    | ✓                      | uint64_t  |
| byteRcvUndecrypt        | interval-based    | bytes               | -                    | ✓                      | uint64_t  |
| usPktSndPeriod          | instantaneous     | us (microseconds)   | ✓                    | -                      | double    |
| pktFlowWindow           | instantaneous     | packets             | ✓                    | -                      | int32_t   |
| pktCongestionWindow     | instantaneous     | packets             | ✓                    | -                      | int32_t   |
| pktFlightSize           | instantaneous     | packets             | ✓                    | -                      | int32_t   |
| msRTT                   | instantaneous     | ms (milliseconds)   | ✓                    | ✓                      | double    |
| mbpsBandwidth           | instantaneous     | Mbps                | ✓                    | ✓                      | double    |
| byteAvailSndBuf         | instantaneous     | bytes               | ✓                    | -                      | int32_t   |
| byteAvailRcvBuf         | instantaneous     | bytes               | -                    | ✓                      | int32_t   |
| mbpsMaxBW               | instantaneous     | Mbps                | ✓                    | -                      | double    |
| byteMSS                 | instantaneous     | bytes               | ✓                    | ✓                      | int32_t   |
| pktSndBuf               | instantaneous     | packets             | ✓                    | -                      | int32_t   |
| byteSndBuf              | instantaneous     | bytes               | ✓                    | -                      | int32_t   |
| msSndBuf                | instantaneous     | ms (milliseconds)   | ✓                    | -                      | int32_t   |
| msSndTsbPdDelay         | instantaneous     | ms (milliseconds)   | ✓                    | -                      | int32_t   |
| pktRcvBuf               | instantaneous     | packets             | -                    | ✓                      | int32_t   |
| byteRcvBuf              | instantaneous     | bytes               | -                    | ✓                      | int32_t   |
| msRcvBuf                | instantaneous     | ms (milliseconds)   | -                    | ✓                      | int32_t   |
| msRcvTsbPdDelay         | instantaneous     | ms (milliseconds)   | -                    | ✓                      | int32_t   |


# Detailed Description

## Accumulated Statistics

### msTimeStamp

The time elapsed since the SRT socket has been created (after successful call to `srt_connect(...)` or `srt_bind(...)` function), in milliseconds. Available both for sender and receiver.

### pktSentTotal

The total number of sent data packets, including retransmitted packets. Available for sender.

### pktRecvTotal

The total number of received packets, including retransmitted packets. Available for receiver.

### pktSndLossTotal

The total number of data packets considered or reported as lost at the sender side. Does not correspond to the packets detected as lost at the receiver side. Available for sender.

A packet is considered lost in two cases: 
1. Sender receives a loss report from a receiver,
2. Sender initiates retransmission after not receiving an ACK packet for a certain timeout. Refer to `FASTREXMIT` and `LATEREXMIT` algorithms.

### pktRcvLossTotal

The total number of SRT DATA packets detected as presently missing (either reordered or lost) at the receiver side. Available for receiver.

The detection of presently missing packets is triggered by a newly received DATA packet with the sequence number `s`. If `s` is greater than the sequence number `next_exp` of the next expected packet (`s > next_exp`), the newly arrived packet `s` is considered in-order and there is a sequence discontinuity of size `s - next_exp` associated with this packet. The presence of sequence discontinuity means that some packets of the original sequence have not yet arrived (presently missing), either reordered or lost. Once the sequence discontinuity is detected, its size `s - next_exp` is added to `pktRcvLossTotal` statistic. Refer to [RFC 4737 - Packet Reordering Metrics](https://tools.ietf.org/html/rfc4737) for details.

If the packet `s` is received out of order (`s < next_exp`), the statistic is not affected.

Note that only original (not retransmitted) SRT DATA packets are taken into account. Refer to [pktRcvRetransTotal](#pktRcvRetransTotal) for the formula of obtaining the total number of lost retransmitted packets.

In SRT v1.4.0, v1.4.1, `pktRcvLossTotal` statistics includes packets that failed to be decrypted. To receive the number of presently missing packets, substract [pktRcvUndecryptTotal](#pktRcvUndecryptTotal) from the current one. This is going to be fixed within SRT v.1.5.0.

### pktRetransTotal

The total number of retransmitted packets sent by the SRT sender. Available for sender.

This statistics is not interchangeable with the receiver [pktRcvRetransTotal](#pktRcvRetransTotal) statistic.

### pktRcvRetransTotal

The total number of retransmitted packets registered at the receiver side. Available for receiver.

This statistics is not interchangeable with the sender [pktRetransTotal](#pktRetransTotal) statistic.

Note that the total number of lost retransmitted packets can be calculated as the total number of retransmitted packets sent by receiver minus the total number of retransmitted packets registered at the receiver side:  `pktRetransTotal - pktRcvRetransTotal`.

This is going to be implemented in SRT v1.5.0, see issue [#1208](https://github.com/Haivision/srt/issues/1208).

### pktSentACKTotal

The total number of sent ACK (Acknowledgement) control packets. Available for receiver.

### pktRecvACKTotal

The total number of received ACK (Acknowledgement) control packets. Available for sender.

### pktSentNAKTotal

The total number of sent NAK (Negative Acknowledgement) control packets. Available for receiver.

### pktRecvNAKTotal

The total number of received NAK (Negative Acknowledgement) control packets. Available for sender.

### usSndDurationTotal

The total accumulated time in microseconds, during which the SRT sender has some data to transmit, including packets that have been sent, but not yet acknowledged. In other words, the total accumulated duration in microseconds when there was something to deliver (non-empty senders' buffer). Available for sender.

### pktSndDropTotal

The total number of "too late to send" packets dropped by the sender (refer to `SRTO_TLPKTDROP` in [API.md](API.md)). Available for sender.

The total delay before TLPKTDROP mechanism is triggered consists of the `SRTO_PEERLATENCY`, plus `SRTO_SNDDROPDELAY`, plus 2 * the ACK interval (default ACK interval is 10 ms). The delay used is the timespan between the very first packet and the latest packet in the sender's buffer.

### pktRcvDropTotal

The total number of "too late to deliver" missing packets. Available for receiver.

Missing packets means lost or not yet received out-of-order packets. The receiver drops only those packets that are missing by the time there is at least one packet ready to be delivered to the upstream application.

Also includes packets that failed to be decrypted (see [pktRcvUndecryptTotal](#pktRcvUndecryptTotal)). These packets are present in the receiver's buffer and not dropped at the moment the decryption has failed.

 `SRTO_TSBPDMODE` and `SRTO_TLPKTDROP` socket options should be enabled (refer to in [API.md](API.md)).

### pktRcvUndecryptTotal

The total number of packets that failed to be decrypted at the receiver side. Available for receiver.

### pktSndFilterExtraTotal

The total number of packet filter control packets supplied by the packet filter (refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md)). Available for sender.

Packet filter control packets are SRT DATA packets.

`SRTO_PACKETFILTER` socket option should be enabled (refer to in [API.md](API.md)). Introduced in SRT v1.4.0.

### pktRcvFilterExtraTotal

The total number of packet filter control packets received and not supplied back by the packet filter
(refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md)). Available for receiver.

Packet filter control packets are SRT DATA packets.

For FEC, this is the total number of received FEC control packets.

`SRTO_PACKETFILTER` socket option should be enabled (refer to in [API.md](API.md)). Introduced in SRT v1.4.0.

### pktRcvFilterSupplyTotal

The total number of packets supplied by the packet filter excluding actually received packets
(e.g., FEC rebuilt packets, refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md)). Available for receiver.

Packet filter control packets are SRT DATA packets.

`SRTO_PACKETFILTER` socket option should be enabled (refer to in [API.md](API.md)). Introduced in SRT v1.4.0.

### pktRcvFilterLossTotal

The total number of lost packets that were not covered by the packet filter (refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md)). Available for receiver.

Packet filter control packets are SRT DATA packets.

`SRTO_PACKETFILTER` socket option should be enabled (refer to in [API.md](API.md)). Introduced in SRT v1.4.0.

### byteSentTotal

Same as [pktSentTotal](#pktSentTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for sender.

### byteRecvTotal

Same as [pktRecvTotal](#pktRecvTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for receiver.

### byteRcvLossTotal

Same as [pktRcvLossTotal](#pktRcvLossTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Bytes for the presently missing (either reordered or lost) packets' payloads are estimated based on the average packet size. Available for receiver.

### byteRetransTotal

Same as [pktRetransTotal](#pktRetransTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for sender.

### byteSndDropTotal

Same as [pktSndDropTotal](#pktSndDropTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for sender.

### byteRcvDropTotal

Same as [pktRcvDropTotal](#pktRcvDropTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Bytes for the dropped packets' payloads are estimated based on the average packet size. Available for receiver.

### byteRcvUndecryptTotal

Same as [pktRcvUndecryptTotal](#pktRcvUndecryptTotal), but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for receiver.


## Interval-Based Statistics

### pktSent

Same as `pktSentTotal`, but for a specified interval. Available for sender.

### pktRecv

Same as `pktRecvTotal`, but for a specified interval. Available for receiver.

### pktSndLoss

Same as `pktSndLossTotal`, but for a specified interval. Available for sender.

### pktRcvLoss

Same as `pktRcvLossTotal`, but for a specified interval. Available for receiver.

### pktRetrans

Same as `pktRetransTotal`, but for a specified interval. Available for sender.

### pktRcvRetrans

Same as `pktRcvRetransTotal`, but for a specified interval.

TODO: 

- There is no `pktRcvRetransTotal` stats.
- Which side
- include in table

### pktSentACK

Same as `pktSentACKTotal`, but for a specified interval.

TODO: Update accordingly.

### pktRecvACK

Same as `pktRecvACKTotal`, but for a specified interval.

TODO: Update accordingly.

### pktSentNAK

Same as `pktSentNAKTotal`, but for a specified interval.

TODO: Update accordingly.

### pktRecvNAK

Same as `pktRecvNAKTotal`, but for a specified interval.

TODO: Update accordingly.

### pktSndFilterExtra

Same as `pktSndFilterExtraTotal`, but for a specified interval. Sender only.

Introduced in v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

### pktRcvFilterExtra

Same as `pktRcvFilterExtraTotal`, but for a specified interval. Receiver only.

Introduced in SRT v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

### pktRcvFilterSupply

Same as `pktRcvFilterSupplyTotal`, but for a specified interval. Receiver only.

Introduced in SRT v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

### pktRcvFilterLoss

Same as `pktRcvFilterLossTotal`, but for a specified interval. Receiver only.

Introduced in SRT v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

### mbpsSendRate

Sending rate in Mbps. Sender side.

TODO: How it is calculated?

### mbpsRecvRate

Receiving rate in Mbps. Receiver side.

TODO: How it is calculated?

### usSndDuration

Same as `usSndDurationTotal`, but measured on a specified interval. Available for sender.

### pktReorderDistance

TODO: How it is calculated? Why it is interval based?

The distance in sequence numbers between two original (not retransmitted) packets received out of order. Receiver only.

The traceable distance values are limited by the maximum reorder tolerance set by  `SRTO_LOSSMAXTTL`.

### pktReorderTolerance

TODO: 
- Why it is in interval-based statistics?
- broken link

Instant value of the packet reorder tolerance (refer to [pktReorderDistance](#pktReorderDistance)). Receiver side. 

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

### pktRcvAvgBelatedTime

TODO: What's this?

Accumulated difference between the current time and the time-to-play of a packet 
that is received late.

### pktRcvBelated

TODO: Revise this, which side, measured over the interval

The number of packets received but IGNORED due to having arrived too late.

Makes sense only if TSBPD and TLPKTDROP are enabled.

An offset between sequence numbers of the newly arrived DATA packet and latest 
acknowledged DATA packet is calculated.
If the offset is negative, the packet is considered late, meaning that it was 
either already acknowledged or dropped by TSBPD as too late to be delivered.

Retransmitted packets can also be considered late.

### pktSndDrop

Same as `pktSndDropTotal`, but for a specified interval. Available for sender.

### pktRcvDrop

Same as `pktRcvDropTotal`, but for a specified interval. Available for receiver.

### pktRcvUndecrypt

Same as `pktRcvUndecryptTotal`, but for a specified interval. Available for receiver.

### byteSent

Same as `byteSentTotal`, but for a specified interval. Available for sender.

### byteRecv

Same as `byteRecvTotal`, but for a specified interval. Available for receiver.

### byteRcvLoss

Same as `byteRcvLossTotal`, but for a specified interval. Available for receiver.

### byteRetrans

Same as `byteRetransTotal`, but for a specified interval. Available for sender.

### byteSndDrop

Same as `byteSndDropTotal`, but for a specified interval. Available for sender.

### byteRcvDrop

Same as `byteRcvDropTotal`, but for a specified interval. Available for receiver.

### byteRcvUndecrypt

Same as `byteRcvUndecryptTotal`, but for a specified interval. Available for receiver.


## Instantaneous Statistics

### usPktSndPeriod

TODO: 

- How is this calculated? The minimum time during which period?
- probing packets
- rephrase - They may have different pacing of the outgoing packets, but all the packets will
  be placed in the same sending queue, which may affect the send timing. - who may have sockets? which may affect?

The current minimum packet inter-sending time, in microseconds. Does not take into account the probing packet pairs. Available for sender.

`usPktSndPeriod` is the minimum time (minimum sending period) that must be kept
between two packets sent consecutively over the link used by an SRT socket.
It is not the EXACT time interval between two consecutive packets. In the case where the time spent by an 
application between sending two consecutive packets exceeds `usPktSndPeriod`, the next 
packet will be sent faster, or even immediately, to preserve the average sending rate.

Note that several SRT sockets sharing one outgoing port use the same sending queue.
They may have different pacing of the outgoing packets, but all the packets will
be placed in the same sending queue, which may affect the send timing.

### pktFlowWindow

TODO:

- Rephrase this - The maximum number of packets that can be "in flight" state. - it does not reflect the idea
- revise the whole paragraph

The maximum number of packets that can be "in flight" state. See also [pktFlightSize](#pktFlightSize). Available for sender.

The value retrieved on the sender side represents an estimation of the amount of free space left in the SRT receiver buffer. The actual amount of available space is periodically reported back by the receiver in ACK packets. When this value drops to zero, the next packet sent (??? Received) will be dropped by the receiver without processing. 

In **file mode**, this may cause a slowdown of packets' sending in order to wait until the receiver has more space available, after it eventually extracts the packets waiting in its receiver buffer. In **live mode**, the receiver buffer contents should normally occupy not more than half of the buffer size (default 8192 - ??? Packets). If `pktFlowWindow` value is less than that and becomes even less in the next reports, it means that the receiver application on the peer side cannot process the incoming stream fast enough and this may lead to a dropped connection.

### pktCongestionWindow

The current congestion window size, in packets. Sender only.

The congestion window dynamically limits the maximum number of packets that can be "in flight" state. During transmission, congestion control module dynamically changes the size of congestion window.

In **file mode**, the size of congestion control window is equal to 16 packets at the very beginning and is increased during transmision to the number of reported acknowledged packets. This value is also updated based on the delivery rate reported by the receiver. It represents the maximum number of packets that can be safely sent without causing network congestion. The higher this value is, the faster the packets can be sent. In **live mode**, this field is not used.

### pktFlightSize

The number of packets in flight. Sender only.

The number of packets in flight is calculated as the difference between sequence numbers of the latest acknowledged packet (latest reported by an ACK message packet) and the latest sent packet at the moment statistic is being read. Note that `pktFlightSize <= pktFlowWindow` and `pktFlightSize <= pktCongestionWindow`.

**NOTE:** ACKs are received by the SRT sender periodically at least every 10 milliseconds. This statistic is most accurate just after receiving an ACK packet and becomes a little exaggerated over time until the next ACK packet arrives. This is because with a new packet sent, while the ACK number stays the same for a moment, the value of `pktFlightSize` increases. But the exact number of packets arrived since the last ACK report is unknown. A new statistic might be added to only report the distance between the ACK sequence number and the sent packet sequence number at the moment when an ACK arrives. This statistic will not be updated until the next ACK packet arrives. The difference between the suggested statistic and `pktFlightSize` would then reveal the number of packets with an unknown state at that moment.

### msRTT

The estimation for the round-trip time (RTT), in milliseconds. Available both for sender and receiver.

This value is calculated by the SRT receiver based on the incoming ACKACK control packets (sent back by the SRT sender to acknowledge incoming ACKs).

TODO: peer to agent terminology, with the same journal

The round-trip time (RTT) is the sum of two single-trip time (STT) values: one from agent to peer, the other from peer to agent. Note that the measurement method is different the method used in TCP. SRT measures only the "reverse RTT", that is, the time measured at the receiver between sending an ACK packet until receiving back the sender's ACKACK response message (with the same journal). This happens to be a little different from the "forward RTT" measured in TCP, which is the time between sending a data packet of a particular sequence number and receiving an ACK with a sequence number that is later by 1. Forward RTT isn't being measured or reported in SRT, although some research works have shown that these values, even though they should be the same, happen to differ; "reverse RTT" seems to be more optimistic.

### mbpsBandwidth

The estimation of the available bandwidth of the network link, in Mbps. Available both for sender and receiver.

At the protocol level, bandwidth and delivery rate estimations are calculated at the receiver side and used primarily as well as packet loss ratio and other protocol statistics for smoothed sending rate adjustments during the file transmition process (in congestion control module). This statistic is also available in live mode.

TODO: 

- What about stats on sender and rcv? smoothed average?
- there is no receiving speed stats, see also mbpsRecvRate; make a note here

The receiver records the inter-arrival time of each packet (time delta with the previous data packet) which is further used in the models to estimate bandwidth and receiving speed. The communication between receiver and sender happens by means of acknowledgment packets which are sent regularly (each 10 milliseconds) and contain some control information as well as bandwidth and delivery rate estimations. At the sender side, upon receiving a new value, a smoothed average is used to update the latest estimation mantained at the sender side.

It is important to note that for bandwidth estimation only data probing packets are taken into account while all the data packets (both data and data probing) are used for receiving speed estimation. The idea behind packet pair techniques is to send the groups of back-to-back packets, i.e., probing packet pairs, to a server thus making it possible to measure the minimum interval in receiving the consecutive packets.

### byteAvailSndBuf

The available space in the SRT sender buffer, in bytes. Sender only.

TODO: 

- increases first, decreases second or good?
- after the packets are sent over the UDP link - ?

This value decreases with the data scheduled for sending by the application and increases with every ACK received from the SRT receiver after the packets are sent over the UDP link.

### byteAvailRcvBuf

The available space in the SRT receiver buffer, in bytes. Receiver only.

TODO: SRT socket?

This value increases after the application extracts the data from the socket (uses one of the `srt_recv*` functions) and decreases with every packet received from the SRT sender over the UDP link.

### mbpsMaxBW

TODO: Revise

The transmission bandwidth limit, in Mbps. Available for sender.

Usually this is the setting from the `SRTO_MAXBW` option, which may include the value 0 (unlimited). Under certain conditions a nonzero value might be be provided by a congestion control module, although none of the built-in congestion control modules currently use it.

Refer to `SRTO_MAXBW` and `SRTO_INPUTBW` in [API.md](API.md).

### byteMSS

The maximum segment size (MSS), in bytes. Available for both sender and receiver.

Same as the value from the `SRTO_MSS` socket option. Should not exceed the size of the maximum transmission unit (MTU), in bytes. The default size of the UDP packet used for transport, including all possible headers (Ethernet, IP and UDP), is 1500 bytes.

Refer to `SRTO_MSS` in [API.md](API.md).

### pktSndBuf

The number of packets in the SRT sender's buffer that are already scheduled for sending or even possibly sent, but not yet acknowledged. Available for sender.

Once the SRT receiver acknowledges the receipt of a packet, or the Too-Late Packet Drop (TLPKTDROP) is triggered, the packet is removed from the sender's buffer. Until this happens, the packet is considered as unacknowledged.

TODO: ???

A moving average value is reported when the value is retrieved by calling `srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)` with `instantaneous=false`. The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

### byteSndBuf

Same as `pktSndBuf`, but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for sender.

### msSndBuf

The timespan of packets in the sender's buffer (unacknowledged packets), in milliseconds. Available for sender.

A moving average value is reported when the value is retrieved by calling `srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`
with `instantaneous=false`. The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

### msSndTsbPdDelay

The Timestamp Based Packet Delivery (TSBPD) delay value of the peer, in milliseconds. Available for sender.

TODO: 

- ???
- seems to be only for sender

If `SRTO_TSBPDMODE` is on (default for **live mode**), it 
returns the value of `SRTO_PEERLATENCY`, otherwise 0.
The sender reports the TSBPD delay value of the receiver.
The receiver reports the TSBPD delay of the sender.

/// TsbpdDelay is the receiver's buffer delay (or receiver's buffer
      latency, or SRT Latency).  This is the time, in milliseconds, that
      SRT holds a packet from the moment it has been received till the
      time it should be delivered to the upstream application

### pktRcvBuf

The number of acknowledged packets in the receiver's buffer. Receiver only.

This statistic does not include received but not acknowledged packets stored in the receiver's buffer.

A moving average value is reported when the value is retrieved by calling `srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`
with `instantaneous=false`. The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

### byteRcvBuf

The instantaneous value of `pktRcvBuf`, expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Available for receiver.

### msRcvBuf

The timespan of acknowledged packets in the receiver's buffer, in milliseconds. Available for receiver.

If TSBPD mode is enabled (defualt for **live mode**), a packet can be acknowledged, but not yet ready to play.
This range includes all packets regardless of whether they are ready to play or not.

A moving average value is reported when the value is retrieved by calling `srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`
with `instantaneous=false`. The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

Instantaneous value is only reported if TSBPD mode is enabled, otherwise 0 is reported (see #900).

### msRcvTsbPdDelay

The Timestamp Based Packet Delivery (TSBPD) delay value set on the socket via `SRTO_RCVLATENCY` or `SRTO_LATENCY`, in milliseconds. Available for receiver.

The value is used to apply TSBPD delay for reading the received data on the socket.

If `SRTO_TSBPDMODE` is off (default for **file mode**), 0 is returned.