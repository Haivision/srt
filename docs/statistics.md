SRT provides a powerful set of statistical data on a socket.
This data can be used to keep an eye on a socket's health,
and track faulty behavior.

Statistics are calculated independently on each side (receiver and sender),
and are not exchanged between peers, unless explicitly stated.

TODO: unless explicitly stated ???

The following API functions can be used to retrieve statistics on an SRT socket.
Refer to the documentation of the [API functions](API-functions.md) for usage instructions.

* `int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear)`
* `int srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous)`



# Summary Table

TODO:

- Data type (int, float) - ?

- Absolute timestamp, but it's not a statistics

- Explain the difference between accumulated, and other types of stats

- go through empty and ??? cells

  

| Statistic               | Type of Statistic | Measured in       | Available for Sender | Available for Receiver | Data Type |
| ----------------------- | ----------------- | ----------------- | -------------------- | ---------------------- | --------- |
|                         |                   |                   |                      |                        |           |
| msTimeStamp             | accumulated       | ms (milliseconds) | ✓                    | ✓                      | int64_t   |
| pktSentTotal            | accumulated       | packets           | ✓                    | -                      | int64_t   |
| pktRecvTotal            | accumulated       | packets           | -                    | ✓                      | int64_t   |
| pktSndLossTotal         | accumulated       | packets           | ✓                    | -                      | int       |
| pktRcvLossTotal         | accumulated       | packets           | -                    | ✓                      | int       |
| pktRetransTotal         | accumulated       | packets           | ✓                    | -                      | int       |
| pktSentACKTotal         | accumulated       | packets           |                      |                        | int       |
| pktRecvACKTotal         | accumulated       | packets           |                      |                        | int       |
| pktSentNAKTotal         | accumulated       | packets           |                      |                        | int       |
| pktRecvNAKTotal         | accumulated       | packets           |                      |                        | int       |
| usSndDurationTotal      | accumulated       | us (microseconds) | ✓                    | -                      | int64_t   |
| pktSndDropTotal         | accumulated       | packets           | ✓                    | -                      | int       |
| pktRcvDropTotal         | accumulated       | packets           | -                    | ✓                      | int       |
| pktRcvUndecryptTotal    | accumulated       | packets           | -                    | ✓                      | int       |
| pktSndFilterExtraTotal  | accumulated       | packets           | ✓                    | -                      | ???       |
| pktRcvFilterExtraTotal  | accumulated       | packets           | -                    | ✓                      | ???       |
| pktRcvFilterSupplyTotal | accumulated       | packets           | -                    | ✓                      | ???       |
| pktRcvFilterLossTotal   | accumulated       | packets           | -                    | ✓                      | ???       |
| byteSentTotal           | accumulated       | bytes             | ✓                    | -                      | uint64_t  |
| byteRecvTotal           | accumulated       | bytes             | -                    | ✓                      | uint64_t  |
| byteRcvLossTotal        | accumulated       | bytes             | -                    | ✓                      | uint64_t  |
| byteRetransTotal        | accumulated       | bytes             | ✓                    | -                      | uint64_t  |
| byteSndDropTotal        | accumulated       | bytes             | ✓                    | -                      | uint64_t  |
| byteRcvDropTotal        | accumulated       | bytes             | -                    | ✓                      | uint64_t  |
| byteRcvUndecryptTotal   | accumulated       | bytes             | -                    | ✓                      | uint64_t  |
| pktSent                 | interval-based    | packets           | ✓                    | -                      | int64_t   |
| pktRecv                 | interval-based    | packets           | -                    | ✓                      | int64_t   |
| pktSndLoss              | interval-based    | packets           | ✓                    | -                      | int       |
| pktRcvLoss              | interval-based    | packets           | -                    | ✓                      | int       |
| pktRetrans              | interval-based    | packets           | ✓                    | -                      | int       |
|                         |                   |                   |                      |                        |           |
| pktSentACK              | interval-based    | packets           |                      |                        | int       |
| pktRecvACK              | interval-based    | packets           |                      |                        | int       |
| pktSentNAK              | interval-based    | packets           |                      |                        | int       |
| pktRecvNAK              | interval-based    | packets           |                      |                        | int       |
| pktSndFilterExtra       | interval-based    | packets           | ✓                    | -                      | ???       |
| pktRcvFilterExtra       | interval-based    | packets           | -                    | ✓                      | ???       |
| pktRcvFilterSupply      | interval-based    | packets           | -                    | ✓                      | ???       |
| pktRcvFilterLoss        | interval-based    | packets           | -                    | ✓                      | ???       |
| mbpsSendRate            | interval-based    | Mbps              | ✓                    | -                      | double    |
| mbpsRecvRate            | interval-based    | Mbps              | -                    | ✓                      | double    |
| usSndDuration           | interval-based    | us (microseconds) | ✓                    | -                      | int64_t   |
| pktReorderDistance      | interval-based    |                   |                      |                        |           |
| pktReorderTolerance     | interval-based    |                   |                      |                        |           |
| pktRcvAvgBelatedTime    |                   |                   |                      |                        |           |
| pktRcvBelated           |                   |                   |                      |                        |           |
| pktSndDrop              | interval-based    | packets           | ✓                    | -                      | int       |
| pktRcvDrop              | interval-based    | packets           | -                    | ✓                      | int       |
| pktRcvUndecrypt         | interval-based    | packets           | -                    | ✓                      | int       |
| byteSent                | interval-based    | bytes             | ✓                    | -                      | uint64_t  |
| byteRecv                | interval-based    | bytes             | -                    | ✓                      | uint64_t  |
| byteRcvLoss             | interval-based    | bytes             | -                    | ✓                      | uint64_t  |
| byteRetrans             | interval-based    | bytes             | ✓                    | -                      | uint64_t  |
| byteSndDrop             | interval-based    | bytes             | ✓                    | -                      | uint64_t  |
| byteRcvDrop             | interval-based    | bytes             | -                    | ✓                      | uint64_t  |
| byteRcvUndecrypt        | interval-based    | bytes             | -                    | ✓                      | uint64_t  |



# Accumulated Statistics

TODO: 

- Header level?

## msTimeStamp

The time elapsed since the SRT socket was started (after successful call to `srt_connect(...)` or `srt_bind(...)` function), in milliseconds. Available both for sender and receiver.

TODO: was created?

## pktSentTotal

The total number of sent data packets, including retransmitted packets. Available for sender.

## pktRecvTotal

The total number of received packets, including retransmitted packets. Available for receiver.

## pktSndLossTotal

The total number of data packets considered or reported as lost at the sender side. Does not correspond to the packets detected as lost at the receiver side. Available for sender.

A packet is considered lost in two cases: 
1. Sender receives a loss report from a receiver,
2. Sender initiates retransmission after not receiving an ACK packet for a certain timeout. Refer to `FASTREXMIT` and `LATEREXMIT` algorithms.

## pktRcvLossTotal

TODO: Update the description.

The total number of data packets detected as lost at the receiver side. Available for receiver.

If a packet is received out of order, the gap (sequence discontinuity) is also taken into account and the number of lost packets is increased by the discontinuity size independently of the reordering tolerance value.

Lost packets detection is based on the gaps in sequence numbers of the SRT DATA packets and is triggered by a newly received packet. An offset is calculated between sequence numbers of the newly arrived DATA packet and the previously received DATA packet (the packet with the highest sequence number). Receiving older packets does not affect this value. The packets from that gap are considered lost,
and that number is added to the `pktRcvLossTotal` measurement. In the case where
the offset is negative, the packet is considered late, meaning that it was either
already acknowledged or dropped by TSBPD as too late to be delivered. Such late
packets are ignored.

TODO: This is wrong.

Since SRT v1.4.0, includes packets that failed to be decrypted.

## pktRetransTotal

The total number of retransmitted packets. Calculated at the sender side only.
Not exchanged with the receiver.

TODO: 

- We can calculate retransmitted packets both at the sender side and at the receiver side which is stated in pktSentTotal and pktRecvTotal stats -> we should have this statistics available both for snd and rcv. The same for byteRetransTotal
- Not exchanged with the receiver: what does it mean?

## pktSentACKTotal

The total number of sent ACK packets. Available for sender.

TODO: 

- abbreviation
- possible mistake, see pktSentNAKTotal

## pktRecvACKTotal

The total number of received ACK packets. Available for receiver.

TODO: Possible mistake, see pktSentNAKTotal.

## pktSentNAKTotal

The total number of NAK packets received by the sender from the receiver. Essentially means LOSS reports. Available for sender.

TODO: 

- abbreviation
- sent -> received - right? or if it's sent than it's receiver statistics. Correct in the table as well.

## pktRecvNAKTotal

The total number of NAK packets sent back to the sender by the receiver. Essentially means LOSS reports. Available for receiver.

TODO: received -> sent? Or if it's received, than it's sender statistics. Correct in the table as well.

## usSndDurationTotal

The total accumulated time in microseconds, during which the SRT sender has some 
data to transmit, including packets that have been sent, but not yet acknowledged. In other words, the total accumulated duration in microseconds when there was something to deliver (non-empty senders' buffer). Available for sender.

## pktSndDropTotal

The total number of "too late to send" packets dropped by the sender (refer to `TLPKTDROP`). Available for sender.

TODO: Update what's below

The total delay before `TLPKTDROP` is triggered consists of the `SRTO_PEERLATENCY`, 
plus `SRTO_SNDDROPDELAY`, plus 2 * the ACK interval (default ACK interval is 10 ms).
The delay used is the timespan between the very first packet and the latest packet 
in the sender's buffer.


## pktRcvDropTotal

TODO: 

- missing?
- Revise the text

The total number of "too late to deliver" missing packets. Available for receiver.

TSBPD and TLPacket drop socket option should be enabled.

The receiver drops only those packets that are missing by the time there is at 
least one packet ready to play.

Also includes packets that failed to be decrypted (pktRcvUndecryptTotal). These 
packets are present in the receiver's buffer, and not dropped directly. 

## pktRcvUndecryptTotal

The total number of packets that failed to be decrypted at the receiver side. Available for receiver.

TODO: Is it correct that we include this number in the other statistics?

## pktSndFilterExtraTotal

The total number of control packets supplied by the packet filter (refer to
[SRT Packet Filtering & FEC](packet-filtering-and-fec.md)). Sender only.

Introduced in SRT v1.4.0.

## pktRcvFilterExtraTotal

TODO: 

- I do not understand the description, check the whole group.
- for the whole group we should also mention that this statisitcs is available if FEC option is enabled

The total number of control packets received and not supplied back by the packet filter
(refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md)). Receiver only.

Introduced in SRT v1.4.0.

## pktRcvFilterSupplyTotal

The total number of packets supplied by the packet filter excluding actually received packets
(e.g.,  FEC rebuilt, refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md)). Receiver only.

Introduced in SRT v1.4.0.

## pktRcvFilterLossTotal

The total number of lost packets that were not covered by the packet filter (refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md)). Receiver only.

Introduced in SRT v1.4.0.

## byteSentTotal

Same as `pktSentTotal`, but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Sender side.

## byteRecvTotal

Same as `pktRecvTotal`, but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Receiver side.

## byteRcvLossTotal

Same as `pktRcvLossTotal`, but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Receiver side.

## byteRetransTotal

Same as `pktRetransTotal`, but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Sender side.

## byteSndDropTotal

Same as `pktSndDropTotal`, but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Sender side.

## byteRcvDropTotal

Same as `pktRcvDropTotal`, but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Bytes for the dropped packets' payloads are estimated based on the average packet size. Receiver side.

TODO: Bytes for the dropped packets' payloads are estimated based on the average packet size. - What's about the other stats from this group?

## byteRcvUndecryptTotal

Same as `pktRcvUndecryptTotal`, but expressed in bytes, including payload and all the headers (20 bytes IPv4 + 8 bytes UDP + 16 bytes SRT). Receiver side.

# Interval-Based Statistics

TODO: Update the description

These values can be reset by calling `srt_bstats(..., int clear)` with `clear = 1`. \
This is helpful to get statistical measurements within a certain period, e.g. 1 second.

## pktSent

Same as `pktSentTotal`, but for a specified interval. Available for sender.

## pktRecv

Same as `pktRecvTotal`, but for a specified interval. Available for receiver.

## pktSndLoss

Same as `pktSndLossTotal`, but for a specified interval. Available for sender.

## pktRcvLoss

Same as `pktRcvLossTotal`, but for a specified interval. Available for receiver.

## pktRetrans

Same as `pktRetransTotal`, but for a specified interval. Available for sender.

## pktRcvRetrans

Same as `pktRcvRetransTotal`, but for a specified interval.

TODO: 

- There is no `pktRcvRetransTotal` stats.
- Which side
- include in table

## pktSentACK

Same as `pktSentACKTotal`, but for a specified interval.

TODO: Update accordingly.

## pktRecvACK

Same as `pktRecvACKTotal`, but for a specified interval.

TODO: Update accordingly.

## pktSentNAK

Same as `pktSentNAKTotal`, but for a specified interval.

TODO: Update accordingly.

## pktRecvNAK

Same as `pktRecvNAKTotal`, but for a specified interval.

TODO: Update accordingly.

## pktSndFilterExtra

Same as `pktSndFilterExtraTotal`, but for a specified interval. Sender only.

Introduced in v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

## pktRcvFilterExtra

Same as `pktRcvFilterExtraTotal`, but for a specified interval. Receiver only.

Introduced in SRT v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

## pktRcvFilterSupply

Same as `pktRcvFilterSupplyTotal`, but for a specified interval. Receiver only.

Introduced in SRT v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

## pktRcvFilterLoss

Same as `pktRcvFilterLossTotal`, but for a specified interval. Receiver only.

Introduced in SRT v1.4.0. Refer to [SRT Packet Filtering & FEC](packet-filtering-and-fec.md).

## mbpsSendRate

Sending rate in Mbps. Sender side.

## mbpsRecvRate

Receiving rate in Mbps. Receiver side.

## usSndDuration

Same as `usSndDurationTotal`, but measured on a specified interval. Available for sender.

## pktReorderDistance

TODO

The distance in sequence numbers between the two original (not retransmitted) packets,
that were received out of order. Receiver only.

The traceable distance values are limited by the maximum reorder tolerance set by  `SRTO_LOSSMAXTTL`.

## pktReorderTolerance

TODO

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

## pktRcvAvgBelatedTime

TODO: What's this?

Accumulated difference between the current time and the time-to-play of a packet 
that is received late.

## pktRcvBelated

TODO: Revise this, which side, measured over the interval

The number of packets received but IGNORED due to having arrived too late.

Makes sense only if TSBPD and TLPKTDROP are enabled.

An offset between sequence numbers of the newly arrived DATA packet and latest 
acknowledged DATA packet is calculated.
If the offset is negative, the packet is considered late, meaning that it was 
either already acknowledged or dropped by TSBPD as too late to be delivered.

Retransmitted packets can also be considered late.

## pktSndDrop

Same as `pktSndDropTotal`, but for a specified interval. Available for sender.

## pktRcvDrop

Same as `pktRcvDropTotal`, but for a specified interval. Available for receiver.

## pktRcvUndecrypt

Same as `pktRcvUndecryptTotal`, but for a specified interval. Available for receiver.

## byteSent

Same as `byteSentTotal`, but for a specified interval. Available for sender.

## byteRecv

Same as `byteRecvTotal`, but for a specified interval. Available for receiver.

## byteRcvLoss

Same as `byteRcvLossTotal`, but for a specified interval. Available for receiver.

## byteRetrans

Same as `byteRetransTotal`, but for a specified interval. Available for sender.

## byteSndDrop

Same as `byteSndDropTotal`, but for a specified interval. Available for sender.

## byteRcvDrop

Same as `byteRcvDropTotal`, but for a specified interval. Available for receiver.

## byteRcvUndecrypt

Same as `byteRcvUndecryptTotal`, but for a specified interval. Available for receiver.

# Instant measurements

The measurements effective at the time retrieved.

## usPktSndPeriod

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

## pktFlowWindow

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


## pktCongestionWindow

Congestion window size, in number of packets. Sender only.

Dynamically limits the maximum number of packets that can be in flight.
Congestion control module dynamically changes the value.

In **file mode**  this value starts at 16 and is increased to the number of reported
acknowledged packets. This value is also updated based on the delivery rate, reported by the receiver.
It represents the maximum number of packets that can be safely
sent without causing network congestion. The higher this value is, the faster the
packets can be sent. In **live mode** this field is not used.

## pktFlightSize

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

## msRTT

Calculated Round trip time (RTT), in milliseconds. Sender and Receiver. \
The value is calculated by the receiver based on the incoming ACKACK control packets
(used by sender to acknowledge ACKs from receiver).

The RTT (Round-Trip time) is the sum of two STT (Single-Trip time) 
values, one from agent to peer, and one from peer to agent. Note that **the 
measurement method is different than in TCP**. SRT measures only the "reverse
RTT", that is, the time measured at the receiver between sending a `UMSG_ACK`
message until receiving the sender's `UMSG_ACKACK` response message (with the
same journal). This happens to be a little different from the "forward RTT"
measured in TCP, which is the time between sending a data packet of a particular 
sequence number and receiving `UMSG_ACK` with a sequence number that is later 
by 1. Forward RTT isn't being measured or reported in SRT, although some
research works have shown that these values, even though they should be the same,
happen to differ; "reverse RTT" seems to be more optimistic.

## mbpsBandwidth

Estimated bandwidth of the network link, in Mbps. Sender only.

The bandwidth is estimated at the receiver.
The estimation is based on the time between two probing DATA packets.
Every 16th data packet is sent immediately after the previous data packet.
By measuring the delay between probe packets on arrival,
it is possible to estimate the maximum available transmission rate,
which is interpreted as the bandwidth of the link.
The receiver then sends back a running average calculation to the sender with an ACK message.

## byteAvailSndBuf

The available space in the sender's buffer, in bytes. Sender only.

This value decreases with data scheduled for sending by the application, and increases 
with every ACK received from the receiver, after the packets are sent over 
the UDP link.

## byteAvailRcvBuf

The available space in the receiver's buffer, in bytes. Receiver only.

This value increases after the application extracts the data from the socket
(uses one of `srt_recv*` functions) and decreases with every packet received
from the sender over the UDP link.

## mbpsMaxBW

Transmission bandwidth limit, in Mbps. Sender only.
Usually this is the setting from 
the `SRTO_MAXBW` option, which may include the value 0 (unlimited). Under certain 
conditions a nonzero value might be be provided by a congestion 
control module, although none of the built-in congestion control modules 
currently use it.

Refer to `SRTO_MAXBW` and `SRTO_INPUTBW` in [API.md](API.md).

## byteMSS

Maximum Segment Size (MSS), in bytes.
Same as the value from the `SRTO_MSS` socket option.
Should not exceed the size of the maximum transmission unit (MTU), in bytes. Sender and Receiver.
The default size of the UDP packet used for transport,
including all possible headers (Ethernet, IP and UDP), is 1500 bytes.

Refer to `SRTO_MSS` in [API.md](API.md).

## pktSndBuf

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

Timestamp-based Packet Delivery Delay value of the peer.
If `SRTO_TSBPDMODE` is on (default for **live mode**), it 
returns the value of `SRTO_PEERLATENCY`, otherwise 0.
The sender reports the TSBPD delay value of the receiver.
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

## msRcvTsbPdDelay

Timestamp-based Packet Delivery Delay value set on the socket via `SRTO_RCVLATENCY` or `SRTO_LATENCY`.
The value is used to apply TSBPD delay for reading the received data on the socket. Receiver side.

If `SRTO_TSBPDMODE` is off (default for **file mode**), 0 is returned.

