# Sender buffer

The sender buffer is organized with the cells kept by a container using
the `std::deque` type with additional memory manager. The logics of this
container maintain keeping the packets themselves and the loss information,
but there's no separation of the newly scheduled packets and those kept for
the sake of retransmission.


## The cell block

The `CSndBlock` class represents the single cell in the container and keeps
the scheduled payload together with various characteristic data. Basing on
this there will be created full UDP packets sent over the UDP socket, although
the payload is using a different buffer than the header.

Note that the `PH_MSGNO` field in the header occupies only several bits for
the message number itself, others are allocated for special flags.


## The main container wrapper: `SndPktArray`

The container keeps the blocks and also additional characteristic data for
the container as a whole. The actual type of the container values is defined
as `SndPktArray::Packet`, which derives from `CSndBlock` and adds several
fields for the sake of other types of mechanisms:

* `m_iBusy`: the busy counter; if not 0, then the acknowledgement action that
is about to revoke packets from the sender buffer must stop on this packet
and retry only when it is 0 again. See `SndPacket` for further explanations

* `m_tsNextRexmitTime` : time intended for the next retransmission
* `m_iLossLength` : how many packets since this are retransmit-eligible
* `m_iNextLossGroupOffset` : points to the next index of retransmit-eligible

These fields will be explained below for the "Sender Loss Structure".

For the update after scheduling a packet for retransmission, there's a function
to check if the time has come to retransmit a packet,
`updated_rexmit_time_passed`. This function must ensure that
`m_tsNextRexmitTime`, if set, is distant by at least `miniv` towards
`m_tsRexmitTime`; if not, `m_tsNextRexmitTime` will be updated to the minimum
acceptable value, then returns:
- false: the next rexmit time is in the future
- true: the next rexmit time is in the past, or we don't care (if miniv is zero)

Further fields in `SndPktArray`:

* `m_Storage`: memory storage. This is a specialized allocator, which keeps
deallocated packets for the need of future allocations in the sensible number
of spare buffers. This keeps buffers for payload that are assigned to elements
kept in the actual container

* `m_PktQueue`: the packet container, managed internally

* `m_iCachedSize`: the container size updated with every modification, kept
to avoid mutex locking when the size reading is requested

* `m_iFirstRexmit`, `m_iLastRexmit`: hooks for the retransmission scheduling
subcontainer, see below.

* `m_iLossLengthCache`: total number of retransmission-scheduled packets


## Retransmission scheduling subcontainer

This is a singly linked list with hook pointer to the head and tail of the
list, where pointers are the indexes into the `m_PktQueue` container. As
index values change as the container gets modified (actually only on
revocation), the main hooks must be updated, but then those in the container
elements themselves are expressed in relative values, so they don't change
provided that packets in the container are always ordered in their sequence
numbers.

The `m_iFirstRexmit` and `m_iLastRexmit` fields in `SndPktArray` keep the index
of the first and last retransmission request record. If there are no such
records, both should be -1. The last one is to speed up place search for
inserting newly incoming loss reports.

The linked list is organized in records (or groups otherwise), that is, what is
being pointed at first is the packet to be retransmitted, but any directly
following packets may belong to a single group. All the required information is
only written into the very first such packet, and this is:

* `m_iLossLength`: This value is at least 1 in the packet being pointed as the
group head of the scheduled packets - in all other packets it's always 0. The
value designates how many packets directly following this one belong to the
group of scheduled packets.

* `m_iNextLossGroupOffset`: It's a relative value, that is the difference
between the index of the current packet and index of the packet beginning the
next group. If this record is the last group, this value is 0.

The fact that packets are in the loss records doesn't yet mean they will be
retransmitted - this is decided by the key field `m_tsNextRexmitTime`. It
defines the time of the next retransmission. If zero-time, this packet is not
to be retransmitted. If future-time, this packet should be skipped when
looking for the packets for retransmission, but the time remains unchanged.
This will be set to zero-time right after picking up for retransmission.

The possibility to use this field is important for performance when a single
packet must be removed quickly from the list - it's just cleared this field
and then it will be skipped when looking for the next candidate, and if all
preceding packets were removed from the loss retransmission, only then the
whole list structure will be updated and these packets removed from the list.
This happens inside `extractFirstLoss`. This function looks for the first
found loss record, but skips all packets with `m_tsNextRexmitTime` with
nonzero value, and those that might be set as such during the check, as
well as those having this value in the future - although if this kind of
packet was skipped, revocation index will stop here. Then all packets up to the
revocation index will be revoked from the loss list.

Packets that are requested retransmission are first set `m_tsNextRexmitTime`
to the value of the time that must be in the past to be retransmitted.

Any insertion also updates the following:

- `m_iFirstRexmit` is set to the index of the first rexmit packet, or unchanged
if the inserted sequence pair was not the very first

- `m_iLastRexmit` is set to the first packet of the group, if this was the very
last insertion (if the current inserion was past the previous last one)

- `Packet::m_iLossLength` is the number of consecutive packets since
this packet that belong to the retransmission-requested. Note that
also only this packet contains a nonzero `m_tsNextRexmitTime` field.

- `Packet::m_iNextLossGroupOffset` is set to 0 if this was inserted as
the last one, or to the offset between this packet and the nearest packet
beginning the next retransmission group

Example:

```
       *   *               *   *   *               *   *
  [00][01][02][03][04][05][06][07][08][09][10][11][12][13][14][15]
       |                                           |
      /                                           /
m_iFirstRexmit = 1                               /
m_iLastRexmit = 12 ------------------------------

[01]:
   m_iLossLength = 2
   m_iNextLossGroupOffset = 5 (6 - 1)

[06]:
   m_iLossLength = 3
   m_iNextLossGroupOffset = 6 (12 - 6)

[12]:
   m_iLossLength = 2
   m_iNextLossGroupOffset = 0 (last group)

[any other]:
   m_iLossLength = 0
   m_iNextLossGroupOffset = 0

NOTE:
m_PktQueue[m_iLastRexmit].m_iNextLossGroupOffset == 0

```

Revocation of the packets from the sender buffer updates the fields:

- If the series of packets in a loss record are split in half, the first packet
that survives the revocation is updated: the `m_iLossLength` is set to the new
size of the group, `m_iFirstRexmit` is set to 0.

- If the whole series are revoked, only the `m_iFirstRexmit` field is updated
to the new beginning.

- If all packets with retransmission requests are effectively removed,
both `m_iFirstRexmit` and `m_iLastRexmit` fields are set to -1.

- No matter if any groups were removed or not (but unless there are no
loss records), `m_iFirstRexmit` and `m_iLastRexmit` fields are being updated by
decreasing with the number of revoked packets, if they are not set the new
value.

Expiration of a packet (per TTL, for example) causes `m_tsNextRexmitTime`
to be reset to zero, but no other action is undertaken. The packet is
still in the retransmission request record, just won't be retransmitted.

Popping a loss does the following:

- The first packet group, pointed by `m_iFirstRexmit`, is checked and removed

- Removal means that the next packet is taken as the first one:

   - if `m_iLossLength` == 1, take the packet distant by
`m_iNextLossGroupOffset`

   - if `m_iLossLength` > 1, take the packet distant by 1, set it the
     values of this packet's `m_iLossLength` and `m_iNextLossGroupOffset`
     decreased by 1

   - the `m_iFirstRexmit` index is updated to point to this new packet

- If the packet at this position has `m_tsNextRexmitTime` zero, this
is not reported as retransmission-eligible, but still removed,
that is, after removal the whole procedure starts over

- If the search with removed expired retransmission packets reaches
a packet with `m_iNextLossGroupOffset == 0`, finally "no rexmit request"
state is reported, same as when `m_iFirstRexmit == -1`.

- If a removal resulted in removing the last record, whether a valid
retransmission request or not, both `m_iFirstRexmit` and `m_iLastRexmit`
are set to -1.


## Buffer zones

The sender buffer serves for two purposes:

* Schedule packets to be sent (inter-thread passing from API to sender thread)
* Keeping old sent packets for the sake of prospective retransmission

By sequence numbers these two ranges could be determined in the sender
buffer as "unique" and "historic" respectively.

The sender buffer, however, doesn't keep this information. It does keep
the information about the oldest stored sequence number, but all packets
stored there have equal status as for the sender loop. It's the socket
that should keep this information.

The `extractUniquePacket` method is indeed intended to extract the unique
packet, but which packet is unique, this information should be provided
by the caller. One of the parameters is required to contain the current
sequence number of the last packet that for given socket was considered
unique, and this function attempts to shift this value by 1 and take the
packet at that sequence number. This need not go smoothly due to possible
TTL-expiration of the packets in the message mode, including a possibility
to ride up to the end of filled buffer and find nothing. Still, during this
TTL-expiration skipping, the last sent sequence number is still updated,
so the caller, after returning from this call, should treat this as a good
deal and the sequence number that should be considered as of the last
packet that was sent as unique, even if nothing was effectively extracted.





