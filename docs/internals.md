# Introduction

** This document is a *DRAFT* version**

This document aims to describe several things about SRT library internals
that should answer various questions about how this library is related to
the system resources. It's not a comprehensive description for developers
that might want to expand or improve the SRT library, rather for those who
want to imagine how it works and how an application can make best use of
this library.


# Introduction about UDP

UDP is known as a connectionless non-reliable protocol. Actually it does
provide one level of reliability - the contents of the packet received
really are the exact contents of that packet when it was sent (there's
a checksum used to ensure correct contents). The packet itself, however,
might simply get lost during transmission.

This protocol is connectionless, which means that the only data category
that is sent over this protocol are user data. However every packet has
the information that some documents mention mistakenly as "UDP connection":
the IP address and port. This is specified as both source and destination.
The IP addresses are specified in the IP header, and the ports in UDP
header. The UDP header actually contains the following 16-bit data:

* Source port
* Destination port
* Checksum
* Payload size

This way there's something that can be called "UDP link", which is a pair
of two "application addreses" (IP address and port), Source and Destination. If
we define them as A:P and B:Q respectively, then this UDP link is defined by
this pair. If a packet is sent over this channel, then it's either A node
sending to B or vice versa; in this first case then the UDP packet has A:P as
source and B:Q as destination, and when the B machine sends the packet, then
B:Q is source and A:P is destination. This kind of UDP link is also recognized
in the firewall settings. For example the NAT rule can be defined for the side
of only one node and define for example that A:P shall be translated into X:Z.
This rule, however, will not only translate source A:P to X:Z for the packet
sent to B, but also when the B node sends the packet back, this time addressed
to X:Z destination, it will be also translated back to A:P. The NAT rule simply
applies to the whole link (or channel, if you prefer) and both directions.

Important to understand from the SRT perspective is that this UDP link is being
used instrumentally in order to implement the connection in the SRT logics.
These logics are not bound to the logics of UDP protocol.


# SRT Protocol basics


## 1. SRT Protocol on top of UDP

The SRT Protocol is using UDP protocol to implement its transport things,
so what is seen as user data from UDP perspective, from SRT perspective it
might be data with some control information, or simply a control information
only.

Every UDP packet that is sent over the link used by the SRT connection
is using additionally the SRT header, which consists of 4 32-bit fields:

1. SEQNO/COMMAND ID
2. MSGNO+FLAGS/EXTRA DATA
3. TIMESTAMP
4. TARGET SOCKET ID

The most significant bit in the first field defines the basic type of
the packet: control (if set) or data (if clear). Rest of the bits
are then command identifier or sequence number respectively.

The second field contains various specific flags and message number (used to
detect message boundaries if spans for multiple packets, otherwise unused). For
control packets the use of this field is command specific.

Rest of the fields are rather logically stable, except that the timestamp may
be used for various purposes (regular data packets use it for calculating the
play time in live mode, otherwise the use is packet type defined). The Socket
ID field may have a special value 0, which is used only in handshake command
packets to mark the connection pending state (that is, the target socket id is
only about to be learned).

Command packets usually contain the list of 32-bit integer numbers with
command-specific purpose, although there happen to be special cases with simply
binary data content.


## 2. Communication control and UDP link

There are two basic states of the connection in which particular
rules for communnication control apply:

1. Connection pending state
2. Connected state

The control communication during the connection pending state is
handshake-only. That is, the only type of packet that is allowed to be
exchanged in this state is a command packet with command id `UMSG_HANDSHAKE`.
All other kinds of packets are usually rejected ("usually" because there may
potentially happen a kind of "confusion state", when one side considers itself
connected, but the other does not and is still in the connection pending). The
handshake exchange should carry out appropriate information between two sides
so that they can finally turn into a connected state or reject the connection
(in specific cases).

The "confusion state" is a state that may temporarily happen at the caller
side. The caller-listener style of connection relies on that the caller first
sends the handshake request and gets a response from the listener. It may
happen that the final response from the listener gets lost, as it may normally
happen for UDP packets. In this case the listener already turns into a
connected state, however the caller is for the moment unaware of that, and
still thinks of itself as pending for connection. In this case the caller may
store the incoming packets other than handshake for the purpose of later use,
but it can't do antyhing with them, until it's connected. The caller then must
repeat stubbornly the handshake requests until it finally receives the response
from the listener, and only then does it turn into a connected state.

The handshake data among others contain also the handshake type value. In case
of caller-listener the handshake is 4-way: first, there is an exchange of a
handshake type `URQ_INDUCTION`, then the second handshake phase uses
`URQ_CONCLUSION`. The connection is considered established on particular party
upon reception of the `URQ_CONCLUSION` message.

When the state turns into connected, further handshake packets shall be
normally ignored (except the fact that the listener should send a handshake
response every time it receives the handshake request from the caller). This is
not exactly the case for rendezvous connection. The Rendezvous connection
requires 3-way handshake: first `URQ_WAVEAHAND` shall be sent by the party that
hasn't been contacted yet by the peer, `URQ_CONCLUSION` by the party that was
contacted, and then in order to "confirm reception of conclusion and connection
established state" there may be sent `URQ_AGREEMENT`. This last message is
necessary for a case when a party is already in connected state, but it
receives still the `URQ_CONCLUSION` messages, which might mean that the peer
probably didn't comprehend that the agent is already in the connected state, so
this message makes the peer aware of that.

During the connected state, the usual packet types used over the UDP link are:

* Regular data packets to carry out the data
* `UMSG_KEEPALIVE` packets when no data are sent at the moment
* `UMSG_ACK` sent back by the receiver
* `UMSG_ACKACK` response for `UMSG_ACK` from the sender
* `UMSG_LOSSREPORT` to report the lost packets
* `UMSG_SHUTDOWN` to inform the peer that the connection is broken

Let's focus on the usual data flow and let's state for a moment that we have a
unidirectional transmission only, so one side is defined as sender and the
other as receiver.

The sender sends the data packets, with increasing sequence number. After the
ACK period, the receiver sends the `UMSG_ACK` message in order to inform the
sender about the last sequence number of a packet it has received. This marks
the point in the sender buffer up to which sequence number packets can be
already dismissed from the buffer. Those that are not to be dismissed, must be
kept in the sender buffer for the purpose of prospective retransmission.

After the `UMSG_ACK` packet was received, the sender sends immediately the
`UMSG_ACKACK` message. This is used mainly for the purpose of various
measurements. `UMSG_ACK` message reports many interesting data - such as
average reception speed, bandwidth usage, and the RTT measured as a distance
between sending `UMSG_ACK` and receiving `UMSG_ACKACK`. Note that this is
measured differently than on TCP and therefore it's sometimes referred to as
"reverse RTT".

In case when during reception the sequence number of the incoming packet is not
contiguous (it's not the sequence number of the previous packet increased by 1
- mind, of course, that sequence numbers are circular numbers, that is, there's
a value of maximum sequence number which after increasing by 1 becomes 0), this
situation is considered a loss, and in this case the `UMSG_LOSSREPORT` is sent
back to the sender, containing the currently perceived loss range (the range
enclosing the sequence next to previously received and the previous towards the
currently received packet). Note that here, in normal situation, there aren't
reported any lost packets that were detected in some previous reception, just
not yet recovered - this thing still happens, but at a different time. This is
during a periodic check of current state used in some modes (live mode
currently only) and when the recovery didn't happen in some predicted time, the
loss report is sent again, this time containing information of all packets
considered lost so far.

However, if a packet has come with a sequence number that is older than the
last sequence received, it's considered coming out of order. Packets may come
then out of order by two reasons:
* Normal UDP packet reordering (rarely happens, but possible)
* Recovered packets

To distinguish them, the R flag is used (msgno field with flags in the
data-type packet), which is set when the packet was retransmitted. This is to
determine if the UDP packet reordering is happening, where the retranmitted
packet is not considered out of order. This is used for reordering detection
and prevent excessive retransmissions for falsely detected loss. It is
controlled by `SRTO_LOSSMAXTTL` option and by default no such detection is
made.

Anyway, the packet coming out of order usually is a retransmitted packet, which
seals up the loss hole in the receiver buffer. It allows the acknowledged
packet range to be extended and both returned to the application and
acknowledged by the message. Note that this process happens differently in live
and file mode:

* In Live mode, the packets are signed off to the application
only when the play time comes (as determined from their timestamp, measured RTT
and the configured latency). Until then the packets stay in the buffer. If the
time has come to play a packet that follows any number of lost packets, this is
considered a "too late packet" and they are dropped under this condition
(TLPKTDROP, "too late packet drop"). This last option can be turned off, but
it's not recommended.

* In File mode, the packets are signed off to the application
when they are confirmed as contiguous. If there are losses, the packets must be
recovered by retransmission and until then nothing is signed off to the
application, just like it's not acknowledged to the sender (they both are
actually done together in one procedure).


## 3. The internal use of UDP socket

SRT, beside the data assigned per socket, uses also many various global data.
They contain, beside having organized also all data used by the application, also
the shared data. One of the most important part of it is "multiplexer".

A "multiplexer" is an object that directly manages a UDP link used by an SRT
socket. An SRT socket may only use one multiplexer, but a multiplexer may be
shared between SRT sockets. A multiplexer uses exactly one UDP socket (maybe in
future this can be also abstracted out, so it's better to think of a multiplexer
as an object that deals directly with a single system resource that maintains
the UDP link).

There's one small limitation that SRT obviously can't overcome: it's the UDP
port. A UDP port occupied by a single application can't be shared with other
applications (or, even though such a possibility sometimes exist, this wouldn't
result in anything SRT can make use of). This way, when you have an SRT listener,
it must be bound to a single UDP port.

The use of UDP socket is different for listener side and caller side.

For listener, you use first one UDP socket for the listener SRT socket itself.
Listening means that the multiplexer facility reads from the given port and is
prepared to receive "handshake connection request" control packets. Normally
one of the data inside the header is the target socket ID, but in the
handshake packets for connection request this holds a special value 0, as per
conenction request. The first portion of the connection, `URQ_INDUCTION` only
exchanges the cookie, and no resources are allocated. Only with
`URQ_CONCLUSION` is the connection really established, but at the moment when
this handshake message comes in, it's not managed for the sake of the listener
socket anymore. Instead a new socket is spawned - it's often referred to as
"accepted socket" - and the handshake packet is dispatched into this. After
the handshake process is successful, this "accepted socket" is returned from
the `srt_accept` function. However, this socket shares the multiplexer with
the listener socket. This means that if there are any data packets or other
command packets coming into the UDP port that is currently used by listener,
they come in the frames of this connection. Such packets have their target
socket ID written in the "TARGET SOCKET ID" field, and basing on this value
this packet is then dispatched into the correct SRT socket. Only if this
packet has "TARGET SOCKET ID" value 0 and the packet is a control packet of
type `UMSG_HANDSHAKE` is this packet dispatched to the listener socket.

This simply means that the "accepted socket" always shares a multiplexer with
the listener socket, from which it was spawned. It also means that all these
socket share one UDP socket and occupy one UDP port. This way also all UDP
packets that are sent for the sake of various SRT sockets will have exactly
the same source (outgoing) port. The only way to distinguish the connection is
the target socket ID in the SRT header.

For caller sockets this matter is a little different, however sharing a
multiplexer is also possible.

Normally, when you create a socket for connecting it to a listener on the
other side you don't specify a specific source (outgoing) port. Binding a
socket before connecting is only obligatory for listener sockets and
rendezvous sockets, which is because the port must be known so that the other
party knows what to specify in the connecting parameters. For connecting
sockets this binding is optional, and if not done, then they will be bound to
an address specified as "0.0.0.0:X", where X is a system autoselected port.
This is something that is already provided by the system - when the system
function `bind` is called with given port number 0, then the socket will be
bound to a system-selected first free port searched starting from the highest
possible value, so usually this port is in the upper part of 16-bit range
(32768-65535). You can extract this port number by getting the full sockaddr
object by `srt_getsockname` function.

However, you can bind the socket for connecting as well, and this way it will
have the port that you give it explicitly. Binding a socket is also useful if
you want to bind it only to one of the network interfaces in the system, not
all of them (as happens by default), and this way you can still use the port
number 0 to enforce system autoselection. Even though a port can't be shared
between applications, it can still be shared between multiple sockets in one
application. So, when you bind a socket to a given port, and in the internal
global SRT data there is found a multiplexer that is already bound to that
port, then this multiplexer will be attached to this socket instead of
creating a new one.

By sharing one port between multiple calling sockets, you make it possible to
use one UDP link to send multiple independent data streams that logically
belong to completely different SRT connections. This may be important for
cases when you have some firewall settings that allow UDP packets sent only
over given UDP link.


# Handshake


# Sending


# Receiving



# SRT transmission reliability mechanism


SRT mainly relies in the reliability on the same mechanism that TCP is using,
called ARQ.

SRT relies on the system UDP protocol to do its transmission work, and so are
particular packets used for either transmitting the payload or sending control
information, hence we talk about DATA packets and CONTROL packets. The extra
SRT header in every SRT packet, of constant size, is used to distinguish the
type of the packet as well as provide extra information.

This extra information in case of data packets is the Sequence Number (SN). The
number used here comes from a domain called "circular number". Note that
sequence numbers are 31-bit.


## Explanation for "circular numbers"


It's a number that simulates infinite numbers, that is, you can always increase
it by one and still have space in the memory to record it, at the expense of
having limited range of how distant two such numbers can be to one another.
Therefore in case of these numbers we speak about "distance" rather than
"difference": circular numbers are implemented by having a maximum numeric
value, and if the operation of increasing has reached it, it becomes zero.
Similarly, when zero is decreased by one, it turns to the maximum value.
Therefore the distance between two such numbers can be calculated as a
difference of two numeric value only in case when this difference is below the
half of the maximum (called "threshold" value). It should be then assumed that
if two values are tried to be tested for distance between one another, they
both must come from the same domain, must start from the same value, just be
possibly increased in different pathways, but only up to some distance. Trying
to compare two circular numbers coming from an unknown source results in an
undefined behavior.

For example, if you compare two circular numbers, one being the maximum value
decreased by 2 and the other being 2, the distance between them counts 4. Even
though if you make a subtraction operation, the difference will be maximum
value minus 4. However the distance between 2 and 10 counts 8 and it's equal to
the difference.


## Sending in the Sequence Number order


Simply, when sending the data, they are stored in the sender buffer first, so
that they can be later picked up and sent over the network. Every packet being
sent over the network is assigned a sequence number. The so called Initial
Sequence Number (ISN), is agreed upon during the handshake, and it will be the
first sequence number of a packet being sent by the sending party. It is then
incremented (mind what it means for circular numbers) with every packet sent,
so the next packet gets the next sequence number in order.

It is then expected at the receiver side that packets come in also in order.
When this doesn't happen, appropriate actions are undertaken.


## Minding the order at the reception side


In the reception side then when a packet is received, let's state currently
in order, then its sequence number is recorded as the last received sequence
number. When the next packet comes in then, it is expected that the distance
between this packet's sequence number and the last received sequence number
is exactly 1. Otherwise we have one of two situations:

1. The distance is 0 or negative. It means that the sequence number is behind
the latest one, and this can mean one of the following things:
   - **belated** packet. It means that a packet with that sequence number is
already received and this one is somehow duplicated or mistakenly retransmitted;
in such a situation it's simply discarded as unnecessary
   - **recovered** packet. This packet was lost, but it was then retransmitted
and the loss has been "sealed".
   - **reordered** packet. This has come as late only because the routers on
the way of transmission have somehow changed the order of packets towards
the order in which they were originally sent. Note that in distinction to
**belated** and **recovered** packets, the reordered packet differs to them
by not having the R (retransmission) flag set so that these situations can
be distinguished.

2. The distance is greater than 1. It means that there's a "gap" between
the "train" (the sequence of the incoming packet) and the "platform" (the last
received sequence). This value decreased by 1 is the number of the lost packets
and the lost packets are counted all those between these two sequence numbers.
When this happens, the loss report is prepared and sent immediately, except
a situation of a reordering control, controlled by `SRTO_LOSSMAXTTL` option.

When packets come in in order, at least up to some sequence, the receiver side
sends periodically a control packet that informs the sender that the packets
have been successfully received - ACK (acknowledgement). This contains the
sequence number that is the number of the last successfully received packet
in order, increased by one (that is, effectively, either a number of the next
expected packet, or a number of the first lost packet). This informs the
sender that all packets up to (excluding) this one can be now removed from
the sender buffer.


## Detecting a loss and reporting


When you have a sequence number, for example 1255, as the last received
sequence number, and then the received packet is 1259, it means that we have 3
packets lost, with sequence numbers 1256, 1257, and 1258. The loss is then
recorded for the needs of future tracking the lost packets, and the loss report
is sent. The syntax for the lossreport is prepared to hold ranges. As the
SNs are 31-bit, the last most significant bit is used to mark the first of
two numbers defining a range, where the end of range is in the next cell,
otherwise it's a single sequence number. Note that the immediate loss report
reports always only one range.


## Reordering detection


Reordering is a phenomenon that usually should not occur in a public internet
other than accidental, however in some types of network it may happen that
its character is rather systematic than accidental.

There are two problems that may arise due to reordering:

1. **Different order of data at the reception side.** This is however taken
care of due to using the sequence numbers, and as well as the loss and
recovery main use, they help with this problem, too. Simply the data will
be given up to the receiver application always in the same order as they
were sent, as the order is defined by sequence numbers.

2. **False loss report**. This is a minor problem in case when the reordering
has an accidental character, but it may rise to serious problem when it's
regular. It means that if the packet with the expected didn't come before the
incoming one just because this will simply come in different order, the real
fate of the jumped-over packet is unknown until it finally comes, and it's
by default treated as lost, and therefore it results in a lossreport and
retransmission. The loss recovery is always treated as a high priotity sending
in both reporting and recovery, so even if this error is later seen, there
will always be too late to do anything. The packet will be loss-reported,
retransmitted, wasting some bandwidth this way, and then discarded as the
packet will be still received normal way.

An interesting fact about reordering is that during tests with artificial
reordering imposed by netem it could even happen that the retransmission was
triggered by a reordered packet, and the retransmitted packet came in earlier
than the originally sent packet. It means that the packet was still once
discarded, but retransmission had helped this packet arrive sooner.

However if the reordering is systematic, this false loss report may burden
the link a lot with a useless retransmission. This can be mitigated then by
setting the `SRTO_LOSSMAXTTL` option to a positive value. This defines a
value up to which the reorder tolerance may grow. By default it's 0, which
means that the reorder tolerance will never grow.

The reorder tolerance grows upon reordering detection. Reordering detection
relies mainly on the R (retransmission) flag in the flag field in the header.
This flag is 0 for regularly sent packets and 1 for retransmitted packets.
This means that if a late packet comes in (see above), it should usually have
the R flag set, as this is the packet that was sent on request triggered by
the loss report. If it happened, however, that such a packet has the R flag
clear, it was sent regularly, so late coming means that it was reordered.

If this situation is detected, then the distance between the last received
sequence (which is always the farthest sequence number ever seen) and the
sequence if the incoming packet (negated, as it's negative in this case) is
set as the current reorder tolerance. Please note that "detection" of this
reordering still costs sending one false loss report. But setting the reorder
tolerance to this value will prevent it, when it happens next time.

Reorder tolerance requires that this number of packets must come in first
before any loss report is sent (no matter if in order or not!). This should
give long enough time to have a chance for the reordered packet to come in
later. If this happens then, the record of the lost packet will be removed
before any loss report can be sent.

Note however that this reorder tolerance brings up a latency penalty. The
recovery is predicted normally to happen as fast as possible because the
more time it takes, the more chance that it will cause the stream holdup,
which in case of file stream means to stop transmission for a while, and
with live stream it may result in recovery failure and packet dropping.
To mitigate this, you may need to use higher value of latency.


## Conditional dropping - TLPKTDROP


This is a mechanism used by default in live mode. The problem is that when
a packet is lacking due to have been lost, it holds up the whole stream,
while "the show must go on". If this is allowed (which can be done by
turning the `SRTO_TLPKTDROP` flag off), then one lost packet may stop
the whole transmission until it's finally recovered, then the whole stopped
stream might be given up to the receiver application by even several
seconds of the stream at once. This is rather unwanted, especially that
it's not predictable as to how long the stream can be held up; therefore
a more preferable way in case of live streams is to allow errors to happen
for a while, without stopping the transmission.

When this mode is on then, it's usually connected with the "TSBPD mode",
which is required to work with this. This means that the timestamp as set
at the sender side is decoded at the receiver side, with embracing the
drift and preset latency, as the time when this packet is expected to be
submitted to the receiver application - and until then it stays in the
buffer. This time when it remains in the buffer is the time when it's
expected that all lost packets be recovered. When the time "would come"
to play for a lost packet (which we don't know because the packet was
lost, and so was its time information), still nothing happens. However
if the time has come to play a packet that immediately follows the lost
one, it's agreed to be delivered at that time to the receiver, even though
it requires that the lost packets, that can no longer be delivered, be now
forgotten forever.

This mechanism works also on the sender side so that packets that would be
discarded anyway by the receiver will be also prevented from sending so that
the link bandwidth isn't wasted. This mechanism is controlled by the
`SRTO_SNDDROPDELAY` option. This option allows to give more time tolerance
for retransmitting a packet that is suspected to not arrive on time anyway,
or even turn this mechanism off, in which case packets will be always
retransmitted as requested.

The TLPKTDROP mechanism on the receiver side makes a "fake ACK", that is,
packets are being acknowledged at the next periodic ACK time to be marked
as if they were really received.


## Periodic NAK report


Beside the initial lossreport that is sent upon loss detection, there's
another mechanism that uses sending the loss report. After sending a loss
report, it is then given time to recover it, that is, the sender side should
receive the loss report and schedule the lost packets to be sent again.
When the packet doesn't arrive at the expected time, the periodic NAK
report is sent. This means, the lossreport is sent again, this time
however containing all the loss records collected so far.

This mechanism is used to mitigate such problems as lost loss report itself,
or losing again packets that were retransmitted. It is still only about
decreasing the possibility of extra problems.


## Ultimate recovery and blind retransmission


When a packet was detected as lost, and the recovery failed (either the
report or the recovered packet was lost again), and this has even happened
again even with the periodic NAK report mechanism working, SRT is finally
coming to a situation of "ultimate recovery". This is a situation that
is defined by the following state at the sender side:

- for all reported losses it already sent a recovery packet
- ACK is not moving forward, and is still way behind the last sent sequence
(in other words: the flight window has reached the value of congestion
window)

When this happens, the "blind retransmission" is undertaken periodically,
that is, all packets between the last acknowledged and last sent are
sent again, as retransmitted. As the flight window size has grown up to
the allowed maximum, no new packets are also sent, until the situation
resolves.

Note that in live mode this usually doesn't happen, as long as the default
values (true) of `SRTO_NAKREPORT` and `SRTO_TLPKTDROP` are set. The periodic
NAK report decreases the probability of the "double loss" problem, and the
TLPKTDROP mechanism prevents the flight window from growing due to an
irrecoverable loss, by simply skipping it.


## Builtin FEC facility


The builtin FEC module is an alternative way to increase reliability of
the stream by adding redundant information. This then increases the stream
reliability at the expense of extra bandwidth occupation. This is not a
place to describe the whole mechanism, rather an explanation how it works
with other reliability mechanisms in SRT.

The filtering mechanism, on which the FEC is based, is placed before the
loss detection facility, and the filter, if used, is considered the real
deliverer. However the loss is still reported by the filter anyway, even
if the filter is capable of recovering a packet - otherwise the packet is
considered received, or fake-received as per TLPKTDROP, and after this
happens, the packet is never further recovered.

However, it doesn't mean that every loss is reported by the loss report.
This is reported only when so requested - which happens when the ARQ is
set to "always" mode. Usually, with "onreq" mode, only those are reported
that are considered not recoverable by the filter, and with "never" nothing
is loss-reported at all. Normally, when the FEC facility could recover the
packet, it will be then a situation as if two packets are received at once,
whereas one of them is "late", and it has the R flag set to prevent it from
being treated as reordered. With a bit of good luck, however, it may also
happen that a lost packet was the last packet in the recovery group in FEC, so
the recovery was triggered by the FEC control packet before anyone could notice
a loss - in which case it won't cause a loss to be reported, even if ARQ
is set to "always".


## Collecting statistical information


There are several general rules driving the statistical information that
is collected during transmission.

The receiver collect loss packet information as per the "gap" in the
sequence numbers. The sender only knows about the lost packets from the
loss reports - however, it doesn't count multiple times any multiple loss
reports - counted are only those single packets that were lost.

Among the lost packets at the receiver side there would be then some number
of recovered packets and some number of dropped packets (TLPKTDROP). The
difference between these two is the number of recovered packets.

Packets that come late that have the R flag set are considered retransmitted
packets and they are counted as retransmitted. The number of retransmitted
received packets decreased by the number of recovered packets gives you
the number of wastefully retransmitted packets.

Some packets that were lost could be also dropped at the sender side; this
number should be less or equal to the number of dropped packets at the
receiver side. Tweaking the parameters may help keeping these numbers at
minimum. Most important parameters are:

1. `SRTO_RCVLATENCY` (also `SRTO_LATENCY`). This gives an extra delay to
the stream transmission so that it can accumulate any short-term jitter
and give time to recover a loss. Greater value may improve stream reliability
on poorer networks, at the expense of more delay put on playing the stream.

2. `SRTO_SNDDROPDELAY`. This allows to control whether recovery is denied
when the sender considers a packet "not playable by the receiver", at the
expense of possibly wasted link for useless transmission.

3. `SRTO_LOXXMAXTTL`. In case when you experience a systematic packet
reordering, you can set this to some small positive value so that reordering
doesn't cause false loss report and therefore waste the link bandwidth

4. `SRTO_MAXBW`. This limits the overall bandwidth of the transmission.
In case of live mode it's only meaningful for the retransmission so that
retransmitting a large amount of lost packets doesn't cause a spike and
possibly another congestion.



# Live mode features

## TSBPD mode

TSBPD (TimeStamp-Based Packet Delivery) is a mechanism that aims to replay
packet times from the source to the destination. For that the timestamp
field in SRT is reused, which is a 32-bit field containing the relative
time.

As this is a relative time, there's also needed the time base. The time
base is recorded during the handshake on both sides independently,
although it is believed that it should happen in almost the same time
on both sides, while the difference results from the RTT time (in
particular, the single trip time) are ruled out.

Also, this time base is being updated by a "block" value (maximum
32-bit unsigned value) after the 32-bit relative time gets overflown.

This time base added to the value read from the timestamp is considered
the "expected arrival time". This value, plus the latency as defined
on the receiver side (by `SRTO_RCVLATENCY` option), results in
"play time". This is the time up to which the packet should remain
in the buffer, and after that time the packet is allowed for the
application to be extracted.

This also means that the Timestamp field in the packet has a value
assigned to the packet's identity - its contents and sequence number.
This need not hold true when the `SRTO_TSBPDMODE` is off.

The calculation of the "play time" consists of:

- "expected arrival time" (time base + timestamp)
- latency (as configured by `SRTO_RCVLATENCY`)
- drift (see [Drift tracing](#drift-tracing))


## TLPKTDROP

The Too-Late packet drop is a mechanism connected to TSBPD, which causes
that a packet may be conditionally dropped.

On the receiver side, this is the mechanism preventing the "hold the line"
effect in case when recovering a packet takes too long. This is based
on the TSBPD and the defined "play time" for a packet. The packet should
be delivered to the application at the moment when the "play time" comes.
But it may happen that the packet, for which the "play time" has come,
is preceded by some lost packets, which still wait for recovery. In this
case, the packet, that is "ready to play", is delivered to the application,
and all preceding lost packets are "dropped", that is, lost forever.

This mechanism can be turned off by setting `SRTO_TLPKTDROP` to false.


## Drift tracing

The time base is set to more-less the same value on both sides and
then the local time is based on the timestamp and the time base,
independently on every side. However the clocks on both machines
need not run exactly with the same speed. In the beginning this isn't
a big problem, but after some time it may result in that the clocks
on both sides refer to different times.

Drift tracing relies on comparison between the "expected arrival time"
(see description in [TSBPD mode](#tsbpd-mode)) and the actual arrival
time. The values from this difference are calculated with a long time
IIR average (in case of no-jitter situation, and perfect clock
synchronization this difference should be 0, or at least some all-time
constant value), and then it is included in the TSBPD time calculation.


