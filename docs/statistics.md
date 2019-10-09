SRT provides a powerful set of statistical data on a socket. This data can be used to keep an eye on socket's health, and track some faulty situations as well.

Statistics are calculated independently on each side (receiver and sender), and are not exchanged between peers, unless explicitly stated.

## Total accumulated measurements

### msTimeStamp

Time elapsed since the SRT socket was started (after successful call to `srt_connect(...)` or `srt_bind(...)`), in milliseconds.

### pktSentTotal

The total number of sent data packets, including retransmitted packets. Applicable for data sender.

### pktRecvTotal

The total number of received packets, including received retransmitted packets. Applicable for data receiver.

### pktSndLossTotal

The total number of data packets considered or reported lost (sender side). Does not correspond to packets detected as lost at receiving side. Applicable for data sender.

A packet is considered lost in two cases: 
1. Sender receives a loss report from receiver.
2. Sender initiates retransmission after not receiving ACK for a certain timeout. Refer to `FASTREXMIT` and `LATEREXMIT` algorithms.

### pktRcvLossTotal

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

### pktRetransTotal

The total number of retransmitted packets. Calculated on the sender's side only. 
Not exchanged with the receiver.

### pktSentACKTotal

The total number of sent ACK packets. Applicable for data sender.

### pktRecvACKTotal

The total number of received ACK packets. Applicable for data receiver.

### pktSentNAKTotal

The total number of NAK (Not Acknowledged) packets sent. Essentially means LOSS 
reports. Applicable for data sender.

### pktRecvNAKTotal

The total number of NAK (Not Acknowledged) packets received. Essentially means 
LOSS reports. Applicable for data receiver.

### usSndDurationTotal

The total accumulated time in microseconds, during which the SRT sender has some 
data to transmit, including packets that were sent, but is waiting for acknowledgement.
In other words, the total accumulated duration in microseconds when there was something 
to deliver (non-empty senders' buffer).
Applicable for data sender.

### pktSndDropTotal

The number of "too late to send" packets dropped by sender (refer to `TLPKTDROP`).

The total delay before `TLPKTDROP` is triggered consists of the `SRTO_PEERLATENCY`, 
plus `SRTO_SNDDROPDELAY`, plus 2 * the ACK interval (default ACK interval is 10 ms).
The delay used is the timespan between the very first packet and the latest packet 
in the sender's buffer.


### pktRcvDropTotal

The number of "too late to play" missing packets. Receiver only.

TSBPD and TLPacket drop socket option should be enabled.

The receiver drops only those packets that are missing by the time there is at 
least one packet ready to play.

Also includes packets that failed to be decrypted (pktRcvUndecryptTotal). These 
packets are present in the receiver's buffer, and not dropped directly. 
Potentially a bug.

### pktRcvUndecryptTotal

The number of packets that failed to be decrypted. Receiver side.

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
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Receiver side only. 

### byteRcvUndecryptTotal

Same as `pktRcvUndecryptTotal`, but expressed in bytes, including payload and all headers (SRT+UDP+IP). \
20 bytes IPv4 + 8 bytes of UDP + 16 bytes SRT header. Receiver side.

## Interval measurements

These values can be reset by calling `srt_bstats(..., int clear)` with `clear = 1`. \
This is helpful to get statistical measurements within a certain period, e.g. 1 second.

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

`SRTO_LOSSMAXTTL` sets the maximum reorder tolerance value. The internal algorithm 
checks the order of incoming packets and adjusts the tolerance based on the reorder 
distance, but not to a value higher than the maximum. An example is the very first 
LOSS report in a capture. SRT starts from 0 tolerance. Once it receives the first 
reordered packet, it increases the tolerance to 1, meaning that 28 30 29 will not 
trigger a loss report, while 28 31 29 30 will trigger one.


### pktRcvAvgBelatedTime

Accumulated difference between the current time and the time-to-play of a packet 
that is received late.

### pktRcvBelated

The number of packets received but IGNORED due to having arrived too late.

Makes sense only if TSBPD and TLPKTDROP are enabled.

An offset between sequence numbers of the newly arrived DATA packet and latest 
acknowledged DATA packet is calculated.
If the offset is negative, the packet is considered late, meaning that it was 
either already acknowledged or dropped by TSBPD as too late to be delivered.

Retransmitted packets can also be considered late.

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

Current minimum time interval between which consecutive packets are sent, in 
microseconds. Sender only.

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
