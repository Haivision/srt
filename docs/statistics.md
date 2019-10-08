SRT provides a powerful set of statistical data on a socket. This data can be used to keep an eye on socket's health, and track some faulty situations as well.

Statistics is calculated on each side (receiver and sender) independently, and is not exchanged between peers, unless explicitly stated.

## Total accumulated measurements

### msTimeStamp

Time elapsed since the SRT socket is started (after successful call to `srt_connect(...)` or `srt_bind(...)`), in milliseconds.

### pktSentTotal

A total number of sent data packets, including retransmitted packets. Applicable for data sender.

### pktRecvTotal

A total number of received packets, including received retransmitted packets. Applicable for data receiver.

### pktSndLossTotal

A total number of data packets considered or reported lost (sender side). Does not correspond to packets detected as lost at receiving side. Applicable for data sender.

A packet is considered lost in two cases: 
1. Sender receives a loss report from receiver.
2. Sender initiates retransmission after not receiving ACK for a certain timeout. Refer to `FASTREXMIT` and `LATEREXMIT` algorithms.

### pktRcvLossTotal

A total number of data packets detected lost on the receiver's side.

Does not count packets failed to be decrypted (v1.3.4 and before). Starting from version 1.4.0 also includes packets failed to be decrypted.

If a packet was received out of order, the gap (sequence discontinuity) is also treated as lost packets, independent of the reorder tolerance value.

Loss detection is based on gaps in Sequence Numbers of SRT DATA packets. Detection of a packet loss is triggered by a newly received packet.
An offset between sequence numbers of the newly arrived DATA packet and previously received DATA packet is calculated.
Those number of packets that from the gap are considered lost, and added to this `pktRcvLossTotal` measurement.

In case the offset is negative, the packet is considered belated, meaning that it was either already acknowledged or dropped by TSBPD as too late to be delivered. Such belated packets are ignored.

Here previously received DATA packet refers to the received packet with highest sequence number. This means that receiving older packets to not effect this value.

### pktRetransTotal

A total number of retransmitted packets. Calculated on the sender's side only. Not exchanged with the receiver.

### pktSentACKTotal

A total number of sent ACK packets. Applicable for data sender.

### pktRecvACKTotal

A total number of received ACK packets. Applicable for data receiver.

### pktSentNAKTotal

A total number of NAK (Not Acknowledged) packets sent. Essentially means LOSS reports. Applicable for data sender.

### pktRecvNAKTotal

A total number of NAK (Not Acknowledged) packets received. Essentially means LOSS reports. Applicable for data receiver.

### usSndDurationTotal

A total accumulated time in microseconds, when SRT has some data to transmit, including packets that were sent, but waiting for acknowledgement.
In other words, the a total accumulated duration in microseconds when there was something to deliver (non-empty senders' buffer).
Applicable for data sender.

### pktSndDropTotal

A number of packets dropped by sender due to too-late to send (refer to `TLPKTDROP`).

The total delay before `TLPKTDROP` is triggered consists of the `SRTO_PEERLATENCY`, plus `SRTO_SNDDROPDELAY`, plus 2 * the ACK interval (default ACK interval is 10ms).
And the delay used is the timespan between the very first packet in the sender's buffer, and the latest packet in the sender's buffer.


### pktRcvDropTotal

A number of too-late-to play missing packets. Receiver only.

TSBPD and TLPacket drop socket option should be enabled.

Receiver drops only those packets, that are missing by the time, when there is at least one packet ready to play.

Also includes packets failed to be decrypted (pktRcvUndecryptTotal). Those packets are present in the receiver's buffer, and not dropped directly. Potentially a bug,

### pktRcvUndecryptTotal

A number of undecrypted packets (packets that failed to be decrypted). Receiver side.

### byteSentTotal

Same as `pktSentTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Sender side.

### byteRecvTotal

Same as `pktRecvTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Receiver side.

### byteRcvLossTotal

Same as `pktRcvLossTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Receiver side.

### byteRetransTotal

Same as `pktRetransTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Sender side only.

### byteSndDropTotal

Same as `pktSndDropTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Sender side only.

### byteRcvDropTotal

Same as `pktRcvDropTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
Bytes of payload of dropped packets is estimated based on average packet size. \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Sender side only.

### byteRcvUndecryptTotal

Same as `pktRcvUndecryptTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Receiver side.

## Interval measurements

These values can be reset by calling `srt_bstats(..., int clear)` with `clear = 1`. \
This is helpful to get statistical measurements with a certain period, e.g. 1 second.

### pktSent

Same as `pktSentTotal`, but on the interval.

### pktRecv

Same as `pktRecvTotal`, but on the interval.

### pktSndLoss

Same as `pktSndLossTotal`, but on the interval.

### pktRcvLoss

Same as `pktRcvLossTotal`, but on the interval.

### pktRetrans

Same as `pktRetransTotal`, but on the interval.

### pktRcvRetrans

Same as `pktRcvRetransTotal`, but on the interval.

### pktSentACK

Same as `pktSentACKTotal`, but on the interval.

### pktRecvACK

Same as `pktRecvACKTotal`, but on the interval.

### pktSentNAK

Same as `pktSentNAKTotal`, but on the interval.

### pktRecvNAK

Same as `pktRecvNAKTotal`, but on the interval.

### mbpsSendRate

Same as `pktRecvNAKTotal`, but on the interval.

### mbpsRecvRate

### usSndDuration

### pktReorderDistance

`SRTO_LOSSMAXTTL` sets the maximum reorder tolerance value. And the internal algorithm checks the order of incoming packets and adjusts the tolerance based on the reorder distance, but not higher than the maximum value.
An example is the very first LOSS report in your capture. SRT starts from 0 tolerance. Once it receives the first reordered packet, it increases the tolerance to 1, meaning that 28 30 29 will not trigger a loss report, while 28 31 29 30 will trigger.


### pktRcvAvgBelatedTime

Accumulated difference between the time-to-play of the received belated packet and the current time.

### pktRcvBelated

A number of received AND IGNORED packets due to having come too late.

Makes sense only if TSBPD and TLPKTDROP are enabled.

An offset between sequence numbers of the newly arrived DATA packet and latest acknowledged DATA packet is calculated.
In case the offset is negative, the packet is considered belated, meaning that it was either already acknowledged or dropped by TSBPD as too late to be delivered.

Retransmitted packet can also be considered belated.

### pktSndDrop

Same as `pktSndDropTotal`, but on the interval.

### pktRcvDrop

Same as `pktRcvDropTotal`, but on the interval.

### pktRcvUndecrypt

Same as `pktRcvUndecryptTotal`, but on the interval.

### byteSent

Same as `byteSentTotal`, but on the interval.

### byteRecv

Same as `byteRecvTotal`, but on the interval.

### byteRcvLoss

Same as `byteRcvLossTotal`, but on the interval.

### byteRetrans

Same as `byteRetransTotal`, but on the interval.

### byteSndDrop

Same as `byteSndDropTotal`, but on the interval.

### byteRcvDrop

Same as `byteRcvDropTotal`, but on the interval.

### byteRcvUndecrypt

Same as `byteRcvUndecryptTotal`, but on the interval.

## Instant measurements

The measurements effective at the time retrieved.

### usPktSndPeriod

Current minimum time interval between consecutive packets are sent, in microseconds. Sender only.

**Note**. Except for probing packets.


### pktFlowWindow

The maximum number of packets that can be in flight. Sender only.

### pktCongestionWindow

### pktFlightSize

The number of packets in flight. Sender only.

`pktFlightSize <= pktFlowWindow`

### msRTT


### mbpsBandwidth
### byteAvailSndBuf
### byteAvailRcvBuf
### mbpsMaxBW
### byteMSS
### pktSndBuf
### byteSndBuf
### msSndBuf
### msSndTsbPdDelay
### pktRcvBuf
### byteRcvBuf
### msRcvBuf
### msRcvTsbPdDelay

## Srt 1.4

pktSndFilterExtraTotal
pktRcvFilterExtraTotal
pktRcvFilterSupplyTotal
pktRcvFilterLossTotal

local measurements
pktSndFilterExtra
pktRcvFilterExtra
pktRcvFilterSupply
pktRcvFilterLoss