# Retransmission

Retransmission is the mechanism of recovering lost packets by sending them
again; also known as ARQ.

As a derivative from UDT, which was the mechanism for file transmission, this
was essential for the reliability - the stream must be received exactly as
sent, as long as the connection is maintained.

The live mode with retransmissions is possible, but various changes have to be
applied so that this can cooperate with live mode.


## Reporting a loss

The general mechanism relies on a simple rule:

* As packets are received, they are tried to be inserted into the receiver
buffer. Packets are rejected if

  * their sequence number would place them outside the current capacity of
the receiver buffer.

  * at the position designated by the sequence number there already is a
packet.

* Packets that are earlier than the newest one, but still within the range of
the buffer, and the cell at that position points to a missing packet, it's
placed there as recovery.

* If the sequence number of the packet is the newer than any packet's sequence
number received so far, and it doesn't immediately follow that last sequence,
it's interpreted as a loss, and the receiver side sends the `UMSG_LOSSREPORT`
control packet. That control packet contains the loss range between the
expected new sequence and the predecessor of the inserted packet.

The loss report is sent immediately, but the loss information is also recorded
in the loss list for further periodic retransmission.

Note that this mechanism gets a bit complicated if the stream is transmitted
over a group connection and multiple links may deliver packets with different
reception time. This doesn't concern the Backup group because in this group
there's only one link intended for transmitting, but it does concern the
Broadcast (and potentially Balancing) groups. In this case the loss should
be remembered, but not reported, until it's ascertained that the other link
will not deliver the packet detected as lost.

There's also additional mechanism intended for links where packet reordering
is expected to often occur. This can be set by the `SRTO_LOSSMAXTTL` option
and if set it allows the value of reorder tolerance to grow up to this value.
Reorder tolerance increses with reordering detected and it allows to wait
for so many packets to arrive after the detected loss so that the loss can
be reported.

## Direct retransmission

Upon reception of `UMSG_LOSSREPORT`, the sender side records scheduling of
the reported packets for retransmission. This is marked in the sender buffer
as an embedded container, which marks packets eligible for retransmission.
This causes that next time when the sender thread loop asks the socket for
providing the information of the new packet to send, the socket will provide
this packet as the next one to be sent. Retransmitted packets are removed
from the schedule and considered recovered until the receiver party denies it.

The `SRTO_RETRANSMITALGO` option controls whether any packets are blocked
from being retransmitted, if the time that elapsed since their last
retransmisssion is less than the "optimistic RTT". This mainly applies to the
periodic retransmission.

## Delaying loss reports

Originally the earliest versions of SRT have been reported losses always
immediately - and that's in most cases the best approach, which ensures
fastest possible loss recovery, and in case of TLPKTDROP setting (which is
default in live mode), it gives best chances of not dropping the lost packet.

However, sometimes there are networks, where the user wants to tolerate higher
latency, and it's prone for packet reordering. If this happens often, then a
reordered packet may come in originally, just later, but then a gap in the
sequence numbers may cause it considered a loss before the original packet
has a chance to come. Therefore there is a possibility to set up a tolerance
for reordering through `SRTO_LOSSMAXTTL`. The mechanism triggers in the
`acquireDataPacket` function, where the losses are detected and handled. This
is saved in a separate loss list with their TTL.

PERFORMANCE CONSIDERATIONS:

This list is quite inefficient as a data type and finding the candidate to send
`UMSG_LOSSREPORT` is linear time. On the other hand, there are some special cases
that are important for performance:

- only the first (plus some following) could have had TTL drown to 0

- the only (little likely) possibility that the next-to-first record has TTL=0
  is when there was a loss range split (due to dropFromLossLists() of one sequence)

- first found record with TTL>0 means end of "ready to LOSSREPORT" records

So, all you have to do is:

 - start with first element and continue with next elements, as long as they have TTL=0
   If so, send the loss report and remove this element.

 - Since the first element that has TTL>0, iterate until the end of container and decrease TTL.

This will be efficient because the loop to increment one field (without any condition check)
can be quite well optimized.

Additional action is undertaken in the `CUDT::unlose` function. This is called
for a packet coming in NOT ahead of the newest incoming sequence, normally a
potential loss recovery. The sequence number is then tried to be removed from
both loss records: the general loss record and the fresh loss record.

Additionally, it checks whether the "latecoming" packet has been sent due to
retransmission or due to reordering, by checking the rexmit flag. If this
packet was surely ORIGINALLY SENT it means that the current network connection
suffers of packet reordering. This way it tries to introduce a dynamic
tolerance by calculating the difference between the current packet reception
sequence and this packet's sequence. This value will be set to the tolerance
value, which means that later packet retransmission will not be required
immediately, but only after receiving N next packets that do not include the
lacking packet. The tolerance is not increased infinitely - it's bordered by
`m_config.iMaxReorderTolerance`. This value can be set in options -
`SRTO_LOSSMAXTTL`.


## Periodic retransmission

There's only one way to know that the retransmission was successful: when the
`UMSG_ACK` control packet comes in and the declared sequence number covers the
packet that was earlier retransmitted (just like all other packets). Therefore
if a packet was lost again, there must be some mechanism to retry the recovery.

The `SRTO_NAKREPORT` option controls the "periodic NAK report" done on the
receiver side. If on, there's a handler attached in the receiver/update worker
thread, which is triggered every SYN period (see `CUDT::checkTimers` and
following `CUDT::checkNAKTimer`) and this checks if there are packets still
not recovered. If such information is found, the new `UMSG_LOSSREPORT` control
packet is created with the use of all the ranges of lost packets and sent at
once this time. If it happened that the "repeated" loss report for particular
packet was sent too early (the network didn't have a chance to carry over a
retransmitted packet in such a short time), the blocking mechanism is used
through the `SRTO_RETRANSMITALGO` option.

Note for the FEC Packet Filter: it may work with "ARQ onreq" mode, that is, it
requests retransmission if the packet was lost and the FEC mechanism could not
recover it. For this kind of packets, direct loss reports are handled as
before, but NAKREPORT is not implemented. The reason for it is that the
structure of the loss list container (`m_pRcvLossList`) is such that it is
expected that the loss records are ordered by sequence numbers (so that two
ranges sticking together are merged in place). Unfortunately in case of
`SRT_ARQ_ONREQ` losses must be recorded as before (at the moment of detection),
but they should not be reported, until confirmed by the filter (that they are
not recoverable, so it uses ARQ as a fallback). By this reason they appear
often out of order and for adding them properly the loss list container wasn't
prepared. This then requires some more effort to implement.

NAKREPORT still works with FEC in case of setting "ARQ always", but this mode
is more for experiments than real use, as it is a mechanism of "double
certainty" recovery, comparable to broadcast groups - in this case it works
parallelly with FEC itself, so NAKREPORT is working in this mode.

On the receiver side (see `CUDT::checkRexmitTimer`) the party should wait until
packets are finally acknowledged by receiving `UMSG_ACK`, but in theory it may
happen that it stays in this state forever. There are two failsafe mechanisms
to avoid that:

1. Sender dropping: `SRTO_TLPKTDROP` option is set, the sender side may decide
that it's so late that the packet to be retransmitted will never make it on
time (assigned to delivery). Therefore the packet is not retransmitted even if
it was requested. Note that if the LOSSREPORT comes for a packet that was
already ACK-ed (points to a position in the buffer that is already in the
past), the sender side will respond with `UMSG_DROPREQ`.

2. The BLINDREXMIT mechanism, which works in two different modes: LATEREXMIT
and FASTREXMIT.

The BLINDREXMIT mechanism is simply a mechanism that requires sending again
every single packet between the last received ACK and the very last sent one.
The question is only, when it is decided to do so. The LATEREXMIT is the
original mechanism that existed in SRT for the use in file transmission and
FASTREXMIT was an attempt to do it a bit more efficiently for the live mode. As
the NAKREPORT mechanism was considered efficient enough, currently in live mode
when NAKREPORT and TLPKTDROP are set, FASTREXMIT is not in use. The BLINDREXMIT
is then triggered under the following conditions:

* LATEREXMIT is only used with FileCC. The RTO is triggered when some time has
passed since the last ACK from the receiver, while there is still some
unacknowledged data in the sender's buffer, and the loss list is empty at the
moment of RTO (nothing to retransmit yet).

* FASTREXMIT is only used with LiveCC. The RTO is triggered if the receiver is
not configured to send periodic NAK reports, when some time has passed since
the last ACK from the receiver, while there is still some unacknowledged data
in the sender's buffer.

In case the above conditions are met, the unacknowledged packets in the
sender's buffer will be added to the SND loss list and retransmitted.

So, the retransmission is scheduled if:

* there are packets in flight (`getFlightSpan() > 0`);

* in case of LATEREXMIT (File Mode): the sender loss list is empty
(the receiver didn't send any LOSSREPORT, or LOSSREPORT was lost on track).

* in case of FASTREXMIT (Live Mode): the RTO (`rtt_syn`) was triggered,
therefore schedule unacknowledged packets for retransmission regardless of the
loss list emptiness.


## Prioritization

The overall rule in SRT (since UDP codebase) was that the retransmission
candidate is always treated with higher priority than regular (unique) packets.
This isn't always wanted in live mode with TLPKTDROP turned on because the
large retransmission requests may block regular packets from being sent and
finally they can eat up all the latency advantage even up to sending packets
way later than it is required for it to be delivered to the application.

The method `CUDT::isRegularSendingPriority` checks if the regular packet should
be preferred over retransmitted packet (even if it increases the probability of
getting the lost packet never recovered).

The following options should be set to take this kind of priority:

* `SRTO_TLPKTDROP` = true
* `SRTO_MESSAGEAPI` = true

NOTE: We ignore `SRTO_TSBPDMODE` because it can't be set in stream mode,
although this theoretically enables this for plain message mode with
TLPKTDROP.

The current solution is simple - the regular packet takes precedence over a
retransmitted packet, if there's at least one packet not yet transmitted first
time.

This probably isn't the most wanted solution, some more elaborate condition
might be better in some situations. For example, it should be acceptable
that a packet that has very little time to be recovered is sent before a
regular packet that has still STT + Latency time to deliver. The regular
packets should still be favored, but not necessarily at the expense of
dismissing a recovery chance that wouldn't endanger the delivery of a regular
packet. Criteria might be various, for example, the number of scheduled
packets and their late delivery time might be taken into account.

PROPOSAL:

A specific time-amount proportion should be applied for cases like this so
that retransmission candidates with very little left time can be preferred
at the small expense of the latency remaining for the new packets, but if
there's a situation with a large portion of retransmission candidates that
can overflow the link and block awaiting packets from timely delivery, the
prioritized should be still unique packets.

