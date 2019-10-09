SRT provides a powerful set of statistical data on a socket.
This data can be used to keep an eye on socket's health,
and track some faulty situations as well.

Statistics are calculated independently on each side (receiver and sender),
and are not exchanged between peers, unless explicitly stated.

The following API functions can be used to retrieve statistics on an SRT socket.
Refer to the documentation of the [API functions](API-functions.md) for usage instructions.
* `int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear)`
* `int srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`


# Total accumulated measurements

## msTimeStamp

Time elapsed since the SRT socket was started (after successful call to `srt_connect(...)` or `srt_bind(...)`), in milliseconds.

## pktSentTotal

The total number of sent data packets, including retransmitted packets. Applicable for data sender.

## pktRecvTotal

The total number of received packets, including received retransmitted packets. Applicable for data receiver.

## pktSndLossTotal

The total number of data packets considered or reported lost (sender side). Does not correspond to packets detected as lost at receiving side. Applicable for data sender.

A packet is considered lost in two cases: 
1. Sender receives a loss report from receiver.
2. Sender initiates retransmission after not receiving ACK for a certain timeout. Refer to `FASTREXMIT` and `LATEREXMIT` algorithms.

## pktRcvLossTotal

The total number of data packets detected lost on the receiver's side.

Includes packets that failed to be decrypted (only as of SRT version 1.4.0).

If a packet was received out of order, the gap (sequence discontinuity) is also 
treated as lost packets, independent of the reorder tolerance value.

Loss detection is based on gaps in Sequence Numbers of SRT DATA packets. Detection 
of a packet loss is triggered by a newly received packet. An offset is calculated 
between sequence numbers of the newly arrived DATA packet and previously received 
DATA packet (the received packet with highest sequence number). Receiving older 
packets does not affect this value. The packets from that gap are considered lost, 
and that number is added to this `pktRcvLossTotal` measurement. In the case where 
the offset is negative, the packet is considered late, meaning that it was either 
already acknowledged or dropped by TSBPD as too late to be delivered. Such late 
packets are ignored.

## pktRetransTotal

The total number of retransmitted packets. Calculated on the sender's side only. 
Not exchanged with the receiver.

## pktSentACKTotal

The total number of sent ACK packets. Applicable for data sender.

## pktRecvACKTotal

The total number of received ACK packets. Applicable for data receiver.

## pktSentNAKTotal

The total number of NAK (Not Acknowledged) packets sent. Essentially means LOSS 
reports. Applicable for data sender.

## pktRecvNAKTotal

The total number of NAK (Not Acknowledged) packets received. Essentially means 
LOSS reports. Applicable for data receiver.

## usSndDurationTotal

The total accumulated time in microseconds, during which the SRT sender has some 
data to transmit, including packets that were sent, but is waiting for acknowledgement.
In other words, the total accumulated duration in microseconds when there was something 
to deliver (non-empty senders' buffer).
Applicable for data sender.

## pktSndDropTotal

The number of "too late to send" packets dropped by sender (refer to `TLPKTDROP`).

The total delay before `TLPKTDROP` is triggered consists of the `SRTO_PEERLATENCY`, 
plus `SRTO_SNDDROPDELAY`, plus 2 * the ACK interval (default ACK interval is 10 ms).
The delay used is the timespan between the very first packet and the latest packet 
in the sender's buffer.


## pktRcvDropTotal

The number of "too late to play" missing packets. Receiver only.

TSBPD and TLPacket drop socket option should be enabled.

The receiver drops only those packets that are missing by the time there is at 
least one packet ready to play.

Also includes packets that failed to be decrypted (pktRcvUndecryptTotal). These 
packets are present in the receiver's buffer, and not dropped directly. 
Potentially a bug.

## pktRcvUndecryptTotal

The number of packets that failed to be decrypted. Receiver side.

## pktSndFilterExtraTotal

The number of control packets supplied by the packet filter (refer to
[SRT Packet Filtering & FEC](packet-filtering-and-fec.md)). Sender only.

Introduced in v1.4.0.

## pktRcvFilterExtraTotal

The number of control packets received and not supplied back by the packet filter
(refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md)). Receiver only.

Introduced in v1.4.0.

## pktRcvFilterSupplyTotal

The number of packets supplied by the packet filter excluding actually received packets
(e.g. FEC rebuilt). Receiver only. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

Introduced in v1.4.0.

## pktRcvFilterLossTotal

The number of lost packets, that were not covered by the packet filter. Receiver only.
Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

Introduced in v1.4.0.

## byteSentTotal

Same as `pktSentTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Sender side.

## byteRecvTotal

Same as `pktRecvTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Receiver side.

## byteRcvLossTotal

Same as `pktRcvLossTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Receiver side.

## byteRetransTotal

Same as `pktRetransTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Sender side only.

## byteSndDropTotal

Same as `pktSndDropTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Sender side only.

## byteRcvDropTotal

Same as `pktRcvDropTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
Bytes of payload of dropped packets is estimated based on average packet size. \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Receiver side only. 

## byteRcvUndecryptTotal

Same as `pktRcvUndecryptTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Receiver side.

# Interval-based measurements

These values can be reset by calling `srt_bstats(..., int clear)` with `clear = 1`. \
This is helpful to get statistical measurements within a certain period, e.g. 1 second.

## pktSent

Same as `pktSentTotal`, but on the interval.

## pktRecv

Same as `pktRecvTotal`, but on the interval.

## pktSndLoss

Same as `pktSndLossTotal`, but on the interval.

## pktRcvLoss

Same as `pktRcvLossTotal`, but on the interval.

## pktRetrans

Same as `pktRetransTotal`, but on the interval.

## pktRcvRetrans

Same as `pktRcvRetransTotal`, but on the interval.

## pktSentACK

Same as `pktSentACKTotal`, but on the interval.

## pktRecvACK

Same as `pktRecvACKTotal`, but on the interval.

## pktSentNAK

Same as `pktSentNAKTotal`, but on the interval.

## pktRecvNAK

Same as `pktRecvNAKTotal`, but on the interval.

## pktSndFilterExtra

Same as `pktSndFilterExtraTotal`, but on the interval.

Introduced in v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

## pktRcvFilterExtra

Same as `pktRcvFilterExtraTotal`, but on the interval.

Introduced in v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

## pktRcvFilterSupply

Same as `pktRcvFilterSupplyTotal`, but on the interval.

Introduced in v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

## pktRcvFilterLoss

Same as `pktRcvFilterLossTotal`, but on the interval.

Introduced in v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

## mbpsSendRate

Sending rate in Mbps. Sender side.

## mbpsRecvRate

Receiving rate in Mbps. Receiver side.

## usSndDuration

Same as `usSndDurationTotal`, but measured on the interval.

## pktReorderDistance

`SRTO_LOSSMAXTTL` sets the maximum reorder tolerance value. The internal algorithm 
checks the order of incoming packets and adjusts the tolerance based on the reorder 
distance, but not to a value higher than the maximum.

SRT starts from 0 tolerance. Once it receives the first 
reordered packet, it increases the tolerance to the distance in the sequence
discontinuity of the two packets. \
After 10 consecutive original (not retransmitted) packets come in order, the reorder distance
is decreased by 1 with every such a packet.

For example, assume packets with the following sequence
numbers are being received: \
1, 2, 4, 3, 5, 7, 6, 10, 8, 9
SRT starts from 0 tolerance. Receiving packet with sequence number 4 has a discontinuity
equal to one packet. The loss is reported to the sender.
With the next packet (sequence number 3) a reordering is detected. Reorder tolerance is increased to 1. \
The next sequence discontinuity is detected when the packet with sequence number 7 is received.
The current tolerance value is 1, which equals to the gap. No loss is reported. \
Next packet with sequence number 10 has a higher sequence discontinuity equal to 2.
Missing packets with sequence numbers 8 and 9 will be reported lost with the next received packet (reorder distance 1).
The next received packet has sequence number 8. Reorder tolerance value is increased to 2.
The packet with sequence number 9 is reported lost.

## pktRcvAvgBelatedTime

Accumulated difference between the current time and the time-to-play of a packet 
that is received late.

## pktRcvBelated

The number of packets received but IGNORED due to having arrived too late.

Makes sense only if TSBPD and TLPKTDROP are enabled.

An offset between sequence numbers of the newly arrived DATA packet and latest 
acknowledged DATA packet is calculated.
If the offset is negative, the packet is considered late, meaning that it was 
either already acknowledged or dropped by TSBPD as too late to be delivered.

Retransmitted packets can also be considered late.

## pktSndDrop

Same as `pktSndDropTotal`, but on the interval.

## pktRcvDrop

Same as `pktRcvDropTotal`, but on the interval.

## pktRcvUndecrypt

Same as `pktRcvUndecryptTotal`, but on the interval.

## byteSent

Same as `byteSentTotal`, but on the interval.

## byteRecv

Same as `byteRecvTotal`, but on the interval.

## byteRcvLoss

Same as `byteRcvLossTotal`, but on the interval.

## byteRetrans

Same as `byteRetransTotal`, but on the interval.

## byteSndDrop

Same as `byteSndDropTotal`, but on the interval.

## byteRcvDrop

Same as `byteRcvDropTotal`, but on the interval.

## byteRcvUndecrypt

Same as `byteRcvUndecryptTotal`, but on the interval.

# Instant measurements

The measurements effective at the time retrieved.

## usPktSndPeriod

Current minimum time interval between which consecutive packets are sent, in 
microseconds. Sender only.

**Note**. Except for probing packets.

## pktFlowWindow

The maximum number of packets that can be in flight. Sender only.

## pktCongestionWindow

Congestion window size, in number of packets. Sender only.

Dynamically limits the maximum number of packets that can be in flight.
Congestion control module dynamically changes the value.

## pktFlightSize

The number of packets in flight. Sender only.

`pktFlightSize <= pktFlowWindow` and `pktFlightSize <= pktCongestionWindow`

## msRTT

Calculated Round trip time (RTT), in milliseconds. Sender and Receiver. \
The value is calculated by the receiver based on the incoming ACKACK control packets
(sender acknowledges that an ACK acknowledgement packet has been received)
of the ACK control packet sent by the receiver.

The receiver includes the latest RTT value in the ACK control packet, thus letting the
sender know the latest value.

## mbpsBandwidth

Estimated bandwidth of the network link, in Mbps. Sender only.

The estimated bandwidth is based on the time between two probing DATA packets.
Every 16-th data packet is sent immediately after the previous data packet.
Thus it is possible to estimate the maximum available transmission rate, that is considered
as the bandwidth of the link.

## byteAvailSndBuf

The available space in the sender's buffer, in bytes. Sender only.

## byteAvailRcvBuf

The available space in the receiver's buffer, in bytes. Receiver only.

## mbpsMaxBW

Transmission bandwidth limit, in Mbps. Sender only.

Refer to `SRTO_MAXBW` and `SRTO_INPUTBW` in [API.md](API.md).

## byteMSS

Maximum Segment Size (MSS), in bytes. Should not exceed the size of
the maximum transmission unit (MTU), in bytes. Sender and Receiver.

Refer to `SRTO_MSS` in [API.md](API.md).

## pktSndBuf

The number of unacknowledged packets in sender's buffer. Sender only.

Once the receiver acknowledges the receival of a packet, of the TL packet drop
is triggered, the packet is removed from the sender's buffer.
Until this happens, the packet is considered as unacknowledged.

A moving average value is reported when the value is retrieved by calling
`srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`
with `instantaneous=false`.

The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

## byteSndBuf

Instantaneous (current) value of `pktSndBuf`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Sender side.

## msSndBuf

The timespan (msec) of packets in the sender's buffer (unacknowledged packets). Sender only.

A moving average value is reported when the value is retrieved by calling
`srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`
with `instantaneous=false`.

The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

## msSndTsbPdDelay

Timestamp-based Packet Delivery Delay value of the peer. The sender reports TSBPD delay value of the receiver.
The receiver reports the TSBPD delay of the sender.

## pktRcvBuf

The number of acknowledged packets in receiver's buffer. Receiver only.

This measurement does not include received but not acknowledged packets, stored in the receiver's buffer.

A moving average value is reported when the value is retrieved by calling
`srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`
with `instantaneous=false`.

The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

## byteRcvBuf

Instantaneous (current) value of `pktRcvBuf`, expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Receiver side.

## msRcvBuf

The timespan (msec) of acknowledged (ready to play) packets in the receiver's buffer. Receiver side.

A moving average value is reported when the value is retrieved by calling
`srt_bstats(...)` or `srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`
with `instantaneous=false`.

The current state is returned if `srt_bistats(...)` is called with `instantaneous=true`.

Instantaneous value is only reported if TSBPD mode is enabled, otherwise 0 is reported (see #900).

## msRcvTsbPdDelay

Timestamp-based Packet Delivery Delay value set on the socket via `SRTO_RCVLATENCY` or `SRTO_LATENCY`.
The value is used to apply TSBPD delay for reading the received data on the socket. Receiver side.

