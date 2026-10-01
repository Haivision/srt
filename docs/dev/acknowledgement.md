# Acknowledgement

Acknowledgement - or ACK for short - is a mechanism of a feedback provided
by the receiver to the sender containing the basic information about the
received status, mainly concerning the sequence number pointing the position
in the stream up to where everything has been correctly received, or at least
considered so.


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

## Sending ACK and conditions

ACK is tried to be sent on the SYN timer, and the following conditions are
checked:

* ACK time has come, that is, the value of `m_tsNextACKTime` is earlier than
the current time. 

* The Congestion Controller defines the maximum packets to elapse since the
last ACK and this number has been exceeded (recorded in `m_iPktCount`). Note
that none of the current CCs ("live" and "file") defines that value.

* The transfer rate is so high that the number of packets have reached the
value of `SelfClockInterval * LightACKCount` before the time has come according
to `m_tsNextACKTime`. In this case a "lite ACK" is sent (see the use of
`SEND_LITE_ACK`), which doesn't contain statistical data and nothing more than
just the ACK number. The "fat ACK" packets (with the complete data defined for
the `UMSG_ACK`) will be still sent normally according to the timely rules.


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

