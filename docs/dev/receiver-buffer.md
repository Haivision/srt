# Receiver buffer

The receiver buffer in SRT is a circular buffer keeping pointed pieces of memory
containing the data in their desired order, split into packets the way they were
received.

As a circular buffer, it is based on a solid array
with fixed size, but as initial portion of the buffer gets decommissioned,
the position of the first cell may be shifted into the middle of the array.
The logical position is mapped to the physical index by shifting the position
and wrapping around the container size; the index of the logical position 0 -
"first cell" - is set to `m_iStartPos` field. Most of the other meaningful
positions are kept as logical positions, represented as "offset", and so are
marked the field names:

* ...Off : carries an offset - a relative position towards the first cell
* ...Pos : carries the exact (physical) index to the array

Every cell of the container represents a sequence number of the packet. The
sequence number of the first cell is kept in the `m_iStartSeqNo` field. The
distance between this sequence and incoming sequence is turned into an "offset"
(logical position) and this is then turned into the physical index basing
on `m_iStartPos` and wrapping around the size. If "offset" is outside the
buffer capacity (or negative), the packet is discarded.

The packets come in as memory blocks already filled with data, which are only
pinned into the cells of the buffer. How the memory management is organized you
can see in a [separate document](receiver-unit-pool.md).


## Working modes

On the other end packets are being extracted using various ways depending on
the mode:

### live mode

Only one packet at a time, usually the one at position 0 and only if the play
time has come. Play time (known as TSBPD time) is the alleged sending time
recorded in the packet's timestamp converted to local time with the addition
of latency and drift correction. The packet is ready to read if the play time
is in the past. If dropping is enabled, the cell at position 0 is empty, and
there is at least one packet in the buffer that is ready to read, empty cells
are dropped. Otherwise reading will not be available until the packet at
position 0 is available.

### stow mode (CONCEPT)

This is identical as live mode, except that the play time is only given as a
reference and if there is a valid packet at cell 0, it is always readable.
Dropping rules remain intact - a next-to-gap packet may still be only delivered
when its play time has come.

### message mode

A single message may consist of one or more packets and extraction is possible
only if the whole message is reassembled. Normally only the first found message
in the buffer can be extracted, but there is also allowed out-of-order reading
for messages that have the `inorder` flag cleared - in this case first
reassembled message of that type is allowed to be extracted, even if it follows
an earlier incomplete message; packets for this message are then marked as
"read" so that this message will be skipped when preceding messages are
extracted.

The messages can be also set expiration time; if this time is exceeded, the
message will not be sent, and if it is incomplete on the receiver side, the
whole message will be discarded. This happens through sending the drop request
by the sender side, which means that packets for that message will not be
sent anymore; in result the whole message is removed from the buffer.

### stream mode

Extracted are as much data as available and fit in the given buffer. Available
are all data from the packet at cell 0 up to the first gap or the last packet
ever received (whichever comes first). In case when the buffer is shorter than
the available data and reading would have to stop in the middle of a single
packet, the notch marker is used to mark the position for the next reading.


## Terminology

The buffer may or may not deliver data basing on various conditions, mainly
represented by the regions that embrace the range of buffer cells.

The buffer has a fixed capacity, but not all cells need to be in use at the
moment.

* BUSY REGION: It starts with the first (logical) cell and ends with the latest
cell for which a packet has ever been received; naturally empty cells may
happen to be inside this region in case when the packet was lost.

* SPARE REGION: the region of the buffer with existing cells, but following
the last cell of the Busy Region until the end of capacity.

Inside the BUSY REGION there are regions that define the rules for packet
delivery, where the most important is the "first cell", that is the cell
at logical index 0 (physical index == `m_iStartPos`):

* ICR: Initial Contiguous Region: This is present if the first cell contains
a valid packet and it continues until the first gap. If there are no gaps
(no packets were lost or nothing has arrived out of order), ICR is the same
as Busy Region. If the first cell is empty, so is ICR.

* FIRST GAP: It starts with the empty cell that follows the last cell of ICR,
if ICR is shorter than the Busy Region (including empty), and continues with
consecutive empty cells until the first following filled cell (Drop Target).

* SCRAP REGION: Region with possibly filled or empty cells. It starts with
the first gap and ends with the end of Busy Region. NOTE that in the scrap
region the very first cell is empty and the very last cell is filled.

* DROP TARGET: a filled cell that immediately follows the First Gap. May
be not present in case when there's no Scrap Region.

## Design chart and explanations

```

           |      BUSY REGION                      | 
           |           |                           |           |
           |    ICR    |  SCRAP REGION             | SPARE REGION...->
   ......->|           |                           |           |
           |           | /FIRST-GAP                |           |
   |<------------------- m_entries.size() ---------------------------->|
   |       |<------------ m_iMaxPosOff ----------->|           |
   |       |           |                           |   |       |
   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
   | 0 | 0 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |...| 0 | m_entries
   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
           | \ FIRST   |   |                   |
           |   CELL    |   |                   \__last pkt received
           |<------------->| m_iDropOff        |
           |           |                       |
           |<--------->| m_iEndOff             |
           |
             \___ m_iStartPos: first packet position in the buffer

```

## The general buffer's fields

Beside the `m_entries` array, the buffer contains the fields operating the cicrular
buffer:

* `m_iStartPos`: Physical index of the first logical cell
* `m_iMaxPosOff`: past-the-end of the BUSY REGION

And the fields for specific regions:

* `m_iEndOff`: offset to the past-the-end of ICR. This points always to an
empty cell. If this value is 0, the buffer has an empty ICR. If this value is
equal to `m_iMaxPosOff`, the whole buffer is contiguous (no FIRST GAP or DROP
REGION)

* `m_iDropOff`: offset to a packet available for retrieval after a drop, that
is, DROP TARGET. If there is no SCRAP REGION, this value is 0, otherwise it
points to a valid packet following the FIRST GAP.

* `m_iFirstNonreadPos`: physical position to either the first empty cell, or
the first cell with `PB_FIRST` boundary flag (message mode only)

The DROP TARGET is determined in order to quickly perform the dropping
operation. In case when the last packet has been extracted from the ICR, the
cell 0 is empty. When the decision for an extraction-over-consistency is
undertaken (live mode with too-late-packet-drop enabled, when the play time
comes for the packet at the DROP TARGET), the whole FIRST GAP is removed
from the buffer so the beginning of the buffer shifts to the DROP TARGET, which
becomes the first of ICR this way, and then the valid packet at cell 0 is
extracted.


## Entries

Every cell in `m_entries` array is an object of type `Entry`, which contains:

  * pUnit: handle to the unit (payload memory), or empty handle
  * muxID: multiplexer ID that provided the unit (groups only)
  * status:
     * `EntryState_Empty`: No packet was ever received here (pUnit is empty)
     * `EntryState_Avail`: The packet is ready for reading
     * `EntryState_Read`: The packet was extracted (out of order)
     * `EntryState_Drop`: The packet was requested to drop

Status values used normally (live and stream mode):

* `EntryState_Empty`: No packet here (pUnit == NULL). This is the default
state before getting a packet and the state of the units in the spare region

* `EntryState_Avail`: The packet is ready for reading. It is set just after
the position has been confirmed and the unit pointer written into `pUnit`

In message mode additionally two other states are possible:

* `EntryState_Read`: The packet was prematurely extracted by out-of-order
reading. The space must be still occupied because it's in the scrap region
following some fragmented message, which still waits for reassembly (or
dropping)

* `EntryState_Drop`: The message was requested to be dropped. This usually
happens when the timeout for the message passed and the message is still
not reassembled, or when the message was revoked from the sender buffer
by the peer, so the peer has sent the `UMSG_DROPREQ`, making the packets
no longer recoverable. If this happens, all messages containing dropped
packets are marked Drop state and will no longer be delivered. This is
only happening in case when the drop request referred to cells that follow
any message still waiting for reassembly; if the drop request referred
to a message at the start of the buffer, these cells are simply removed.


## Operational rules

Initially:
```
       m_iStartPos = 0
       m_iEndOff = 0
       m_iDropOff = 0
```

When a packet has arrived, then depending on where it landed:

1. Position: next to the last received one and newest

```
    m_iStartPos unchanged.
    m_iEndOff shifted by 1
    m_iDropOff unchanged
```

2. Position: after a loss, newest.

```
    m_iStartPos unchanged.
    m_iEndOff unchanged.
    m_iDropOff:
      - set to this packet's position, if m_iDropOff == 0
      - otherwise unchanged
```

3. Position: after a loss, but belated (retransmitted) -- not equal to `m_iEndPos`

```
   m_iStartPos unchanged.
   m_iEndOff unchanged.
   m_iDropOff: set to this packet's position if:
      - m_iDropOff == 0
      - DROP TARGET %> this sequence (still a drop, but updated)
      - otherwise unchanged
```

4. Position: after a loss, sealing -- seq equal to position of `m_iEndOff`
 
``` 
   m_iStartPos unchanged.
   m_iEndOff: set to the first found empty cell since the current position (up to m_iMaxPosOff)
   m_iDropOff:
     - if m_iEndOff == m_iMaxPosOff, set to 0
     - otherwise search for the first filled cell starting from m_iEndOff (up to m_iMaxPosOff)
```

NOTE:

If there are no "after gap" packets, then `m_iMaxPosOff` == `m_iEndOff`. If
there is one existing packet, then one loss, then one filled packet:

```
    [*] [ ] [*] {-}
     ^   ^   ^   ^
start   /   /   /
m_iEndOff  /   /
m_iDropOff^   /
m_iMaxPosOff-^

```
You have:
* `m_iEndOff` = 1
* `m_iDropOff` = 2
* `m_iMaxPosOff` = 3

The ICR contains 1 packet at position 0, following SCRAP REGION for
positions 1 and 2, and so ends the BUSY REGION.

Cases for inserting a packet:

Let's say we have the following possibilities in a general scheme:


```
                [D]   [C]             [B]                   [A] (insertion cases)
 | (start) --- (end) ===[gap]=== (after-loss) ... (max-pos) |
```

See the `CRcvBuffer::updatePosInfo` method for detailed implementation.

### WHEN INSERTING A NEW PACKET:

If the incoming sequence maps to newpktpos that is:

* newpktpos <% (start) : discard the packet and exit
* newpktpos %> (size)  : report discrepancy, discard and exit
* newpktpos %> (start) and:
   * EXISTS: discard and exit (NOTE: could be also < (end))

* seq == `m_iMaxPosOff` [A]
```
      --> INC m_iMaxPosOff
      * m_iEndOff == previous m_iMaxPosOff
           * previous m_iMaxPosOff + 1 == m_iMaxPosOff
               --> m_iEndOff = m_iMaxPosOff
               --> m_iDropOff == 0
           * otherwise (means the new packet caused a gap)
               --> m_iEndOff REMAINS UNCHANGED
               --> m_iDropOff = m_iMaxPosOff - 1
```
COMMENTARY:

If this above condition isn't satisfied, then there are gaps, first at
`m_iEndOff`, and `m_iDropOff` is at furthest equal to `m_iMaxPosOff`-1. The
inserted packet is outside both the contiguous region and the following
Scrapped Region, so no updates on `m_iEndOff` and `m_iDropOff` are necessary.

NOTE:

SINCE THIS PLACE on, seq cannot be a sequence of an existing packet,
which means that earliest newpktpos is at `m_iEndOff`, up to == `m_iMaxPosOff` - 2.

```
   * otherwise (newpktpos <% max-pos):
   [D]* newpktpos offset == m_iEndOff:
            --> (search FIRST GAP and FIRST AFTER-GAP)
            --> m_iEndOff: increase until reaching m_iMaxPosOff or an empty cell
            * m_iEndOff < m_iMaxPosOff:
                --> m_iDropOff = first FILLED packet since m_iEndOff + 1
            * otherwise:
                --> m_iDropOff = 0
   [B]* newpktpos offset > m_iDropOff
            --> store, but do not update anything
   [C]* otherwise (newpktpos offset > m_iEndOff AND < m_iDropOff) 
            --> store
            --> set m_iDropOff = newpktpos offset
```

COMMENTARY: 

It is guaratneed that between `m_iEndOff` and `m_iDropOff` there is only a gap
(series of empty cells). So wherever this packet lands, if it's next to
`m_iEndOff` and before `m_iDropOff` it will be the only packet that violates the
gap, hence this can be the only drop pos preceding the previous `m_iDropOff`.

The information returned to the caller should contain:

1. Whether adding to the buffer was successful.

2. Whether the "freshest" retrievable packet has been changed, that is:
   * in live mode, a newly added packet has earlier delivery time than one before
   * in stream mode, the newly added packet was at cell[0]
   * in message mode, if the newly added packet has:
      * completed the very first message
      * completed any message further than first that has out-of-order flag

The information about a changed packet is important for the caller in live mode
in order to notify the TSBPD thread.


### WHEN CHECKING A PACKET (for readability)

1. Check the position at `m_iStartPos`. If there is a packet, return info at its
position. Note that this sole packet means readability unconditionally only in
the stream mode. In live mode it must have also the play time in the past, and
for the message mode only if it has `PB_SOLO` boundary flag, or otherwise it
must be followed by packets with the same message number up to the last of them
with `PB_LAST` boundary flag.

2. If position on `m_iStartPos` is empty, get the value of `m_iDropOff`.

NOTE THAT:

  * 0 is the trap representation for both `m_iEndOff` and `m_iDropOff`;
    they are both 0 in case of an empty buffer, although this is easiest
    to check if `m_iMaxPosOff` == 0
  * if there is a packet in the buffer, but the first cell is empty,
    then `m_iDropOff` points to this packet, while `m_iEndOff` == 0.
    Check then `m_iEndOff` == 0 to recognize it, and if then
    `m_iDropOff` != 0, you can read with dropping.
  * If cell[0] is valid, there could be only at worst cell[1] empty
    and `m_iDropOff` == 2. Note that `m_iDropOff` is not always updated
    in case when cell[0] is filled, so you should not check that in this case

3. In case of time-based checking for live mode, return empty packet info,
if this packet's time is later than given time.


### WHEN EXTRACTING A PACKET

1. Direct extraction is only possible if there is a packet at cell[0].

2. If there's no packet at cell[0], the application may request to
   drop up to the given packet, or drop the whole message up to
   the beginning of the next message.

3. In message mode, extraction can only extract a full message, so
   if there's no full message ready, nothing is extracted (even if the
   cell[0] is filled).

4. When the extraction region is defined, the `m_iStartPos` is shifted
   by the number of extracted packets.

5. After updating of `m_iStartPos`, if `m_iEndOff` would point to 0 or
   before the first position, it must be set to 0 and then shifted to the
   first found gap or `m_iMaxPosOff`.

6. `m_iDropOff` must be always updated. If `m_iEndOff` == `m_iMaxPosOff`,
   `m_iDropOff` is set to 0. Otherwise start from `m_iEndOff`
   and search a filled packet up to `m_iMaxPosOff`.

7. NOTE: all `*Off` fields are offsets, hence they must be all set anew after
   update for `m_iStartPos`.


