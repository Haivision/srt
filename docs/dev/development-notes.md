# Introduction

These are loosely placed notes with some interesting information about the
development solutions and design choices used in various parts in SRT, mainly
extracted from long comments.


## ACK and groups

In case of groups the ACK is sent:

* in case of multilink-type groups (Broadcast and potentially Balancing),
  ACK is being sent over every connection, just like with multiple sockets
* in case of Backup-type group, ACK is sent only over the currently active links

1. Special handling for Backup groups

In the case of a Backup-type group, IDLE links are considered to never
sending any packets, hence nothing is to be acknowledged. The problem is
that normally the buffering activities were interconnected with ACK-ing,
however in case of group reception the common receiver buffering causes
that the fact of having received a packet IN THE BUFFER doesn't simultaneously
mean that the packet was received OVER THIS LINK.

So, first, check if this link was IDLE. For IDLE links, ACKs should not
be sent at all.

There is one more small problem though. When a link is being silenced,
then it should turn from RUNNING to IDLE, however the recognition of
this fact is only possible at the moment when the first KEEPALIVE arrives
after the data stop coming. The problem of the wrong ACK could occur
just as well during this period.

Therefore the best way is, beside rejecting ACK on non-RUNNING links, in case
of RUNNING state, additionally there should be checked if the last sent
sequence exceeds the current last ACK received. If not, also no ACK should be
sent, even if the noncontiguous sequence was shifted.

The check must be done regardless if anything has arrived over THIS LINK since
the last ACK. This is still being done for the backup group only because only
in case of this group there can happen an immediate stop of the transmission on
one of the links ("silencing"), of which the receiver has no idea. In multilink
type groups you can safely send ACK basing on the latest contiguous sequence in
the buffer because all links are supposed to be active and deliver packets.

Note also that the IDLE state on the receiver side is only notified upon
reception of KEEPALIVE. Until then it's simply a link that doesn't deliver
data.

```
Consider adding a method of recognizing the IDLE links by having the
number of packets received from another link exceed some predefined number or
time, while over the link in question nothing was received.
```

It would be nice to do a sanity check if this sequence isn't in the past for
the buffer, but there's no point in doing it by two reasons:

1. In this implementation the alleged ACK sequence is taken exclusively from
the buffer, so there's no possibility that it took a sequence of the loss being
in the past towards the buffer that was incorrectly removed, which was a
problem in the past. This implementation doesn't look into the loss record at
all.

2. Taking the start sequence of the buffer requires checking it separately for
the socket and for the group, use separate mutexes etc., and just to do a
sanity check it's not worth a shot.

## Common receiver buffer: removed socket-buffer synchronization

The initial implementation was using multiple sockets, each one with their own
buffer, and the application was being fed with data read from appropriate buffer,
while duplicates from other parallelly received links were discarded. This required
also a specific procedure for TSBPD.

This was done only for "old bonding" using the app-reader procedure. In the
new bonding all buffer reception and reading ready state update happen
exclusively inside the group.

Note that this is only true in TSBPD mode. In file mode, the ACK signoff is
still in force, maybe not for the buffer, but still for the read state update,
where the read declaration is done only when ACK has moved the ACK pointer some
sequences in forward. The problem with properly implementing this is that
reading happens from the group and it is done directly from the group buffer
(without any involvement of the socket), but ACK action is a timer-loop action
executed by a socket. These activities happen on two different timers and on
two different moments, therefore likely it must be implemented somehow in the
group.

Used code:

```
       if (group_read_seq != SRT_SEQNO_NONE && m_parent->m_GroupOf)
       {
           // See above explanation for double-checking
           SharedLock glock (uglobal().m_GlobControlLock);

           if (m_parent->m_GroupOf)
           {
               // The current "APP reader" needs to simply decide as to whether
               // the next CUDTGroup::recv() call should return with no blocking or not.
               // When the group is read-ready, it should update its pollers as it sees fit.
               m_parent->m_GroupOf->updateReadState(m_SocketID, group_read_seq);
           }
       }
```

## Receiving ACK - safety considerations

For safety reasons, we can't take the ACK sequence as a good deal.
This value must be verified and checked if:

1. The value isn't newer than the last sent, although:
   - For backup groups, we can accept ACKs that exceed the sent packets,
     however ACKs are NOT EXPECTED on an idle link. This means if such
     ACK comes we treat it as an IPE, but as fallback we shift the ACK
     position not more than to the last sent packet in the group data
   - For multilink-type groups, an ACK is allowed to exceed
     the current sent sequence for a link, but still must not exceed the
     latest packet sent for the group. Group-excessive ACKs should break
     the connection, but ACKs that only exceed the link latest packet
     should reset the latest sent sequence and clear the sender buffer
2. The value isn't older than the newest ACK. Such packets may happen,
   but due to some random network condition and therefore can't be from
   upside treated as a rogue protocol case, only silently skipped.
3. The value doesn't shift ACK by more than the current size of the
   sender buffer, unless the buffer is empty.
4. (Proposed) The value doesn't shift ACK by more than 4 times the total
   size of the sender buffer, if the buffer is empty, in which case the link
   should be immediately broken.

IMPORTANT: if the sender buffer is empty, then the base sequence number for it
can be set to whatever value. In the old UDT implementation the sender buffer
also didn't manage sequence numbers at all, they were set to packets only when
they were sent. In SRT with the introduction of groups the management of
sequence numbers was necessary for the sender buffer because when a packet is
going to be sent over multiple links at a time (broadcast example), then the
packet with the same payload, to be identified as the same packet against the
receiver application, must go also with the same sequence number - hence
sequence numbers must be dictated at the scheduling time and also be ready to
override the existing sequence number values if they collide with those.

## TSBPD synchronization on packet arrival

This is mainly happening in the `CUDT::acquireDataPacket` function, which is
called when the incoming UDP packet has been recognized as containing a payload
for particular socket. The packet should be eventually inserted into the buffer,
but this need not be done, depending on various conditions.

In TSBPD mode there's also a special thread, which marks packets ready to
retrieve by application; because of that it must sleep until the time comes
for marking the packet ready. The definition of "coming time" is complicated,
through. "Sleeping" is done on a CV because in several situations it must be
notified in order to be prematurely woken up:

1. There are no packets in the buffer at the moment. In that case the CV
is locked forever, so it must be signaled when the new packet is inserted
into the buffer - or simply when the socket is closed.

2. There are packets in the buffer, but there's an "initial gap" (the cell 0
contains no packet, and more empty cells may follow, but there is finally
somewhere a packet). In that case CV is locked timely until the play time
of that existing packet. Therefore the CV must be signaled if a newly
inserted packet is inserted before this earliest packet. Even if the play
time for that packet wouldn't come, TSBPD must simply refresh its state,
that is, either mark the packet ready, or fall asleep again, but this time
up to the time of the newly inserted packet.

3. There has been already one packet marked as ready for retrieval, but the
application hasn't responded for that yet. Therefore TSBPD sleeps forever
again, and it will have to be woken up by notifying CV when the application
finally extracts that packet, and after extraction there are no more playable
packets at the moment (that is, either there aren't any, so the CV is again
locked forever, or there is a packet with play time in the future, so the
CV will be locked timely).

One of the important part of this condition is the marker of the "CV locked
forever" state, which is `m_bTsbpdNeedsWakeup` field:

- `m_bTsbPdNeedsWakeup` is set by TSBPD thread and means that it wishes to be
  woken up on every received packet. Hence we signal always if a new packet was
  inserted.

- even if TSBPD doesn't wish to be woken up on every reception (because it
  sleeps until the play time of the next deliverable packet), it will be woken up
  when `next_tsbpd_avail` is set because it means this time is earlier than the
  time until which TSBPD sleeps, so it must be woken up prematurely. It might be
  more performant to simply update the sleeping end time of TSBPD, but there's no
  way to do it, so we simply wake TSBPD up and count on that it will update its
  sleeping settings.

**To Consider**: as `CUniqueSync` locks `m_RecvLock`, it means that the next
instruction gets run only when TSBPD falls asleep again. Might be a good idea
to record the TSBPD end sleeping time - as an alternative to
`m_bTsbPdNeedsWakeup` - and after locking a mutex check this time again and
compare it against `next_tsbpd_avail`; might be that if this difference is
smaller than "dirac" (could be hard to reliably compare this time, unless it's
set from this very value), there's no need to wake the TSBPD thread because it
will wake up on time requirement at the right time anyway.

**To Consider**: sleeping on a CV with time causes quite a big CPU usage. In
the previous attempts there was tried safety condition to never sleep forever
and use maximum 1 second sleep as a safety precaution. This had to be removed
(and the risky forever-sleeping, ensured by having always a condition to signal
the CV) because just this addition has caused increased CPU usage by 2%.
Therefore there might be some better method used, maybe based on some event
library, such as `libev`. The CV should still stay there, but it could be
just locking always forever so that it is notified when ready, or even simpler,
as this is only about making the socket ready in the epoll system, and release
the blocking-mode read through another CV, do this exactly thing directly through
the timer functionality.

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


## Discarding acknowledged packets

When the packet comes in and its sequence number is recognized, it is attempted
to be inserted into the buffer, at the position being an offset towards the
initial sequence of the buffer, but this attempt will not be made and the packet
will be discarded if:

* The position is negative (the sequence is in the past for the buffer)
* The sequence is in the acknowledged region of the buffer

There might be a controversy as to whether the packet should be discarded with
regard for the `SRTO_TLPKTDROP` option, which allows to drop packets if they
are not recovered on time, so rejecting a packet basing on the sequence number
seems to risk dropping a packet, which's cell is currently empty.

This will not happen because if we have a situation that there are any packets
in the acknowledged area, but they aren't retrieved, this area DOES NOT contain
any losses. So a packet in this area is at best a duplicate.

The mechanism of the actual dropping the packet is always based on the state,
where the very first cell of the buffer is empty, followed by any first valid
packet, and the play time for that packet has come - until then no packets are
dropped. Then, when TSBPD decides to drop these initial empty cells, we'll
have: (`m_iRcvLastAck <% buffer->getStartSeqNo()`) - and if so, the position in
the buffer will also result in being negative (so, handled by the first
condition).

The only case when the buffer position is positive, but packet's seq is earlier
than `m_iRcvLastAck`, is when the packet sequence is within the initial
contiguous area, which never contains losses, so discarding this packet does
not discard a loss coverage, even if this were past ACK.


## Receiver buffer

The receiver buffer is the object that should keep packets until they are
retrieved by the application. Simultaneously it should keep data packets in
their sending order, keep an eye on the losses and provide retransmission
information, and also, depending on configuration, handle dropping.

The new resolution for providing the memory for incoming packet is based on
the ownership passing and so-called "condensation": the original object that
gets the packet memory to be filled by the read packet from the system is
the multiplexer; they are organized in so-called units. The multiplexer picks
up a unit from its own container, fills in the packet through the call of the
`recvmsg` function, and then dispatches it. Some packets are dispatched in
place and the unit remains in the multiplexer - others, data packets are
passed to the receiver buffer for insertion (which need not succeed), possibly
together with additional packets that could be provided by the packet filter
(if installed). If insertion doesn't happen, the packet is returned to the
multiplexer that provided them, otherwise the unit with the packet inside
is transferred ownership to the receiver buffer.

Reading the data by the application happens with different conditions
depending on the configuration:

1. In case of stream reading, the reading is enabled at the time when the ACK
period comes (this triggers readable condition in epoll and CV used in blocking
mode) and available for retrieval are all data since the first cell of the
receiver buffer up to the first gap, or up to the end of the buffer if there
are no loss gaps. Any gap blocks the reading, even if this would block forever.
Extracted is only so much data, as fit in the buffer and are available; in case
of a smaller application's buffer the remaining data are kept in the buffer;
if the border is in the middle of a single packet, the notch is marked to point
the position of continued reading for the next call.

2. In case of message mode reading, a single message, that is, a consecutive
series of packets that have the same message number, can be retrieved, if it is
complete. Additionally, the `inorder` flag set to true (decided at the sender
side) states that if this message is the first one, it must be completed and
delivered before any other message with inorder flag set is delivered. If this
flag is not set, and a message with that flag unset is reassembled earlier than
any other message, which's part is in the buffer, it is allowed to be extracted
by the application out of order. In that case the message remains in the buffer
with only a special flag marking the already-retrieved status; as then earlier
messages are finally retrieved, units carrying this message will be
decommissioned right after decommission of packets carrying earlier messages.

3. In case of live mode reading (`SRTO_TSBPDMODE` is true), the following rules
apply:

a. The application is only allowed to read exactly one whole packet at a time
(the `SRTO_PAYLOADSIZE` option defines the minimum buffer size, even if the
packet happens to be smaller).

b. The TSBPD mode means that the packet is only allowed to be read when the
play time comes, and until then it stays in the buffer. The play time is
defined as the ETS (expected arrival time = first packet's arrival time plus
the difference between timestamps of this and the first packet) plus latency
plus drift.

c. If `SRTO_TLPKTDROP` is set (default in live mode), the play time counts
also as the time to drop packets that haven't been recovered and remain as
lost. Dropping is done exclusively when the packet at the very first cell
is empty and the first packet following the initial gap has the play time
already in the past.

For that reason, the buffer has appropriate offset-pointers that allow
tracking the initial gap and the drop point.

The internal design of the buffer is described in the
[separate document](receiver-buffer.md).



