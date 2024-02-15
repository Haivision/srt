General statement about latency
===================================

In the live streaming there are many things happening between the 
camera's lens and the screen of the video player, all of which contribute 
to a delay that is generally referred to as "latency". This overall latency 
includes the time it takes for the camera frame grabber device to pass 
frames to the encoder, encoding, multiplexing, **sending over the network**, 
splitting, decoding and then finally displaying. 

In SRT, however, "latency" is defined as only the delay introduced by **sending 
over the network**. It's the time between the moment when the `srt_sendmsg2` 
function is called at the sender side up to the moment when the `srt_recvmsg2` 
function is called at the receiver side. This SRT latency is the actual time difference 
between these two events.

The goal of the latency (TSBPD) mechanism
=========================================

The strict goal of this mechanism is to have the time distance between two
consecutive packets on the receiver side identical as they were at the
sender side. Obviously this requires some extra delay defined from upside
that should define when exactly the packet can be retrieved by the receiver
application, and if the packet arrived earlier than this time, it will have to
wait in the receiver buffer until this time comes. This time for the packet N
is roughly defined as:

```
PTS[N] = ETS[N] + LATENCY(option)
```

where `ETS[N]` is the time when the packet would arrive, if all delays
from the network and the processing software on both sides are identical
as they were for the very first received data packet. This means that
for the very first packet `ETS[0]` is equal to this packet's arrival time.
For every next packet the delivery time distance should be equal to the same
packet's declared scheduling time distance.


SRT's approach to packet arrival time
=====================================

SRT provides two socket options `SRTO_PEERLATENCY` and `SRTO_RCVLATENCY`,
where the "latency" name was used for convenience (also for a common option
`SRTO_LATENCY`), but it doesn't mean that it will define the true time
distance between the `srt_sendmsg2` and `srt_recvmsg2` calls for the same
packet. This is only an extra delay added at the receiver side towards
the time when the packet "should" arrive (ETS). This extra delay is used to
compensate two things:

* The extra network delay, that is, if the packet arrived later than it
"should have arrived"

* The packet retransmission, regarding that there might be a need to be
requested at least twice

Note that the values included in these formulas are values that are
actually present, but many of them are not controllable and many are
not even measurable. In many cases there are measured values that are
sums of other values, but ingredients can't be extracted. Values that
we get at the receiver side are actually two:

* ATS: actual arrival time. It's simply the time when the UDP packet
has been extracted through the `recvmsg` system call.

* TS: time recorded in the packet header, set on the sender side and extracted
from the packet at the receiver side

Note that the timestamp in the packet's header is 32-bit, which gives
it more-less 2.5 minutes to roll over. Therefore there is the timestamp
rollover tracked and a segment increase is done in order to keep an
eye on the overall actual time. For the needs of the formula definitions
it will be stated that TS is the true difference between the connection
start time and the time when the sending time has been declared when
the sender application is calling any of the `srt_send*` functions
(see [`srt_sendmsg2`](../API/API-functions.md#srt_sendmsg2) for details).


SRT latency components
======================

To understand the latency components we need also other definitions:

* ETS: expected arrival time. This is the time of the packet when it
"should" arrive according to its TS

* PTS: packet's play time. It's the time when SRT gives up the packet
to the `srt_recvmsg2` call (that is, it sets up the IN flag in epoll
and resumes the blocked function call, if it was in blocking mode).

* STS: the time declared as the sending time when the packet was
scheduled for sending at the sender side (if you don't use the
declared time, by default it's the monotonic time taken when this
function was called), which is represented by TS.

* RTS: the same as STS, but calculated at the receiver side. The
only way to extract it is by using some initial statements.

The "true latency" for a particular packet in SRT can be simply defined as:

* `TD = PTS - STS`

Note that this is a stable definition (independent on the packet),
but this value is not really controllable. So let's define the PTS
for the packet `x`:

* `PTS[x] = ETS[x] + LATENCY + DRIFT`

where `LATENCY` is the negotiated latency value (out of the
`SRTO_RCVLATENCY` on the agent and `SRTO_PEERLATENCY` on the peer)
and DRIFT will be described later (for simplification you can
state it's initially 0).

These components undergo the following formula:

* `ETS[x] = start_time + TS[x]`

Note that it's not possible to simply define it basing on STS
because sender and receiver are two different machines that can only
see one another through the network, but their clocks are separate,
and can even run on different or changing speeds, while the only
visible phenomena happen only at a packet arrival machine. This
above formula, however, allows us to define the start time because
we state the following for the very first data packet:

* `ETS[0] = ATS[0]`

This means that from this formula we can define the start time:

* `start_time = ATS[0] - TS[0]`

Therefore we can state that if we have two identical clocks on
both machines with identical time bases and speeds, then:

* `ATS[x] = program_delay[x] + network_delay[x] + STS[x]`

(The only problem with treating this above formula too seriously
is that there doesn't exist the common clock base for two
network-communicating machines, so these components should be
treated as something that does exist, but isn't exactly measurable).

But even if there is still this formula for ATS, it doesn't
apply to the real latency - this one is based strictly on ETS.
But you can apply this formula for the very first arriving
packet, because for this one they are equal: `ATS[0] = ETS[0]`.

Therefore this formula is true for the very first packet:

* `ETS[0] = prg_delay[0] + net_delay[0] + STS[0]`

We know also that the TS set on the sender side is:

* `TS[x] = STS[x] - snd_connect_time`

Taking both formulas for ETS together:

* `ETS[x] = start_time + TS[x] = prg_delay[0] + net_delay[0] + snd_connect_time + TS[x]`

we have then:

* `start_time = prg_delay[0] + net_delay[0] + snd_connect_time`

Note important thing: `start_time` is not the time of arrival of the first packet,
but that time taken backwards by using the delay already recorded in TS. As TS should
represent the delay towards `snd_connect_time`, `start_time` should be simply the same
as `snd_connect_time`, just on the receiver side, and so obviously shifted by the
first packet's delays of `prg_delay` and `net_delay`.

So, as we have the start time defined, the above formulas:

* `ETS[x] = start_time + TS[x]`
* `PTS[x] = ETS[x] + LATENCY + DRIFT`

define now the packet delivery time as:

* `PTS[x] = start_time + TS[x] + LATENCY + DRIFT`

and after replacing the start time we have:

* `PTS[x] = prg_delay[0] + net_delay[0] + snd_connect_time + TS[x] + LATENCY + DRIFT`

and for the formula of TS we get STS, so we replace it:

* `PTS[x] = prg_delay[0] + net_delay[0] + STS[x] + LATENCY + DRIFT`

so the true network latency in SRT we can get by moving STS to the other side:

* `PTS[x] - STS[x] = prg_delay[0] + net_delay[0] + LATENCY + DRIFT`


The DRIFT
=========

The DRIFT, for simplifyint the calculations above, should be treated as 0,
which is the initial state. In time, however, it gets changed basing on the
value of the Arrival Time Deviation:

* `ATD[x] = ATS[x] - ETS[x]`

The drift is then formed as:

* `DRIFT[x] = average(ATD[x-N] ... ATD[x])`

The value of the drift is tracked by appropriate number of samples and if
it exceeds a threshold value, the drift value is applied to modify the
base time. However, as you can see from the formula for ATD, the drift is
simply taken from the real time when the packet was arrived, and the time
when it would arrive, if the `prg_delay` and `net_delay` values were
exactly the same as for the very first packet. ATD then represents the
changes in these values. There can be two main factors that could result
in having this value as nonzero:

1. There has been observed a phenomenon in several types of networks that
the very first packet arrives very quickly, but then as the data packets
come in regularly, the network delay slightly increases and then stays
for a long time with this increased value. This phenomenon could be
mitigated by having a reliable value of RTT, so once it's observed as
increased, a special factor could be used to decrease the positive value
of the drift, but this currently isn't implemented. This phenomenon also
isn't observed in every network, especially in a longer distance.

2. The clock speed on both machines isn't exactly the same, which means
that if you decipher the ETS basing on the TS, after time it may result
in values that even precede the STS (and this way suggesting as if the
network delay was negative) or having an enormous delay (with ATS exceeding
PTS). This is actually the main reason of tracking the drift.





