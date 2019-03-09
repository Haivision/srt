Introduction
============

The Packet Filtering mechanism in SRT is defined as general packet filtering
with no strictly defined purpose, but the main reason for having it is to
implement the FEC mechanism. Simultaneously there's provided a builtin packet
filter for optional use, identified as "fec". Users may provide and use extra
packet filters using the packet filter framework.

Packet Filter Framework
=======================

The sending and receiving part of packet filtering is to be defined in one
class. The possibility of providing only one direction is not predicted, as
SRT is generally bidirectional, so both directions must be covered on a single
party.

The user defined packet filter should be a class derived from
`SrtPacketFilterBase` class and it should override several virtual methods.

Basic types
-----------

Before the description, let's describe first the packet structures:

 - `CPacket` is an internal SRT class, that is, it gives you access to the
exact packet to be used in the operation.
 - `SrtPacket` is a special intermediate class, which has more static
definition and is used as an intermetdiate space to be copied to the `CPacket`
structure when needed, automatically.

Both classes - beside completely independent contents and methods - implement
a common protocol consisting of the following methods:

`char* data();`

Returns the buffer in the packet.

`size_t size();`

Returns the size of the contents in the buffer. Note that this is not the size
of the buffer itself.

`uint32_t header(SrtPktHeaderFields field);`

This accesses the header. The `field` type specifies the field index in the
header.

Note that if a function gives you a writable access to `CPacket`, you can
modify the payload, but you can't modify the header. With `SrtPacket` you
are free to modify any contents, but to access the header you should use
the `SrtPacket::hdr` field, which is the array to be indexed by values of
`SrtPktHeaderFields` type.

Construction
------------

The constructor of the packet filter class should have the following
signature:


`MyFilter(const SrtFilterInitializer &init,
     std::vector<SrtPacket>& provided,
     const string &confstr);`

The following parameters are required:

* `init` - should be passed to the constructor of `SrtPacketFilterBase`, this
will provide you with the basic data you will need when creating a packet at
the receiver
* `provided` - this is where you have to store packets that your filter will
generate as extra packets (in case of FEC, this is where the rebuilt packets
will be supplied)
* `confstr` - configuration string. You should parse it using
`ParseFilterConfig` so that you can use it for your purpose (note that this
configuration string is still parsed by this function internally in SRT, so
the official syntax must be preserved).

The base class will provide you some important data from the socket through
the following methods:

`socketID()` - the socket ID that you should write into ID header field on
the receiver
`sndISN()` and `rcvISN()` - return the very first sequence number in
particular direction (FEC uses it to initialize the base sequence numbers
for FEC groups)
`payloadSize()` - the maximum size of the single packet (FEC uses it to
know how big the contents of the payload it should use for the FEC control
packet)


Sending
-------

Note that sending in SRT is driven by the congestion control mechanism
currently used and it decides about the socket that is ready to send the
data with the currently defined speed. It means that there's a function called
at the right moment when a socket should provide a packet to be sent over the
UDP link. These functions are used for the filter:

`void feedSource(CPacket& pkt);`

This function is called at the moment when a packet is picked up from the
sender buffer (packets already submitted by `srt_sendmsg` call) and it's going
to be sent. Note that this packet is bound to a buffer representing the input
and its size is set to the maximum possible (one trick here will be covered
below). Normally you want to read the contents and do something with this, but
the packet can be also altered, should you need to, for example, add an extra
header. Note that if you use encryption, the contents of the packet are
already encrypted. Note also that if you alter the packet, you will alter it
in the current sender buffer, so in case when this packet was requested to be
retransmitted, it will be retransmitted in this very form that you altered it
here.

The current FEC filter implementation uses this function only to collect the
contents of the packet to "clip" it into the clip buffer (apply the bit XOR
function on the contents).

`bool packControlPacket(SrtPacket& packet, int32_t seq);`

Before I describe this function, it's important to know the priority order in
the function responsible to pack the packet to be sent as requested:

1. Retransmitted packet. This is so always when there's some loss recorded.
2. packControlPacket on the filter, if filter installed
3. New packets scheduled, waiting in the sender buffer

This function is then called when it was turned out that there are no packets
ready for retransmission, but maybe there are any special control packets
supplied by the filter and ready for extraction. This function should return
false in case when there is no such packet ready, in which case the function
will then try to pick up the next waiting packet from the sender buffer. If
it returns true, it means that it has supplied the packet.

Note that for that case the contents of the packet are to be written in a
special packet buffer, from which the contents will be copied to the target
packet. This buffer is already of maximum possible size of a single packet in
SRT.

The way how packets of special control purpose are distinguished from regular
data packets is by the message number field. Normally the lowest value is 1,
it increases with every next packet (in live mode a message can be only one
packet long) up to the value where it rolls over back to 1. Filter control
packets have message number equal to 0. This is done automatically for packets
that are filter control packets. For that reason, you are free to store
special value in TIMESTAMP and SEQNO fields, but the MSGNO and ID fields shall
be left alone (they will be overwritten anyway).

Receiving
---------

For receiving, there's just one function:

`bool receive(const CPacket& pkt, loss_seqs_t& loss_seqs);`

This function is called for any data packet received for that socket. Things
done already is to state that this is a data packet, the rest is up to this
function. The packet received here is not to be altered.

However, you can decide as to whether the given packet should be passed
through, or dismissed, by the return value. Returning true means that the
received packet should be passed further to the receiver buffer, otherwise it
is discarded. In case when you have some special contents in a packet that you
want removed, simply recreate the packet, and this one declare to be
discarded.

In case when you want to supply a packet from the filter (in case of FEC it
means to supply an FEC-rebuilt packet), you store them in the `provided`
array, to which the reference you received in the constructor. This is also to
be used in case when you want to replace a packet. The order in which they get
stored in this array doesn't matter because the packets before returning them
to SRT will be sorted in the sequence number order.

Things that the user has to worry about inside this function are:

1. It's up to you to distinguish regular data packets and filter control
packets by checking the message number. Important to use `CPacket::getMsgSeq`
function for that purpose because this extracts the right part of the MSGNO
field from the header (`pkt.header(SRT_PH_MSGNO)` will return just the field,
which contains also extra flags).

2. If you use an extra header for every packet, be sure that you can recognize
it correctly as you have created it.

3. When rebuilding the packet, you have to set correctly the sequence number,
the ID field (get the value from `socketID()`), timestamp, and the encryption
flags (other flags are not important, they are set as default).


The builtin FEC filter
======================

The builtin FEC filter implements the usual XOR-based FEC mechanism, which
qualifies packets into groups, and every group gets generated an FEC control
packet, which can be used to rebuild a packet that was lost. It uses both row
groups and column groups, with a special mode of having rows only.

A row is a series of consecutive packets from the base sequence up to the N
next packets with N being a size of the row.

A column is a series of packets that start at the base and the next packet is
shifted towards the previous in the group by N, which is the size of the row.

Important thing to note is that the size of the row is simultaneously the
number of colums, and vice versa - the size of the column is the number of
rows.

Columns in this implementation are not even (that is, the base sequence of the
next column group in order is not the next sequence). The columns are arranged
in a "staircase", so the base sequence for the next column group is shifted
towards the previous column's base by N+1, where N is the number of columns
(and size of the row). The reason for that is that the FEC control packets, if
columns are arranged equally in a row, would be supplied exactly one after
another, which would cause temporary spike in the network usage, maybe still
under a bandwith control (if set), but then it would cause extra sending delay
on data packets already scheduled. All these things may potentially cause
extra congestion and more probable packet loss. In case of the staircase
arrangement, the worst congestion of FEC control packets would be only at the
column group in the last column, which will be then, in order:

 - last data packet in the column
 - FEC control packet for the column
 - FEC control packet for the row
 - data packet from the first column
 - data packet from the second column being last in the column group
 - so followed again by FEC control packet for the group

And then a longer series of data packets.

Sending
-------

In the sending part, `feedSource` gets the packets so that its contents can be
clipped into the group's clip buffer. Data being clipped are:

 - timestamp
 - encryption flags
 - content length
 - contents (virtually extended to the size of `payloadSize()`)

For timestamp clip, the header field TIMESTAMP is reused. For all others there
are extra fields in the payload space of the SRT packet:

 - 8 bits: group index (for rows it's -1)
 - 8 bits: flag clip
 - 16 bits: length clip

When the number of clipped packets reaches the predicted size of the group,
the FEC control packet is considered ready for extraction, and the current
clip state after applying all packets from the group onto it becomes the
required contents of the FEC control packet. This will be then returned at the
next call to `packControlPacket`. First the row FEC control is checked, then
the column.


Receiving
---------

The receiver must first recognize as to whether the packet is a regular data
packet or a FEC control packet, by having `getMsgSeq()` function return 0.
If this was a control packet, then the first byte from the "FEC header" in
that packet is extracted to recognize if this is a column FEC (contains the
column number) or row FEC (contains -1).

The regular data packet is then inserted into the FEC group to which it
belongs, both horizontal and vertical, by clipping the contents and increasing
its count. The FEC control packet is inserted into the group to which it is
destined (its sequence number is the last sequence in the group). When the
rebuild-ready condition is met - the number of clipped packets is N-1, where N
is the size of the group, and the FEC control packet was also clipped, then
the packet is being rebuilt - that is, its contents are restored basing on the
contents of the clipped data.

The rebuilding happens recursively, that is, after the packet is rebuilt, its
contents are also clipped to the cross-group to which it simultaneously
belongs (if rebuilding was in a row group, it clips it into the column group
and vice versa), and the same condition on this group is checked again. And
alike, if the condition of rebuild-ready is met, the packet is being rebuilt,
and so on.

The rebuilt packets are stored in the `provided` array so that they can be
picked up by SRT to insert them all into the receiver buffer.

Cooperation with retransmission
-------------------------------

The ARQ level is a value that decides how the filtering should cooperate with
retransmission. Possible values are:

* NEVER: Do not do retransmission at all. This means that packets are ACK-ed
always up to the sequence that is lately received and all losses are ignored.
* ALWAYS: Do retransmission like before, that is, the retransmission request
is sent immediately upon loss detection, parallelly with possible FEC
rebuilding. It makes little sense, but it might be useful in case of networks
that maintain high bandwidth, but happens to be very unstable; working both
FEC and ARQ on the same edge at least increases the probability that whatever
can't be restored by one system, will be restored by the other as fast as
possible. However, both ARQ and FEC overheads will apply.
* ONREQ: The lost packets are recorded by SRT, but the loss report is not
being sent, unless this sequence is reported in the `loss_seqs`. Note that in
this case the lost packets will be reported with a delay. With very low
latency it's very little time to recover by ARQ in general, and with this
delay it's even less, or may be even always too late to retransmit. 

Note that in ALWAYS and NEVER mode the filter should not return anything in
`loss_seqs`.

It's very important that the latency is properly set. The FEC mechanism may
rebuild a packet, but it will provide it with some delay, at the moment when
the group gets stuffed with all required data so that the rebuilding can
trigger. The minimum value that grants it should be based on the number of
packets described as `N = (R * (C-1)) + 2` (with R=row size, C=column size).
The latency should be such that with the current bitrate the N number of
packets should take a time to send that still fits in the latency, and this
minimum should be even increased by some extra safety margin. If this
condition isn't met, the TLPKTDROP mechanism may drop packets anyway even if
they can be rebuilt.

When you cooperate with ARQ, note that the latency penalty for that case is
estimated now as `4 * RTT`. If you use ALWAYS level, your latency penalty
should be the maximum of these two. If you use ONREQ level, your latency
penalty should be the sum of these two.

THe builtin FEC filter in case of ONREQ setting is collecting the sequence
numbers of all packets that are lost and no longer recoverable. This is
recognized by the fact that a packet has come with a sequence number that is
past the last sequence of particular group. All groups for which this packet
is the future are dismissed using a special recognition mechanism.

First, at the reception side there are columns (with indexes from 0 up to the
number of columns, or row size as well) and column series. From the sequence
number of the packet there's calculated it offset towards the first still
existing row base sequence, and this distance's remainder against the row size
is the column index. Then, from this column there's taken the base sequence
number (sequence of the first packet in the group) and confronted with this
packet's sequence number to recognize the group series for the group to which
this packet belongs. When it turns out that this group series is later than 0,
then all groups from the group in the same column and series 0 backwards are
tested for dismissal, that is, every group is dismissed up to the first group
that was already dismissed or up to the very first group.

Let's take an example:

1. We have column size 8 and row size 10
2. We get the absolute column base B0 and every column has its own base,
appropriately shifted

The distance between the packet's sequence number and the base counts for 15.
Column size is 8, so 15 % 8 = 7, so the column index is 7, and 15 / 8 = 1,
so the column series is 1, which is greater than 0. The index of the group in
the same column of series 0 is then 7. So we take group 7 and will continue
down to 0, stopping at the first group that is already dismissed, and until
this condition is met, every group is dismissed.

Also in this mode when it turns out that the dismissed group has also removed
the last standing row group that crosses this column group, the rows are
dismissed as well. All lost packets are collected in this procedure and then
finally reported in `loss_seqs`.

As the column groups are arranged in "staircase", the penalty for loss
detection (regarded with ONREQ level) shall be always counted as "size of the
matrix" (a product of both group sizes). So many packets must be received
since the packet that caused a loss detection so that the FEC facility reports
it as lost and not recoverable. This is averge the same value that should be
the FEC latency penalty.

