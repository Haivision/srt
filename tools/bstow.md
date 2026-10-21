BSTOW
=====

This is a simulation development tool for sending the live stream using the
stow mode.

The BSTOW name stands for "binary stow" and it's a binary data container that
provides payloads with associated parameters. It's a medium security format
(less than the one with MPEG-TS) and it's intended to keep all the data in a
file.

A possible extension is to make it an index file to another file in the same
directory, where instead of physical payload, there is only declared numeric
offset of the beginning of the payload.


Motivation
==========

The stow mode is a sending mode for a live stream, where packets still do
have their smoothly distributed time gaps between single network packets,
but the real time when the packet is sent is allowed to be earlier than
at this very time.

However, there are not many existing software solution that can work this
way. Testing with `ffmpeg -re` is pointless - even if you can use a different
time proportion between stream time and sending time than 1:1 using the
`-readrate X` option (allowing to use X:1 proportion), it will speed up the
right way only if you have a real live encoder (and for that case it could be
adjusted to the needs) or when the reading application causes appropriate
blocking while tracing the stream's times by itself.

What really is needed is a situation that the frame is available as a whole
at once (or at least a bigger portion of it), data are delivered with time gaps
that correspond to the framerate, and even if the data are split into single
packets that fit in a network packet with their defined sending time, the whole
series of packets that carry a single frame are all available at once. It would
be then up to the decision of the application whether to send the packet
physically at the declared time, or earlier.


BSTOW format
============

The general format is the following:

* The 4-byte constant header
* The parameter with the value
* The payload (if required by the parameter)

The header consists of 4 bytes, in HEX: `BE 57 08 88`.

The parameter uses special value-length encoding:

1. 4 bytes are read as A, B, C, D.
2. If byte A has the 0x80 bit clear, the format is: LABEL = A, VALUE = B:C:D
3. If byte A has the 0x80 bit set, then:
   * LABEL = A:B & 0x7FFF
   * LENGTH = C:D
   * VALUE = Further bytes of that LENGTH

All multi-byte values are encoded as Big Endian.

So, for example:

* 01 03 AB 02 - LABEL=1, VALUE=0x03AB02
* 80 02 00 04 DE AD BE EF - LABEL=2 VALUE=0xDEADBEEF

Parameters use the following LABELs:

* LENGTH: 1 : length of the payload
* PLAYTIME: 2 : block's timestamp (DTS in video terms)
* SENDTIME: 3 : packet's timestamp (smooth-scaled bitrate)
* DATA : 0x7F : end of parameters, start of the payload

Parameters should be specified in total as first and the DATA parameter
should be the last one, with just 0 value, followed by the payload. The
payload should have the length as declared in the LENGTH parameter. The
PLAYTIME and SENDTIME parameters' values and presence depend on the
position of the payload in the structure.


The BSTOW file structure
========================

The BSTOW file consists of single packet declarations, that is, payloads
with attached parameters, but the shape of the parameters define also the
structure.

The BSTOW system consists of 3 layers of information:

1. Series
2. Block
3. Packet

Meaning, Series contains multiple Blocks, and a Block contains multiple
Packets.

To help you imagine how it maps to real MPEG-TS structure:

* Packet is one portion of data that can be sent over network at once.

* Block is a portion of data representing a single frame. This need not
be just the video frame, it can be also additional data, and also not only
audio data, but audio data happen to be also "attached" to the corresponding
frame and gets completed together of the particular video frame data.

* Series collects blocks that belong to one GOP. In other words, it's a series
of blocks that start with the I-Frame and ends before the next I-Frame.

Now that the structure is explained, the PLAYTIME and SENDTIME parameters
can be explained with more precision:

* PLAYTIME corresponds to the video frame's DTS, and it's only specified in
the first Packet of the Block. Playtime is monotonic; it can be generated
basing on the timestamp read from the video frame in MPEG-TS, but discontinuity
must be detected and handled when translating the timestamps.

* SENDTIME is specified in every packet in the series, except the first one
(or the first one has simply the 0 value). The value should designate the time
when the packet has to be send according to the smooth time distribution rules
of the live stream, expressed as the time distance between this packet and the
first packet of the series. Note that even though the first packet of a block
does define its PLAYTIME, the SENDTIME is still defined according to this rule.

As an example how this might be distributed, a list of packets:

* 0001: (series: 1) (block: 1) PLAYTIME=1000
* 0002: SENDTIME=3
* 0003: SENDTIME=6
* 0004: SENDTIME=9
* 0005: SENDTIME=12
* 0006: SENDTIME=15
* 0007: SENDTIME=18
* 0008: (block: 2) PLAYTIME=1020 SENDTIME=21
* 0009: SENDTIME=24
* 0010: SENDTIME=27
* 0011: SENDTIME=30
* 0012: SENDTIME=33
* 0013: SENDTIME=36
* 0014: (series: 2) (block: 1) PLAYTIME=1040
* 0015: SENDTIME=3
* 0016: SENDTIME=6

The reading rules are the following:

1. The reading starts with the packet declared as the first in the series.
Sending time should be immediate and the application should use this as a base
time for any further sendings. The playtime should be remembered for next
calculations as well, and also the time when sending of the first packet happened.

2. Reading the next packet gets the sendtime and the value should be translated
to the sending time in the application by adding this value to the base value.

3. Reading the first packet of the next block in the series should read the
playtime, but it's not significant for any data. This is only to allow for
simulation of the live encoder: the current time should be decreased by the
time of sending the first packet of the previous block, and if the distance
is different than the distance of the playtime declared in these packets, the
application should simulate non-readiness of the frame by sleeping for the
remaining time.

4. Reading the first packet of the next series should do the following:

   * Determine the alleged sendtime for this packet from the distance from
the last packet of the previous series

   * Calculate the the distance between this value and the send time base.

   * Compare this distance with the distance between the playtime values of
this one and the first packet of the previous series

   * Prepare compensation for the difference between these two values:

      - if sending was too fast, simply add the difference to the send time
of the first packet

      - if sending was too slow, remember the difference in the compensation
register and distribute it with the next sent packets as much as possible

Example reading session for the above packet example:

Initial playtime = 1000, sendtime = 10000

Sending times with comments:

* 0001: 10000 (taking initial, sendtime base = 10000)
* 0002: 10003
* 0003: 10006
* 0004: 10009
* 0005: 10012
* 0006: 10015
* 0007: 10018
* 0008: 10021 (pause for up to 20ms before delivery)
* 0009: 10024
* 0010: 10027
* 0011: 10030
* 0012: 10033
* 0013: 10036
* 0014: 10040 (see Calculations below, new sendtime base = 10040)
* 0015: 10043
* 0016: 10046

Calculations:

* PLAYTIME distance: 40
* Last send gap: 10036-10033 = 3
* New packet sendtime = last packet + last gap = 10036+3 = 10039
* SENDTIME distance: 10039 - 10000 = 39
* DEVIATION = PLAYTIME distance - SENDTIME distance = 1
* Compensation method:
   - One shot fix for sendtime: +1
   - SENDTIME = 10039 + 1 = 10040


Testing with SRT
================

For SRT there's a specific application prepared: `srt-test-stow`, which uses
the BSTOW file as the source and SRT endpoint for sending.

The `senmode` option should be configured by the user - default options remain
default, so the default mode is live: packets are sent immediately upon arrival.
The application obtains the sendtime directly from the BSTOW reader and sets it
into the packet's timestamp; sleeps between sending is another matter.

Among the sendmode values there are 3 possibilities:

* 0 (default): live mode (send immediately upon arrival)
* 1: eager mode - send with speedup and controlled speed
* 2: planned mode - send the packets exactly according to the declared sendtime

