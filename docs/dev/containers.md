Containers
==========

Receiver buffer
---------------

The receiver buffer is based on the circular buffer with appropriate
enhancements.

The circular buffer contains Entry internal type, which entails the status
and possibly the Unit containing the data. Units are supplied by a multiplexer
and placed by pointer here.

Every time a packet comes in over the connection, which may be a control or
data packet, it's written into the buffer contained in a Unit. If such a unit
is filled with the control data, the control command is handled and the unit is
reused. If it contained the data, this is passed to the receiver buffer.  The
position in the buffer is determined by the sequence number of the incoming
packet. The position is validated first, and if it turns out to be not filled
in before and within the expected range of arriving sequence numbers, the unit
filled during extraction of the data from the network is placed by pointer
into the appropriate position in the buffer. A unit that has been pinned into
the buffer is marked busy, and after extraction is marked free.

On the other end packets are being extracted using various ways depending on
the mode:

* live mode: always one packet at a time at position 0. If dropping is enabled,
  the empty positions are just skipped until the readable packet is reached.

* message mode: extraction is possible only if the whole message is
  reassembled, although it may happen that there is available a complete
  message, just not at the beginning of the buffer. If this message has
  the `inorder` flag clear, it is allowed to be delivered even if it would
  mean an out-of-order delivery. If this happens, all packets of this message
  are marked as Read so that if the earlier message is finally completed,
  after extracting this message the read messages directly following it
  are also removed from the buffer

* stream mode: extracted are as much packets from the ICR region as available
  and fit in the given buffer.

This is a rough schema of the receiver buffer:

* ICR = Initial Contiguous Region: all cells here contain valid packets
* SCRAP REGION: Region with possibly filled or empty cells
     - NOTE: in scrap region, the first cell is empty and the last one filled.
* SPARE REGION: Region without packets

```

          |      BUSY REGION                      | 
          |           |                           |           |
          |    ICR    |  SCRAP REGION             | SPARE REGION...->
  ......->|           |                           |           |
          |             /FIRST-GAP                |           |
  |<------------------- hsize() ---------------------------->|
  |       |<------------ m_iMaxPosOff ----------->|           |
  |       |           |                           |   |       |
  +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
  | 0 | 0 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |...| 0 | m_pUnit[]
  +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
          |           |   |                   |
          |           |   |                   \__last pkt received
          |<------------->| m_iDropOff        |
          |           |                       |
          |<--------->| m_iEndOff             |
          |
          \___ m_iStartPos: first packet position in the buffer

```

Container value type is ReceiverBufferBase::Entry, which contains two
fields:

* `pUnit`: points to the Unit, or NULL if empty
* `status`: the cell's status

Status values used normally (live and stream mode):

* `EntryState_Empty`: No packet here (pUnit == NULL). This is the default
  state before getting a packet and the state of the units in the spare region

* `EntryState_Avail`: The packet is ready for reading. It is set just after
  the position has been confirmed and the unit pointer written into `pUnit`

In message mode additionally two other states are possible:

* `EntryState_Read`: The packet was prematurely extracted by out-of-order
  reading. The space must be still occupied because it's in the scrap region
  following some fragmented message, which still waits for reassembly

* `EntryState_Drop`: The message was requested to be dropped. This usually
  happens when the timeout for the message passed and the message is still
  not reassembled, or when the message was revoked from the sender buffer
  by the peer, so the peer has sent the `UMSG_DROPREQ`, making the packets
  no longer recoverable. If this happens, all messages containing dropped
  packets are marked Drop state and will no longer be delivered

Position fields from the CiBuffer container:

* `m_iStartPos`: physical container's index with the logical position 0
* `m_iMaxPosOff`: past-the-end offset value (NOT position!) of the occupied region

The container normally expects packets to be there in the order of their
sequence numbers. However, potentially packets can come out of order or
get lost in the UDP link; we can have then gaps in the form of unfilled
cells in the buffer. Hence we have 3 important regions:

* ICR: This starts with the cell 0 and ends with the next empty cell.
  ICR can be empty if the cell 0 is empty

* FIRST GAP: Starts with the very first empty cell in the buffer
  and follows until the first found filled cell. If the first empty
  cell following the ICR is at `m_iMaxPosOff` potition, the first
  gap is empty

* DROP REGION: Starts with the first filled cell after a series of
  empty cells of the FIRST GAP. Empty, if FIRST GAP is empty.

These positions are marked with the following fields:

* `m_iEndOff`: offset to the end of ICR. This points always to an empty cell.
  If this value is 0, the buffer has an empty ICR. If this value is equal to
  `m_iMaxPosOff`, the whole buffer is contiguous (no FIRST GAP or DROP REGION)

* `m_iDropOff`: offset to a packet available for retrieval after a drop. If 0,
  the DROP REGION is empty. DROP REGION is the fragment of the SCRAP REGION
  that begins with at least one valid packet

The DROP REGION is determined in order to quickly perform the dropping
operation. In case when the last packet has been extracted from the ICR, the
cell 0 is empty. When the decision for an extraction-over-consistency is
undertaken (live mode with too-late-packet-drop enabled, when the play time
comes for the first packet in the DROP REGION), the whole FIRST GAP is removed
from the buffer so the beginning of the buffer shifts to the DROP REGION, which
becomes ICR this way, and then the valid packet at cell 0 is extracted.

Operational rules:

Initially:

```
      m_iStartPos = 0
      m_iEndOff = 0
      m_iDropOff = 0
```

When a packet has arrived, then depending on where it landed:

1. Position: next to the last read one and newest

* `m_iStartPos` unchanged.
* `m_iEndOff` shifted by 1
* `m_iDropOff` = 0

2. Position: after a loss, newest.

* `m_iStartPos` unchanged.
* `m_iEndOff` unchanged.
* `m_iDropOff`:
    - set to this packet, if `m_iDropOff` == 0
    - otherwise unchanged

3. Position: after a loss, but belated (retransmitted) -- not equal to `m_iEndPos`

* `m_iStartPos` unchanged.
* `m_iEndOff` unchanged.
* `m_iDropOff`:
    - if `m_iDropOff` == 0, set to this
    - if `m_iDropOff` is past this packet, set to this
    - otherwise unchanged

4. Position: after a loss, sealing -- seq equal to position of `m_iEndOff`
  
* `m_iStartPos` unchanged.
* `m_iEndOff`:
    - since this position, search the first empty cell
    - stop at `m_iMaxPosOff` or first empty cell, whichever is found first
* `m_iDropOff`:
    - if `m_iEndOff` == `m_iMaxPosOff`, set it to 0
    - otherwise search for a nonempty cell since `m_iEndOff`
    - walk at maximum to `m_iMaxPosOff`

NOTE:

If DROP REGION is empty, then `m_iMaxPosOff` == `m_iEndOff`. If there is one
existing packet, then one loss, then two existing packets, the offsets are:

* `m_iEndOff` = 1
* `m_iDropOff` = 2
* `m_iMaxPosOff` = 4

To wrap up:

Let's say we have the following possibilities in a general scheme:


```
                [D]   [C]             [B]                   [A] (insertion cases)
 | (start) --- (end) ===[gap]=== (after-loss) ... (max-pos) |
           ICR        FIRST GAP
```

See the CRcvBuffer::updatePosInfo method for detailed implementation.

WHEN INSERTING A NEW PACKET:

If the incoming sequence maps to newpktpos that is:

* newpktpos <% (start) : discard the packet and exit
* newpktpos %> (size)  : report discrepancy, discard and exit
* newpktpos %> (start) and:
   * EXISTS: discard and exit (NOTE: could be also < (end))

```
[A]* seq == `m_iMaxPosOff`
      --> INC `m_iMaxPosOff`
      * `m_iEndPos` == previous `m_iMaxPosOff`
           * previous `m_iMaxPosOff` + 1 == `m_iMaxPosOff`
               --> `m_iEndPos` = `m_iMaxPosOff`
               --> `m_iDropPos` = `m_iEndPos`
           * otherwise (means the new packet caused a gap)
               --> `m_iEndPos` REMAINS UNCHANGED
               --> `m_iDropPos` = POSITION(`m_iMaxPosOff`)
```
COMMENT:

If this above condition isn't satisfied, then there are gaps, first at
`m_iEndOff`, and `m_iDropOff` is at furthest equal to `m_iMaxPosOff`-1. The
inserted packet is outside both the contiguous region and the following
scratched region, so no updates on `m_iEndPos` and `m_iDropPos` are necessary.

NOTE:

SINCE THIS PLACE seq cannot be a sequence of an existing packet,
which means that earliest offset(newpktpos) == `m_iEndOff`,
up to == `m_iMaxPosOff` - 2.

```
   * otherwise (newpktpos <% max-pos):
   [D]* newpktpos == `m_iEndPos`:
            --> (search FIRST GAP and FIRST AFTER-GAP)
            --> `m_iEndPos`: increase until reaching `m_iMaxPosOff`
            * `m_iEndPos` <% `m_iMaxPosOff`:
                --> `m_iDropPos` = first VALID packet since `m_iEndPos` +% 1
            * otherwise:
                --> `m_iDropPos` = `m_iEndPos`
   [B]* newpktpos %> `m_iDropPos`
            --> store, but do not update anything
   [C]* otherwise (newpktpos %> `m_iEndPos` && newpktpos <% `m_iDropPos`)          
            --> store
            --> set `m_iDropPos` = newpktpos
```
COMMENT: 

It is guaratneed that between `m_iEndOff` and `m_iDropOff` there is only a gap
(series of empty cells). So wherever this packet lands, if it's next to
`m_iEndOff` and before `m_iDropOff` it will be the only packet that violates
the gap, hence this can be the only drop pos preceding the previous
`m_iDropOff`.

Information returned to the caller should contain:

1. Whether adding to the buffer was successful.

2. Whether the "freshest" retrievable packet has been changed, that is:
   * in live mode, a newly added packet has earlier delivery time than one before
   * in stream mode, the newly added packet was at cell[0]
   * in message mode, if the newly added packet has:
     * completed the very first message
     * completed any message further than first that has out-of-order flag

The information about a changed packet is important for the caller in
live mode in order to notify the TSBPD thread.


WHEN CHECKING A PACKET

1. Check the position at `m_iStartPos`. If there is a packet, return info at
its position.

2. If position on `m_iStartPos` is empty, get the position of `m_iDropOff`.

NOTE THAT:

  * if the buffer is empty, `m_iDropOff` and `m_iEndOff` are both 0

  * if there is a packet in the buffer, but the first cell is empty,
    then `m_iDropOff` points to this packet, while `m_iEndOff` == 0.
    So after getting empty at cell 0 and `m_iDropOff` != 0, you can
    read with dropping.
  * If cell[0] is valid, there could be only at worst cell[1] empty
    and cell[2] pointed by `m_iDropOff`.

Note: `m_iDropOff` is updated every time a new packet arrives, even
if there are still not extracted packets in the ICR.

3. In case of time-based checking for live mode, return empty packet info,
if this packet's play time is in the future.

WHEN EXTRACTING A PACKET

1. Extraction is only possible if there is a packet at cell[0].
2. If there's no packet at cell[0], the application may request to
   drop up to the given packet, or drop the whole message up to
   the beginning of the next message.
3. In message mode, extraction can only extract a full message, so
   if there's no full message ready, nothing is extracted.
4. When the extraction region is defined, the `m_iStartPos` is shifted
   by the number of extracted packets.
5. After extracting packets, and therefore updated `m_iStartPos`, `m_iEndOff`
   must be set again by searching for the first empty cell or reaching
   `m_iMaxPosOff`.
6. If extraction involved dropping, `m_iDropOff` must be set again by
   searching since `m_iEndOff`+1 to find the first valid packet. If
   no such packet found before reaching `m_iMaxPosOff`, it's set to 0.
7. NOTE: all fields ending with `*Pos` are offsets, hence all of them must
   be updated after `m_iStartPos` was changed.



