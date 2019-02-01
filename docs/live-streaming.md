
Live streaming with SRT - guidelines
====================================

SRT is primarily created for live streaming. Before you use it, you must keep
in mind that Live Streaming is a process with its own rules, of which SRT
fulfills only and exclusively the transmission part. The Live Streaming process
consists of more parts.


Transmitting MPEG TS binary protocol over SRT
=============================================

MPEG-TS is the most important protocol commonly sent over internet using
SRT, and the main reason for initiating this project. 

MPEG-TS consists of single units of 188 bytes. Multiplying `188*7` we get 1316, which 
is the maximum product of 188 that is less than 1500 (`188*8=1504`), which is the 
standard MTU size in Ethernet. The headers for the IP and UDP protocols occupy 28 bytes 
of a standard MTU, leaving 1472 bytes, and SRT occupies next 16 bytes for its own header, 
which leaves a maximum payload size of 1456 bytes. A 1316-byte cell is a good single 
transport unit size for SRT, and it is also often used when sending MPEG-TS over UDP.

Note that SRT isn't limited to MPEG-TS -- it can be applied to any "live streaming" data 
transmission (as long as you use Live mode, which is the SRT default mode). You can use 
any other suitable data format, and any intermediate protocol on top of MPEG-TS with 
an extra header (this is an option that people often try with RTP) - note that
1316 is the default maximum payload size, which can be changed using the
`SRTO_PAYLOADSIZE` option to no more than 1456).

However, the transmission must still satisfy the Live Streaming Requirements.


Live Streaming Requirements
===========================

The MPEG-TS stream, as a good example, consists of Frames. Each
Frame is a portion of data assigned to a particular stream (usually you have
multipe streams interleaved, at least video and audio). The video stream always has 
its playing speed expressed in fps (frames per second) units. This
value maps to a duration for one video frame. So, for example, 60 fps means that
one video frame should be "displayed" for a duration of 1/60 of a second. You can 
extract from the stream one video frame and several "audio frames", that is, single units 
that are part of the audio stream that cover the same time range as the video frame. 
Every such unit in the stream has its own *timestamp*, which can be used to synchronize 
the reading and displaying of the data.

Now, imagine the very first video frame in the
stream, which is called an "I-frame". This frame is just a compressed picture, and it
needs no additional information to decode it into a displayable pixmap. Several 
transport units (of 1316 bytes) are needed to transmit the I-frame over the network. 
The audio frames for the period of time corresponding to this video frame 
should also fit into this "duration". So, you will need to send several units of 
1316 bytes to transmit all these data over a network. But you usually spend much less 
time to transmit than the actual "duration" (1/60s in our example). This doesn't mean, 
however, that you should send the video and audio data as fast as possible and then 
simply "sleep" for the remaining time! It means that you should split the whole 
1/60 second **evenly** across all single 1316-byte units. There is a "unit duration" for 
every single 1316-byte unit, so you send the unit, and then "sleep" **after 
every unit**, if sending (as usually happens) has taken less time than the exact time 
predicted to be spent by sending this unit.

Should sending that whole "I-Frame with corresponding audio" take **exactly** 1/60s? 
No, not exactly -- there may be some slight time differences, for two reasons:

1. Sending over a network has only average time predictability. Unusual and
unexpected delays, coming from both the network and your system, may slightly
disrupt time calculations. Therefore you should not rely on the
exact number of bytes sent in one "frame package", but rather on overall
transmission size, and do frequent periodic synchronization of the transmission
time based on the timestamps in the stream.

2. Not all frames are I-Frames. Most of the frames sent in the video stream are
"difference frames" (P or B frames), which can only be decoded if all preceding (or even
succeding) frames are already received. As you can guess, difference frames are much 
shorter than I-Frames, so there is much less data in a whole "frame package" to 
transport, even though these frames still cover the same time period ("duration"). 

Taking the above into consideration, you can understand that the most important factor 
in synchronizing the streaming data is to make sure that the entire I-Frame is 
transported at a specified time, and that every subsequent frame is received at more 
or less the same time as the I-Frame, plus one duration period. There is only a slight 
time tolerance, which results from size differences. 

What must be absolutely adhered to, however, is that the interval between the first 
network unit transporting the first portion of one I-frame and the same unit for the 
next I-frame must correspond to the interval between these same I-frames as given
by their timestamps.

In other words, there is some slight buffering at the receiving side, but the
requirement for a live stream is that data must be transmitted with exactly the same 
**average** speed as they are output by a video player. More precisely, the data must 
be produced at exactly the same speed as they will be consumed by the video player.


Live Streaming Process
======================

Now that you know how Live Streaming turns a bunch of MPEG-TS encoded video and audio 
frames into a network live stream, let's complete the definition of live streaming 
by describing transport over the network using SRT.

The source side should be a real live stream, such as for example:

- a file read by an application that can generate a live stream
- a frame grabber or other device that is capturing data at constant rates, and then 
passing them to a live encoder
- a live network encoder sending a live stream over UDP
- a camera that uses some simple encoding method, like MJPEG

`ffmpeg` is an example of an application that can generate a live stream from a file. 
Note the following:

- The `-re` option is required for making a live stream from a file
- The `pkt_size=1316` parameter should be added to the UDP output URI, if you make
ffmpeg output to UDP

As we described above, you must first split the data into individual cells that can fit 
into one UDP packet. This entails defining time intervals between these cells so that 
they are in synch with the timestamps in the transport stream. 

If you have a device that is capable of grabbing individual frames, it will usually 
capture only one video frame and the corresponding audio at a time, which must be split
into single network units with appropriate time intervals between them. This can only 
be done by an application with explicit knowledge of the type of stream and how to 
transform it into time-divided single network transport units.

The `stransmit` application, or any other application that uses SRT for
reading, should always read data in 1316-byte segments (network transport units) and
feed each such unit into the call to an appropriate `srt_send*` function. The
important part of this process is that these 1316-byte units appear at precise times 
so that SRT can replay them with the identical time interval
between them on the receiving side.

When you feed these 1316-byte units into SRT it will send them to the other
side, applying a configurable delay known as "latency". This is an extra amount of time
that a packet will have to spend in the "anteroom" on the receiving side
before it is delivered to the output. This time should cover both any
unexpected transmission delays for a UDP packet, as well as allowing extra time
for the case where a packet is lost and has to be retransmitted. Every UDP
packet carrying an SRT packet has a timestamp, which is grabbed at the time
when the packet is passed to SRT for sending. Using that timestamp the
appropriate delay is applied before delivering to the output. This ensures that the 
time intervals between two consecutive packets at the delivery application are identical 
to the intervals between these same packets at the moment they were passed to SRT 
for streaming.




