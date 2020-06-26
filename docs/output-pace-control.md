#Output Pace Control

Published: 2020-06-26
Last Updated: 2020-06-26

**content**

## Introduction
This document introduces the Output Pace Mode (OPM) option (`SRTO_OUTPACEMODE`), an option that defines how the options affecting the SRT Sender output rate (`SRTO_MAXBW`, `SRTO_INPUTBW`, and `SRTO_OHEADBW`) are combined to achieve Output Rate Control (OPC).

## Overview
To ensure smooth video playback of a live mode receiving peer, SRT in live mode must control the sender buffer level to prevent overfill and depletion. Output pace control (OPC) is not a congestion control mechanism and does not have the goal to be a fair Internet citizen. Live mode fairness cannot be accomplished inside SRT but rather outside by controlling the bitrate of the encoder at the input of the SRT sender. OPC attempt to send packets as fast as they are submitted by the application to maintain a relatively stable buffer level. While this looks like a simple problem, the sender`s constituent of the ARQ (Automatic Repeat reQuest) system between the input and output of the SRT sender adds some complexity.

The optimal buffer levels in a SRT system are (in unit of time) around RTT (Round Trip Time) for the sender and just below the configured latency (`SRTO_LATENCY`) for the receiver. Assuming:
1. the SRT sender transmits at the same rate the application submits packets, and
2. in absence of network impairments,
the ACKs maintain the sender's buffer level around RTT. The goal of OPC, which is essentially a sender`s mechanism, is to ensure that the assumption 1 above remains.

The output pace is set by adjusting a Packet Send Interval, calculated from the desired output bitrate and the packets payload average size.

## Output Pace Modes (OPMs)
OPM existed before the implementation of the `SRTO_OUTPACEMODE` option but they were unnamed and implicit, based on the setting of `SRTO_MAXBW`, `SRTO_INPUTBW`, and `SRTO_OHEADBW`.

  SRTO_...
| OUTPACEMODE      | MAXBW(B/s) | INPUTBW(B/s) | OHEADBW(%) | Maximum Output rate                            |
|-----------------:|:----------:|:------------:|:----------:|:-----------------------------------------------|
|       0*         |     -1     |      x       |     x      |  infinite: 1Gbps (30Mbps before v1.3.3)
|       0*         |     Mbw    |      x       |     x      |  Mbw
|       0*         |      0     |      0       |     oh     |  sIbw x (100+P)%
|       0*         |      0     |     cIbw     |     oh     |  cIbw x (100+P)%
| SRT_OPM_UNSET    |     -1     |      0       |     25     |  Live Defaults
| SRT_OPM_UNTAMED  |      x     |      x       |     x      |  infinite: 1Gbps (30Mbps before v1.3.3)
| SRT_OPM_CAPPED   |     Mbw    |      x       |     x      |  Mbw
| SRT_OPM_SMPINBW  |      x     |      x       |     oh     |  sIbw x (100+oh)%, Live mode only
| SRT_OPM_INBWSET  |      x     |     cIbw     |     oh     |  cIbw x (100+oh)%, Live mode only
| SRT_OPM_INBWADJ  |      x     |     cIbw     |     oh     |  Max(cIbw,sIbw) x (100+oh)%, Live mode only

0*  : unset, or set to default `SRT_OPM_UNSET`
x   : irrelevant
Mbw : value set with `SRTO_MAXBW`
oh  : percentage of bandwidth overhead for retransmission. Set with `SRTO_OHEADBW`
cIbw: Configured input rate, usually the setting of and encoder feeding SRT sender's input.
sIbw: Internally sampled input rate

### SRT_OPM_UNSET
The use of `SRTO_OUTPACEMODE` is optional and its default value (SRT_OPM_UNSET) preserves the previous behavior (API/ABI backward compatibility).

### SRT_OPM_UNTAMED
This OPM puts no limit on the output speed and is meant to set an infinite output bitrate. To do the maths to obtain the packet send interval, a number is needed and this number is 1Gbps since SRT v1.3.3 (30 Mbps before).

### SRT_OPM_CAPPED
This OPM limits the output to the value sets with `SRTO_MAXBW` (which is set in  bytes/s).

### SRT_OPM_SMPINBW
This OPM enables input rate measurement to adjust the output rate, adding the overhead set with `SRTO_OHEADBW` to account for possible retransmissions. The application must set `SRTO_OHEADBW`.

This OPM is ideal when the application does not control the input source (when retransmitting a received network stream for example).

The disadvantage is the delayed reaction to input bitrate changes when using a VBR source for example. A black screen or static image (low bitrate) followed by a high complexity action scene may cause the output rate to rise too slowly, filling the send buffer and causing packet drops (too old to be sent-and-played by receiver according to the configured latency (`SRTO_LATENCY`).

### SRT_OPM_INBWSET
This OPM uses the input rate set with `SRTO_INPUTBW` to adjust the output rate, adding the overhead set with `SRTO_OHEADBW` to account for possible retransmissions. The application must set both `SRTO_INPUTBW` and `SRTO_OHEADBW`.

This method is ideal when the application controls the input source.

### SRT_OPM_INBWADJ
This method combines both the internally sampled input bandwidth (sIbw) and the configured input rate (cIbw) set with SRTO_INPUTBW). The overhead set with `SRTO_OHEADBW` is also added to account for possible retransmissions. This OPM is the
only one that requires the `SRTO_OUTPACEMODE` option since the internal sampling of the input rate was previously enabled by setting SRTO_INPUTBW to 0.

This methods was implemented to overcome weaknesses of `SRT_OPM_SMPINBW` and `SRT_OPM_INBWSET` on an application using a VBR encoder as the input source. The black screen syndrome of `SRT_OPM_SMPINBW` is described above. The weakness of `SRT_OPM_INBWSET` was specific to the hardware VBR encoder used at low bitrates (~200kbps) where the encoded overshot the configured bitrate that was also set in SRT with `SRTO_INPUTBW` with consequences similar to the black screen of the `SRT_OPM_SMPINBW`. By using the maximum of the actual rate sampled internally (sIbw) and the configure input rate (cIbw) both weaknesses are mitigated.



