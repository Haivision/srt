Introduction
============

The Packet Filtering mechanism in SRT is a general-purpose mechanism to inject
extra processing around the network transmission. The direct reason for
introducing this facility was to implement the FEC mechanism in SRT, although
the Packet Filtering need not be limited to this exactly. There is then one
builtin filter installed, named "fec", but more filters can be added by user.


Configuration
=============

General syntax
--------------

The packet filtering can be configured by a socket option, which gets the
configuration contents passed as a string. How this string is interpreted,
depends on filter itself, however there's one general syntax obligatory:

`<filter-type>,<key>:<value>[,...]`

The parts of this configuration are separated by comma. The first part is the
name of the filter. Other parts are `key:value` pairs, the interpretation of
which is filter type dependent.

You can try this out using `SRTO_FILTER` option or `filter` parameter in SRT
URI in the appliations.


Configuring the FEC filter
--------------------------

To use the FEC type filter, your first part in the configuration,
filter-type, is simply `fec`. Then the following parameters are regarded:

* `cols`: The **number if columns** (and simultaneously **size of the row**).
This parameter is obligatory and must be a positive number of at least 2.
* `rows`: The **number of rows** (and simultaneously **size of the column**).
This parameter is optional and defaults to 1. Usually, if >=2, means exactly
the number of rows, but two other special cases are allowed:
   * `1`: in this case you have a row-only configuration (no columns)
   * `-N`, where N is >=2: Same as `N`, but with column-only configuration
* `layout`: Possible values: `even` (default) and `staircase`:
   * `even`: columns are arranged in a solid matrix (first sequences in the R
first column groups, where R is row size, are contained in one row)
   * `staircase`: first sequences of the first R groups have distance to one
another not by 1, but by R+1, when reached the bottom of the first matrix,
continue since top.
* `arq`: Possible values: `always`, `onreq` (default) and `never`.
   * `always`: ARQ is done parallelly with FEC, that is, loss is always
reported immediately once detected in SRT.
   * `onreq`: ARQ is allowed, but only those losses are reported that FEC
failed to rebuild, at the moment when the incoming packet had a sequence number
that exceeds the last in one of the column groups; such a packet if still
lacking at this moment is considered no longer recoverable by FEC.
   * `never`: ARQ is not done at all. Packets not recovered by FEC undergo
TLPKTDROP, just like those that failed ARQ recovery in a usual mode.


The motivation for staircase arrangement
----------------------------------------

Normally the FEC could be done in a solid matrix, that is, packet sequences are
arranged in a two-dimensional array with sizes R x C, with R rows and C
columns. The problem is that at the moment when the last row is retransmitted,
the FEC control packets are transmitted as well.

Let's imagine 10 columns and 5 rows starting from sequence 500. Rows then begin
with sequences: 500, 510, 520, 530 and 540 is the last one. This is what
happens when transmitting since the half of the butlast row up to the first row
in the next series:

```
<537>
<538>
<539>
<FEC/H:539>
<540>
<FEC/V:540>
<541>
<FEC/V:541>
<542>
<FEC/V:542>
<543>
<FEC/V:543>
<544>
<FEC/V:544>
<545>
<FEC/V:545>
<546>
<FEC/V:546>
<547>
<FEC/V:547>
<548>
<FEC/V:548>
<549>
<FEC/H:549>
<FEC/V:549>
<550>
<551>
...
```

Stating a constant bitrate at the input, there's a certain bandwidth normally
used by the regularly transmitted data packets. For this one moment when the
last row from a series is transmitted, the transmission will use **twice** the
bandwidth towards the bandwidth normally used.

There are several methods how to mitigate this:

1. Limit the bandwidth in SRT (`SRTO_MAXBW`). This means that the packets will
not be transmitted as fast as the FEC requires, but some delay will apply.
However this will put extra delay this way on regular packets. Normally it
shouldn't disturb much, as there's already a very high minimum latency you must
configure with FEC, which counts the size of the matrix (50 in the above
example) multiplied by the bitrate divided by bytes-per-packet factor, but if
you decided to use FEC and ARQ cooperation, delaying a packet at sender side
may challenge the response time for retransmission.

2. NOT IMPLEMENTED: delay sending the FEC packet itself. The problem is that
this would make the time distance between the first packet in a group be
extended to a factor based on twice the matrix size (100 in the above example)
and so should it increase the latency.

3. Use the staircase arrangement.

The staircase arrangement causes that the next column groups starts not at the
distance of 1 towards the previous one, but R+1, where R is the row size. For
10x5 sizes it would look like this:

```
0 1 2 3 4 5 6 7 8 9
A - - - - A - - - -
# A - - - # A - - -
# # A - - # # A - -
# # # A - # # # A -
V # # # A V # # # A
B V # # # B V # # #
# B V # # # B V # #
# # B V # # # B V #
# # # B V # # # B V
V # # # B V # # # B
```

Here in this picture you have the letter A or B at the beginning of the group,
with A for the first series and B for the second series. The V letter shows the
last packet in the column group and simultaneously the packet that will be
followed by an FEC control packet. So, in this arrangement, as a worst possible
scenario, you'll have, starting from 537 packet:


```
<537>
<538>
<539>
<FEC/H:539>
<540>
<FEC/V:540>
<541>
<542>
<543>
<544>
<545>
<FEC/V:545>
<546>
<547>
<548>
<549>
<FEC/H:549>
<550>
<551>
<FEC/V:551>
<552>
...

```
The dashes in this picture mean the packets that are not covered; actually the
current implementation predicts that for the initial packets sent since the
connection is established, these will not be covered by any groups. It's
generally not a problem when some initial packets are lost in the first 2
seconds of a transmission, as long as it's clean later, although if needed this
can be also mitigated by adding a series of unused groups "one matrix before
the transmission starts", if needed.

An extra advantage of the staircase arrangement is also that this may increase
probability of rebuilding packets that happen to span for longer holes (like
subsequent 5 packets). This gives more chance to happen that these packets
qualified to the same column index in two consecutive rows will be actually in
different column groups.


The builtin FEC filter
======================

The builtin FEC filter implements the usual XOR-based FEC mechanism, which
qualifies packets into groups, and every group gets generated an FEC control
packet, which can be used to rebuild a packet that was lost. It allows for the
following possible configurations:

* row only: row group is a range of consecutive packets, no columns used
* column only: usual configuration, just the row FEC packet is never sent
* columns and rows

A row is a series of consecutive packets from the base sequence up to the N
next packets with N being a size of the row.

A column is a series of packets that start at the base and the next packet is
shifted towards the previous in the group by N, which is the size of the row.

Important thing to note is that the size of the row is simultaneously the
number of colums, and vice versa - the size of the column is the number of
rows.


Sending
-------

In the sending part, `feedSource` gets the packets so that its contents can be
clipped into the group's clip buffer. Data being clipped are:

 - timestamp
 - encryption flags
 - content length
 - contents (the size padded with zeros up to `payloadSize()`)

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

FEC control packet is distinct to a regular data packet by having the message
number equal to 0. This number isn't normally used in SRT - it starts from 1
and when reaching the maximum it rolls back to 1.


Receiving
---------

The receiver must first recognize as to whether the packet is a regular data
packet or a FEC control packet, by having `getMsgSeq()` function return 0.
If this was a control packet, then the first byte from the "FEC header" in
that packet is extracted to recognize if this is a column FEC (contains the
column number) or row FEC (contains -1).

The regular data packet is then inserted into the FEC group to which it
belongs, both horizontal and vertical (as per configuration), by clipping the
contents and increasing its count. The FEC control packet is inserted into the
group to which it is destined (its sequence number is the last sequence in the
group). When the rebuild-ready condition is met - the number of clipped packets
is N-1, where N is the size of the group, and the FEC control packet was also
clipped, then the packet is being rebuilt - that is, its contents are restored
basing on the contents of the clipped data.

If both columns and rows are used, the rebuilding happens recursively, that is,
after the packet is rebuilt, its contents are also clipped to the cross-group
to which it simultaneously belongs (if rebuilding was in a row group, it clips
it into the column group and vice versa), and the same condition on this group
is checked again. And alike, if the condition of rebuild-ready is met, the
packet is being rebuilt, and potentially can lead to checking the cross-group
again.

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
should be the maximum of these two (which usually means that only the FEC
latency penalty should apply). If you use ONREQ level, your latency penalty
should be the sum of these two.

The builtin FEC filter in case of ONREQ setting is collecting the sequence
numbers of all packets that are lost and no longer recoverable. This is
recognized by the fact that a packet has come with a sequence number that is
past the last sequence of particular group. All groups for which this packet
is the future are dismissed, that is, the irrecoverable packets are reported
at this call and the `dismissed` flag is set so that it's not reported again.
Note that this group isn't physically deleted at this time.

Unused groups are deleted when no longer needed. In case of even arrangement,
the whole series 0 matrix is deleted at the moment when a first packet came in
from the series 1 matrix. In case of a staircase arrangement, the packet must
jump over a size of two matrices to trigger deletion of the matrix series 0.

As the column groups are arranged in "staircase", the penalty for loss
detection (regarded with ONREQ level) shall be always counted as "size of the
matrix" (a product of both group sizes). So many packets must be received
since the packet that caused a loss detection so that the FEC facility reports
it as lost and not recoverable. This is averge the same value that should be
the FEC latency penalty.



Packet Filter Framework
=======================

The builtin FEC facility is connected with SRT through a mechanism called
Packet Filter. This mechanism relies on the following checkpoints which allow
for Packet Filter injection:

* sending part
   1. a filter is first asked if it is ready to deliver a control packet; if
so it is expected to deliver it and this packet is sent instead of a data
packet waiting in the sender buffer
   2. when the new packet picked up from the sender buffer is about to be sent
over the network, it's first passed to the filter
* receiving part: Every packet received from the network is passed to a filter,
which can:
   * pass it through (report that the packet as original should be reported)
   * provide also extra packets

Builtin FEC doesn't use all abilities of Packet Filter, in particular:

* At the sending part, the packet may be altered. FEC only reads the data.
* Receiver can do no passthrouh and instead process the packet and ship all
results into the provision buffer. FEC does passthrough of the regular data
packets, just eats up FEC control packets and provides rebuilt packets.

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
structure when needed, automatically. This is required for some cases when
SRT is doing some unusual memory management things, so the `CPacket` structure
cannot be used.

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
UDP link.

There are currently three packet providers checked in specified order. When
particular one provides a packet, the provision is done and the others may only
have a chance for provision only next time. The providers are checked in this
order:

1. A packet required for retransmission. This is per information recorded in
the sender loss list, updated from received `UMSG_LOSSREPORT` mssages.
2. If Packet Filter installed, the filter's control packet, if one is ready.
3. Next waiting data packet.

When #3 is currently providing something, this function is called on the
filter:

`void feedSource(CPacket& pkt);`

This function is called at the moment when a packet is picked up from the
sender buffer (packets already submitted by `srt_sendmsg` call) and it's going
to be sent. Note that this packet is bound to a buffer representing the input
and its size is set to the maximum possible (one trick here will be covered
below). As this packet is also allowed to be altered, data can be also extended
up to this size (builtin FEC doesn't use it). Note that in case when it's
altered, it will stay in this form in the sender buffer and in this form
retransmitted, if needed.

The current FEC filter implementation uses this function only to collect the
contents of the packet to "clip" it into the clip buffer (apply the bit XOR
function on the contents).

The case #2 of the providers, a possible control packet, is done by this
function:

`bool packControlPacket(SrtPacket& packet, int32_t seq);`

This function should return false in case when there is no such packet ready,
in which case the provider #3 (new data packet) will be tried. If it returns
true, it means that it has supplied the packet.

Note that for that case the contents of the packet are to be written in a
special packet buffer, from which the contents will be copied to the target
packet. This buffer is already of maximum possible size of a single packet in
SRT.

The way how packets of special control purpose are distinguished from regular
data packets is by the message number field. For these packets the message
number is always 0, while regular data packets have message numbers since 1 up
to the maximum, after which they roll back to 1.

For that reason, you are free to store special value in TIMESTAMP and SEQNO
fields, but the MSGNO and ID fields shall be left alone (they will be
overwritten anyway).

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

3. When creating (reconstructing) the packet, you have to set correctly the
sequence number, the ID field (get the value from `socketID()`), timestamp, and
the encryption flags (other flags are not important, they are set as default).


