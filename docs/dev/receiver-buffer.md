# Receiver buffer

The receiver buffer in SRT is a circular buffer keeping pointed pieces of memory
containing the data in their desired order, split into packets the way they were
received.

Every cell of the container represents a sequence number of the packet; there's the
`m_iStartSeqNo` field that defines the sequence number of the first cell, and all
others follow the sequence. Incoming packets then get their cell index basing on
their sequence number; if the number is outside of the range of sequences that
the buffer currently projects, the packet is not inserted. Same if the packet is
already there.


## Terminology

The buffer may or may not deliver data basing on various conditions, mainly
represented by the regions that embrace the range of buffer cells.

* BUSY REGION: The buffer has a fixed capacity, but not all cells need to be
in use at the moment. It starts with the first cell and ends with the latest
cell for which a packet has ever been received; naturally empty cells may
happen to be inside this region in case when the packet was lost.

* ICR: Initial Contiguous Region: This is present if the first cell contains
a valid packet and it continues until the first gap. If there are no gaps
(no packets were lost or nothing has arrived out of order), ICR is the same
as Busy Region.

* FIRST GAP: It starts with the empty cell that follows the last cell of ICR,
if ICR is shorter than the Busy Region, and continues with consecutive empty
cells until the first following filled cell (Drop Target).

* SCRAP REGION: Region with possibly filled or empty cells. It starts with
the first gap and ends with the end of Busy Region. NOTE that in the scrap
region the very first cell is empty and the very last cell is filled.

* DROP TARGET: a filled cell that immediately follows the First Gap. May
be not present in case when there's no Scrap Region.

* SPARE REGION: the region of the buffer with existing cells, but following
the last cell of the Busy Region until the end of capacity.

NOTE: This is a circular buffer, which means that it holds a solid array
with fixed size, but as initial portion of the buffer gets decommissioned,
the position of the first cell may be in the middle of the array. Some
fields, however, determine the position, others determine the positive-only
offset towards the position of the first cell, with is a logical position
of the cell in the circular buffer.

The field names have appropriate names to qualify the category:

* ...Off : carries an offset - a relative position towards the first cell
* ...Pos : carries the exact index to the array

## Design chart and explanations

```

           |      BUSY REGION                      | 
           |           |                           |           |
           |    ICR    |  SCRAP REGION             | SPARE REGION...->
   ......->|           |                           |           |
           |             /FIRST-GAP                |           |
   |<------------------- m_entries.size() ---------------------------->|
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

   m_pUnit[i]->status:
             EntryState_Empty: No packet was ever received here
             EntryState_Avail: The packet is ready for reading
             EntryState_Read: The packet is non-order-read
             EntryState_Drop: The packet was requested to drop

    m_iStartPos: the first packet that should be read (might be empty)
    m_iMaxPosOff: past-the-end of the BUSY REGION
    m_iEndOff: shift to the past-the-end of ICR. This points always to an empty cell.
    m_iDropOff: shift to the DROP TARGET packet. If 0, there's no DROP TARGET.
    m_iFirstNonreadPos: either the first empty cell, or the first cell with PB_FIRST boundary flag (message mode only)
```

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
      - if m_iDropOff == 0
      - if m_iDropPos %> this sequence (still a drop, but updated)
      - otherwise unchanged
```

4. Position: after a loss, sealing -- seq equal to position of `m_iEndOff`
 
``` 
   m_iStartPos unchanged.
   m_iEndOff: set to the first found empty cell since the current position (up to m_iMaxPosOff)
   m_iDropOff:
     - if m_iEndOff == m_iMaxPosOff, set to 0
     - otherwise search for the first filled cell starting from m_iEndOff
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

To wrap up:

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


