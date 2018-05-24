/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef SRT4UDT_H
#define SRT4UDT_H

#ifndef __UDT_H__
#error "This is protected header, used by udt.h. This shouldn't be included directly"
#endif

/*
* SRT_ENABLE_SRTCC_EMB: Embedded SRT Congestion Control
*/
#define SRT_ENABLE_SRTCC_EMB    1

/*
* SRT_ENABLE_SRTCC_API: "C" application setting ("C" wrapper)
*/
//undef SRT_ENABLE_SRTCC_API    1

/*
* SRT_ENABLE_TSBPD: TimeStamp-Based Packet Delivery
* Reproduce the sending pace at the receiver side using UDT packet timestamps
*/
#define SRT_ENABLE_TSBPD 1

#ifdef  SRT_ENABLE_TSBPD

#define SRT_ENABLE_CTRLTSTAMP 1         /* Set control packet timestamp (required by TSBPD) */
#define SRT_ENABLE_TLPKTDROP 1          /* Too-Late Pkts Dropping: Sender drop unacked data too late to be sent and recver forget late missing data */
//undef SRT_ENABLE_ECN 1                /* Early Congestion Notification (for source bitrate control) */
#define SRT_ENABLE_SRCTIMESTAMP 1       /* Support timestamp carryover from one SRT connection (Rx) to the next (Tx) */

//undef SRT_DEBUG_TSBPD_OUTJITTER 1     /* Packet Delivery histogram */
//undef SRT_DEBUG_TSBPD_DRIFT 1         /* Debug Encoder-Decoder Drift) */
//undef SRT_DEBUG_TSBPD_WRAP 1          /* Debug packet timestamp wraparound */
//undef SRT_DEBUG_TLPKTDROP_DROPSEQ 1
//undef SRT_DEBUG_SNDQ_HIGHRATE 1

#endif /* SRT_ENABLE_TSBPD */

/*
* SRT_ENABLE_FASTREXMIT
* Earlier [re-]retransmission of lost retransmitted packets
*/
#define SRT_ENABLE_FASTREXMIT 1

/*
* SRT_ENABLE_CONNTIMEO
* Option UDT_CONNTIMEO added to the API to set/get the connection timeout.
* The UDT hard coded default of 3000 msec is too small for some large RTT (satellite) use cases.
* The SRT handshake (2 exchanges) needs 2 times the RTT to complete with no packet loss.
*/
#define SRT_ENABLE_CONNTIMEO 1

/*
* SRT_ENABLE_NOCWND
* Set the congestion window at its max (then disabling it) to prevent stopping transmission
* when too many packets are not acknowledged.
* The congestion windows is the maximum distance in pkts since the last acknowledged packets.
*/
#define SRT_ENABLE_NOCWND 1

/*
* SRT_ENABLE_NAKREPORT
* Send periodic NAK report for more efficient retransmission instead of relying on ACK timeout
* to retransmit all non-ACKed packets, very inefficient with real-time and no congestion window.
*/
#define SRT_ENABLE_NAKREPORT 1

/*
* SRT_ENABLE_BSTATS
* Real bytes counter stats (instead of pkts * 1500)
*/
#define SRT_ENABLE_BSTATS 1

#ifdef  SRT_ENABLE_BSTATS

#define SRT_ENABLE_INPUTRATE 1          /* Compute encoded TS bitrate (sender's input) */
#define SRT_DATA_PKTHDR_SIZE (16+8+20)  /* SRT+UDP+IP headers */

#define SRT_ENABLE_RCVBUFSZ_MAVG 1      /* Recv buffer size moving average */
#define SRT_ENABLE_SNDBUFSZ_MAVG 1      /* Send buffer size moving average */
#define SRT_MAVG_SAMPLING_RATE 40       /* Max sampling rate */

#define SRT_ENABLE_LOSTBYTESCOUNT 1

#endif /* SRT_ENABLE_BSTATS */

/*
* SRT_ENABLE_LOWACKRATE
* No ack on each packet in DGRAM mode
*/
#define SRT_ENABLE_LOWACKRATE 1

/*
* SRT_ENABLE_IPOPTS
* Enable IP TTL and ToS setting
*/
#define SRT_ENABLE_IPOPTS 1

/*
* SRT_ENABLE_HAICRYPT
* Encrypt/Decriypt
*/
#define SRT_ENABLE_HAICRYPT 1

/*
* SRT_ENABLE_SND2WAYPROTECT
* Protect sender-only from back handshake and traffic
*/
#define SRT_ENABLE_SND2WAYPROTECT 1

/*
* SRT_FIX_KEEPALIVE
* 
*/
#define SRT_FIX_KEEPALIVE 1

#endif /* SRT4UDT_H */
