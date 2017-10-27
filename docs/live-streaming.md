
Live streaming with SRT - guidelines
====================================

SRT is primarily created for live streaming. Before you use it, you must keep
in mind that Live Streaming is a process with its own rules, of which SRT
fulfills only and exclusively the transmission part. Live Streaming process
consists of more parts, though.


Transmitting MPEG TS binary protocol over SRT
=============================================

MPEG-TS is the most important protocol predicted to be sent over internet using
SRT and the main reason of initiating this project. SRT obviously isn't limited
to MPEG-TS, howver it still is limited in use to any data transmission that can
be called Live Streaming - at least if you use the Live mode (which is SRT
default).

MPEG-TS consists of single units of 188 bytes. By having `188*7` we get the
size of 1316, which is the maximum multiplicity of 188 that results still less
than 1500, the standard MTU size in UDP transport, which is the underlying
transport layer for SRT. The headers for Ethernet, IP and UDP protocol occupy
28 bytes in this size, leaving 1472 bytes of size, and SRT occupies next 16
bytes for its own header, which leaves the maximum payload size 1456 bytes. The
MPEG-TS transport unit size still fits in it, and fortuantely this is a
standard unit size used in Live Streaming, also with the use of UDP.

SRT isn't limited to MPEG-TS - you can use any other data format that you
think can be suitable, as well as use some your own higher level protocol
on top of MPEG-TS and have your extra header (you still have extra 140 bytes
per unit for that purpose). But even though that, the transmission must still
satisfy the Live Streaming Requirements.


Live Streaming Requirements
===========================

The MPEG-TS stream, let it be used as a good example, consists of Frames, which
is a portion of data assigned to particular stream (as usually you have multipe
streams interleaved, at least video and audio). The video stream has always its
playing speed expressed in a unit of fps (frames per second). This value maps
to a duration for one video frame, so for example 60fps means that one video
frame should be "displayed" for a time of 1/60s (it's called "duration"). Then,
you can extract from the stream both one video frame and several "audio
frames", that is, single units that turn into audio stream and they cover
the same time range as the video frame. Every such unit in the stream has its
own *timestamp*, which can be used to synchronize the reading and displaying
the data.

Now, if you imagine the very first video frame since the time you started
reading the stream, which is called I-frame. This frame is just a compressed
picture and it needs no more information than this to decode it into a
displayable pixmap. You can imagine that it needs several units (1316 bytes)
to transmit it over the network, plus the audio frames for the corresponding
time covered by this very video frame should also match this "duration" time.
So, you need to send several units of 1316 bytes to transmit all these data
over the network, however you usually spend much less time to do it than this
"duration" time (1/60s in our example). It doesn't mean, however, that you
should send it as fast as possible and then simply "sleep" for the remaining
time! It means that you should split the whole 1/60s time **evenly** to all
single 1316-byte units. So, there is a "unit duration" for every single
1316-byte unit, so you send the unit, and then you should "sleep" **after every
unit**, if sending (as it usually happens) has taken less time than the exact
time predicted to be spent by sending this unit.

Should that whole "I-Frame with corresponding audio" sending take **exactly**
1/60s? Of course, not exactly. This may have some slight time differences,
which come from two reasons:

1. Sending over the network has only average time perdictability. Unusual and
unexpected delays, coming from both the network and your system, may slightly
destroy your perfect time calculations. Therefore you should not rely on the
exact number of bytes sent by one "frame package", but rather on overall
transmission size, and do often periodic synchronization of the transmission
time basing on the timestamps in the stream.

2. Not all frames as I-Frames. Most of the frames sent in the video stream are
"difference frames" (P or B frames), which need all preceding (or even
succeding) frames already received to be able to decode the designated frame.
As you can guess, difference frames are much shorter than I-Frames, so it makes
the whole "frame package" carry much less data to transport, but they still
cover the same time gap ("duration"). 

Taking all the above things into account, you know now that actually the most
important in the synchronization is to make the I-Frame completely transported
at specified time, and every next frame must be received at more-less the same
time as I-Frame, next by one duration period, and with some only slight time
tolerance, which results from size difference. what must be absolutely adhered
to, however, is that the distance between the first network unit transporting
the first portion of one I-Frame, and the same unit for the next I-Frame must
correspond with the time distance between these two same I-frames that results
from their timestamps.

In other words: there is some slight buffering at the receiving side, but the
requirement for a live streaming is that: data must be transmitted with exactly
the same AVERAGE speed, as the video player that would play it, or even more
exactly, the data must be produced exactly the same fast as they will be consumed
by the video player.


Live Streaming Process
======================

Now that you know how the Live Streaming should turn a bunch of MPEG-TS
encoded video stream into a network live stream, let's complete the definition
of the live streaming transport over the network using SRT.

The source side should be a real live stream, that is, for example:

- a file read by an application that can make a live stream out of it
- a grab device that is grabbing the data in constant rates and then
the data are passed through the live encoder
- some existing live network encoder that streams a live stream over UDP
- a camera that uses some simple encoding method, like MJPEG (needs transcoding)

As an example of an application that can make a live stream out of a file
can be `ffmpeg`. You just need to remember about two things here:

- The `-re` options is required for making the live stream out of a file
- The `pkt_size=1316` parameter should be added to UDP output URI, if you make
ffmpeg output to UDP

Pay attention first that the splitting the data for transport into single units
that can fit in one UDP packet, and first of all defining time distances
between these units is something that must come in synch with the timestamps
in the transport stream. Even if you have a grab device, it usually will make
just one video frame plus some audio at specified time, but still this must
be split into single network units with appropriate time distance between
them. Still, this can only be done by an application, which well knows the
type of the stream and knows how it must be most properly split into the
time-divided single network transport units.

The `stransmit` application, or any other application that uses SRT for
reading, should read always in 1316-byte portions (network transport units) and
feed each such unit into the call to appropriate `srt_send*` function. The
important part of that process is that these 1316 units appear at exactly
required times so that SRT can replay them with the identical time distance
between them at the reception side.

So, you feed these 1316-byte units into SRT, SRT will send them to the
other side, applying a configurable delay, known as "latency". This is an
extra time that the packet will have to spend in the "anteroom" on the
receiving side before it's delivered to the output. This time should cover
both the unexpectedly delayed transmission of one UDP packet, as well
as have extra time for a case when the packet was lost and had to be
retransmitted. Every UDP packet carrying an SRT packet has a timestamp,
which is grabbed at the time when the packet is given up to SRT for
sending, and using that timestamp the appropriate delay is applied before
delivering to the output, so that time distances between two consecutive
packets at the delivery application is aimed to be exactly the same as
the time distance between these same packets at the time when they are
given up to SRT for sending.




