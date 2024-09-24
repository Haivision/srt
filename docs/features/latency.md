## General statement about latency

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


## The goal of the latency (TSBPD) mechanism

SRT employs a TimeStamp Based Packet Delivery (TSBPD) mechanism 
with strict goal of keeping the time interval between two consecutive packets 
on the receiver side identical to what they were at the sender side. This 
requires introducing an extra delay that should define when exactly the packet 
can be retrieved by the receiver application -- if the packet arrives early, it must
wait in the receiver buffer until the delivery time. This time for a packet N
is roughly defined as:

```
PTS[N] = ETS[N] + LATENCY(option)
```

where `ETS[N]` is the time when the packet would arrive, if all delays
from the network and the processing software on both sides are identical
to what they were for the very first received data packet. This means that
for the very first packet `ETS[0]` is equal to this packet's arrival time.
For every following packet the delivery time interval should be equal to the
that packet's declared scheduling time interval.


## SRT's approach to packet arrival time

SRT provides two socket options `SRTO_PEERLATENCY` and `SRTO_RCVLATENCY`.
While they have "latency" in their names, they do *not* define the true time
interval between the `srt_sendmsg2` and `srt_recvmsg2` calls for the same
packet. They are only used to add an extra delay (at the receiver side) to
the time when the packet "should" arrive (ETS). This extra delay is used to
compensate for two things:

* an extra network delay (that is, if the packet arrived later than it
"should have arrived"), or

* a packet retransmission.

Note that many of the values included in these formulas are not controllable and 
some cannot be measured directly. In many cases there are measured values 
that are sums of other values, but the component values can't be extracted. 

There are two values that we can obtain at the receiver side:

* ATS: actual arrival time, which is the time when the UDP packet
has been extracted through the `recvmsg` system call.

* TS: time recorded in the packet header, set on the sender side and extracted
from the packet at the receiver side

Note that the timestamp in the packet's header is 32-bit, which gives
it more or less 2.5 minutes to roll over. Therefore timestamp
rollover is tracked and a segment increase is performed in order to keep an
eye on the overall actual time. For the needs of the formula definitions
it must be stated that TS is the true difference between the connection
start time and the time when the sending time has been declared when
the sender application is calling any of the `srt_send*` functions
(see [`srt_sendmsg2`](../API/API-functions.md#srt_sendmsg2) for details).


## SRT latency components

To understand the latency components we need also other definitions:

* **ETS** (Expected Time Stamp): The packet's expected arrival time, when it
"should" arrive according to its timestamp

* **PTS** (Presentation Time Stamp): The packet's play time, when SRT gives the packet
to the `srt_recvmsg2` call (that is, it sets up the IN flag in epoll
and resumes the blocked function call, if it was in blocking mode).

* **STS** (Sender Time Stamp): The time when the packet was
scheduled for sending at the sender side (if you don't use the
declared time, by default it's the monotonic time used when this
function is called).

* **RTS** (Receiver Time Stamp): The same as STS, but calculated at the receiver side. The
only way to extract it is by using some initial statements.

The "true latency" for a particular packet in SRT can be simply defined as:

* `TD = PTS - STS`

Note that this is a stable definition (independent of the packet),
but this value is not really controllable. So let's define the PTS
for a packet `x`:

* `PTS[x] = ETS[x] + LATENCY + DRIFT`

where `LATENCY` is the negotiated latency value (out of the
`SRTO_RCVLATENCY` on the agent and `SRTO_PEERLATENCY` on the peer)
and DRIFT will be described later (for simplification you can
state it's initially 0).

These components undergo the following formula:

* `ETS[x] = start_time + TS[x]`

Note that it's not possible to simply define a "true" latency based on STS
because the sender and receiver are two different machines that can only
see one another through the network. Their clocks are separate,
and can even run at different or changing speeds, and the only
visible phenomena happen when packets arrive at the receiver machine. 
However, the formula above does allow us to define the start time because
we state the following for the very first data packet:

* `ETS[0] = ATS[0]`

This means that from this formula we can define the start time:

* `start_time = ATS[0] - TS[0]`

Therefore we can state that if we have two identical clocks on
both machines with identical time bases and speeds, then:

* `ATS[x] = program_delay[x] + network_delay[x] + STS[x]`

Note that two machines communicating over a network do not typically have a
common clock base. Therefore, although this formula is correct, it involves
components that can neither be measured nor captured at the receiver side.

This formula for ATS doesn't apply to the real latency, which is based strictly 
on ETS. But you can apply this formula for the very first arriving packet, 
because in this case they are equal: `ATS[0] = ETS[0]`.

Therefore this formula is true for the very first packet:

* `ETS[0] = prg_delay[0] + net_delay[0] + STS[0]`

We know also that the TS set on the sender side is:

* `TS[x] = STS[x] - snd_connect_time`

Taking both formulas for ETS together:

* `ETS[x] = start_time + TS[x] = prg_delay[0] + net_delay[0] + snd_connect_time + TS[x]`

we have then:

* `start_time = prg_delay[0] + net_delay[0] + snd_connect_time`

**IMPORTANT**: `start_time` is not the time of arrival of the first packet,
but that time taken backwards by using the delay already recorded in TS. As TS should
represent the delay towards `snd_connect_time`, `start_time` should be simply the same
as `snd_connect_time`, just on the receiver side, and so shifted by the
first packet's delays of `prg_delay` and `net_delay`.

So, as we have the start time defined, the above formulas:

* `ETS[x] = start_time + TS[x]`
* `PTS[x] = ETS[x] + LATENCY + DRIFT`

now define the packet delivery time as:

* `PTS[x] = start_time + TS[x] + LATENCY + DRIFT`

and after replacing the start time we have:

* `PTS[x] = prg_delay[0] + net_delay[0] + snd_connect_time + TS[x] + LATENCY + DRIFT`

and from the TS formula we get STS, so we replace it:

* `PTS[x] = prg_delay[0] + net_delay[0] + STS[x] + LATENCY + DRIFT`

We can now get the true network latency in SRT by moving STS to the other side:

* `PTS[x] - STS[x] = prg_delay[0] + net_delay[0] + LATENCY + DRIFT`


## The DRIFT

The DRIFT is a measure of the variance over time of the base time. 
To simplify the calculations above, DRIFT is considered to be 0,
which is the initial state. In time, however, it changes based on the
value of the Arrival Time Deviation:

* `ATD[x] = ATS[x] - ETS[x]`

The drift is then calculated as:

* `DRIFT[x] = average(ATD[x-N] ... ATD[x])`

The value of the drift is tracked over an appropriate number of samples. If
it exceeds a threshold value, the drift value is applied to modify the
base time. However, as you can see from the formula for ATD, the drift is
simply taken from the actual time when the packet arrived, and the time
when it would have arrived if the `prg_delay` and `net_delay` values were
exactly the same as for the very first packet. ATD then represents the
changes in these values. There can be two main factors that could result
in having this value as non-zero:

1. A phenomenon has been observed in several types of networks where
the very first packet arrives quickly, but as subsequent data packets
come in regularly, the network delay slightly increases and then remains fixed
for a long time at this increased value. This phenomenon can be
mitigated by having a reliable value for RTT. Once the increase is observed 
a special factor could be applied to decrease the positive value
of the drift. This isn't currently implemented. This phenomenon also
isn't observed in every network, especially those covering longer distances.

2. The clock speed on both machines (sender and receiver) isn't exactly the same, 
which means that if you decipher the ETS basing on the TS, over time it may result
in values that even precede the STS (suggesting a negative network delay) or that 
have an enormous delay (with ATS exceeding PTS). This is actually the main reason 
for tracking the drift.
