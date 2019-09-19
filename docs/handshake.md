# SRT Handshake

Published: 2018-06-28  
Last updated: 2018-06-28

**Contents**

- [Overview](#overview)
- [Short Introduction to SRT Packet Structure](#short-introduction-to-srt-packet-structure)
- [Handshake Structure](#handshake-structure)
- [The "UDT Legacy" and "SRT Extended" Handshakes](#the-udt-legacy-and-srt-extended-handshakes)
  - [UDT Legacy Handshake](#udt-legacy-handshake)
  - [Initiator and Responder](#initiator-and-responder)
  - [The Request Type Field](#the-request-type-field)
  - [The Type Field](#the-type-field)
- [The Caller-Listener Handshake](#the-caller-listener-handshake)
  - [The Induction Phase](#the-induction-phase)
  - [The Conclusion Phase](#the-conclusion-phase)
- [The Rendezvous Handshake](#the-rendezvous-handshake)
  - [HSv4 Rendezvous Process](#hsv4-rendezvous-process)
  - [HSv5 Rendezvous Process](#hsv5-rendezvous-process)
    - [Serial Handshake Flow](#serial-handshake-flow)
    - [Parallel Handshake Flow](#parallel-handshake-flow)
  - [Rendezvous Between Different Versions](#rendezvous-between-different-versions)
- [The SRT Extended Handshake](#the-srt-extended-handshake)
  - [HSv4 Extended Handshake Process](#hsv4-extended-handshake-process)
  - [HSv5 Extended Handshake Process](#hsv5-extended-handshake-process)
  - [SRT Extension Commands](#srt-extension-commands)
    - [HSREQ and HSRSP](#hsreq-and-hsrsp)
    - [KMREQ and KMRSP](#kmreq-and-kmrsp)
    - [Congestion controller](#congestion-controller)
    - [Stream ID (SID)](#stream-id-sid)


## Overview

SRT is a connection protocol, and as such it embraces the concepts of "connection"
and "session". The UDP system protocol is used by SRT for sending data as well as
special control packets, also referred to as "commands".

An SRT connection is characterized by the fact that it is:

- first engaged by a *handshake* process
- maintained as long as any packets are being exchanged in a timely manner
- considered closed when a party receives the appropriate close command from
its peer (connection closed by the foreign host), or when it receives no
packets at all for some predefined time (connection broken on timeout).

Just like its predecessor UDT, SRT supports two connection configurations:

1. **Caller-Listener**, where one side waits for the other to initiate a connection
2. **Rendezvous**, where both sides attempt to initiate a connection

As SRT development has evolved, two handshaking mechanisms have emerged:

1. the **legacy UDT handshake**, with the "SRT" part of the handshake implemented 
as extended control messages; this is the only mechanism in SRT versions 1.2 and 
lower, and is known as **HSv4** (where the number 4 refers to the last UDT 
version)
2. the new **integrated handshake**, known as **HSv5**, where all the required
information concerning the connection is interchanged completely in the
handshake process

The version compatibility requirements are such that if one side of the
connection only understands *HSv4*, the connection is made according to *HSv4*
rules. Otherwise, if both sides are at SRT version 1.3.0 or greater, *HSv5* is
used. As the new handshake supports several features that might be mandatory
for a particular application, it is also possible to reject an HSv4-to-HSv5
connection by setting the `SRTO_MINVERSION` socket option. The value for this
option is an integer with the version encoded in hex. For example:

    int req_version = 0x00010300; // 1.3.0
	srt_setsockflag(s, SRTO_MINVERSION, &req_version, sizeof(int));

**IMPORTANT:** Your SRT application must do either of these two things:

- Be *HSv4* compatible. In this case it must:
   - **NOT** use any new features in 1.3.0 or higher (such as bidirectional 
   transmission or Stream ID)
   - **ALWAYS** set `SRTO_SENDER` to true on the sender side
- Require *HSv5*. If so, it must prevent connections to any older
versions of SRT by setting the minimum version 1.3.0 as shown above.

## Short Introduction to SRT Packet Structure

Every UDP packet carrying SRT traffic contains an SRT header (immediately after 
the UDP header). In all versions, the SRT header contains four major 32-bit fields:

 - `PH_SEQNO`
 - `PH_MSGNO`
 - `PH_TIMESTAMP`
 - `PH_ID`

Their interpretation depends on the type of packet, of which there are two: 
*control packets* and *data packets*, defined by the first bit in the `PH_SEQNO` 
field. 

Here, for example, is a representation of an SRT 1.3.0 **data packet header** 
(where the "packet type" bit = 0):


```
   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |0|                     Packet Sequence Number                  |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |FF |O|KK |R|                  Message Number                   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          Time Stamp                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                    Destination Socket ID                      |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```
**NOTE:** Packet diagrams in this document are in network bit order.

While a complete description of a data packet is out of scope for this document, 
here is a description of some other header fields unique to SRT:

- **FF** = (2 bits) Position of packet in message, where:
  - 10b = 1st
  - 00b = middle
  - 01b = last
  - 11b = single

- **O** = (1 bit) Indicates whether the message should be delivered in order (1) 
or not (0). In File/Message mode (original UDT with UDT_DGRAM) when this bit is 
clear then a message that is sent later (but reassembled before an earlier message 
which may be incomplete due to packet loss) is allowed to be delivered immediately, 
without waiting for the earlier message to be completed. This is not used in Live 
mode because there's a completely different function used for data extraction 
when TSBPD mode is on.

- **KK** = (2 bits) Indicates whether or not data is encrypted:
  - 00b: not encrypted
  - 01b: encrypted with even key 
  - 10b: encrypted with odd key

- **R** = (1 bit) Retransmitted packet. This flag is clear (0) when a packet is 
transmitted the very first time, and is set (1) if the packet is retransmitted.

In **Data** packets, the third and fourth fields are interpreted as follows:

- `PH_TIMESTAMP`: Usually the time when a packet was sent, although the real
interpretation may vary depending on the type, and it's not important for the
handshake
- `PH_ID`: The **Destination Socket ID** to which a packet should be dispatched, 
 although it may have the special value 0 when the packet is a connection request

Additional details for Data packets will be discussed in the sections below 
covering **extension flags**. 

An SRT control packet header ("packet type" bit = 1) has the following structure:

```
    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |1|          Message Type        |    Message Extended Type     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          Additional Data                      |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                            Time Stamp                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                    Destination Socket ID                      |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

For **Control** packets the first two fields are interpreted respectively (using 
network bit order) as:

 - `PH_SEQNO`:
   - Bit 0: packet type (set to 1 for control packet)
   - Bits 1-15: Message Type (see enum `UDTMessageType`)
   - Bits 16-31: Message Extended type
- `PH_MSGNO`: Additional data


The type subfields (in the `PH_SEQNO` field) are used in two ways:

1. The **Message Type** (`SEQNO_MSGTYPE`) is one of the values enumerated as 
`UDTMessageType`, except `UMSG_EXT`. In this case, the type is determined by 
this value only, and the **Message Extended Type** (`SEQNO_EXTTYPE`) value should 
always be 0.
2. The **Message Type** is `UMSG_EXT`. In this case the actual message type is 
contained in the **Message Extended Type**.

The **Extended Message** mechanism is theoretically open for further extensions. 
SRT uses some of them for its own purposes. This will be referred to later in the 
section on the **[SRT Extended Handshake](#the-srt-extended-handshake)**.

The `Additional Data` field (`PH_MSGNO`) is used in some control messages as 
extra space for data. Its interpretation depends on the particular message type. 
Handshake messages don't use it.

[Return to top of page](#srt-handshake)


## Handshake Structure

The handshake portion of a control packet, which comes immediately after the UDT 
header and SRT header, consists of the following 32-bit fields in order:

| Field             | Description                                                                                                                                             |
|:-----------------:|:--------------------------------------------------------------------------------------------------------------------------------------------------------|
| `Version`         | Contains number 4 in this version.                                                                                                                      |
| `Type`            | In SRT versions up to 1.2.0 (HSv4) must be the value of `UDT_DGRAM`, which is 2. For usage in later versions of SRT see the "Type field" section below. |
| `ISN`             | Initial Sequence Number; the sequence number for the first data packet                                                                                  |
| `MSS`             | Maximum Segment Size, which is typically 1500, but can be less                                                                                          |
| `FlightFlagSize`  | Maximum number of buffers allowed to be "in flight" (sent and not ACK-ed)                                                                               |
| `ReqType`         | Request type (see below)                                                                                                                                |
| `ID`              | The SOURCE socket ID from which the message is issued (target is in SRT header)                                                                         |
| `Cookie`          | Cookie used for various processing (see below)                                                                                                          |
| `PeerIP`          | Placeholder for the sender's IPv4 or IPv6 IP address, consisting of four 32-bit fields                                                                  |

Here is a representation of the HSv4 handshake structure (which follows immediately 
after the SRT control packet header):

```
   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                        UDT Version {4}                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          Socket Type                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                Initial Packet Sequence Number                 |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                      Maximum Packet Size                      |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                    Maximum Flow Window Size                   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                        Connection Type                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           Socket ID                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           SYN Cookie                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                        Peer IP Address                        |
   |                                                               |
   |                                                               |
   |                                                               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```
And here is the equivalent portion of the HSv5 handshake structure (to simplify 
the comparison here, the extended portion of the HSv5 handshake structure is not 
shown. See the [**"UDT Legacy" and "SRT Extended" Handshakes**](#the-udt-legacy-and-srt-extended-handshakes) 
section for details):

```
   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                        UDT Version {5}                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |       Encryption Flags        |        Extension Flags        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                Initial Packet Sequence Number                 |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                      Maximum Packet Size                      |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                    Maximum Flow Window Size                   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                        Connection Type                        |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           Socket ID                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           SYN Cookie                          |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                        Peer IP Address                        |
   |                                                               |
   |                                                               |
   |                                                               |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```


The HSv4 (UDT-legacy based) handshake is based on two rules:

1. The complete handshake process, which establishes the connection, is the same
as the UDT handshake.

2. The required SRT data interchange is done **after the connection is established**
using **SRT Extended Message** with the following Extended Types:

    - `SRT_CMD_HSREQ`/`SRT_CMD_HSRSP`, which exchange special SRT flags as well 
    as a latency value
	- `SRT_CMD_KMREQ`/`SRT_CMD_KMRSP` (optional), which exchange the wrapped 
	stream encryption key used with encryption (`KMRSP` is used only for 
	confirmation or error reporting)

**IMPORTANT:** There are two rules in the UDT code that continue to apply to SRT
version 1.2.0 and earlier, and therefore affect the prerequisites for any future
versions of the protocol:

1. The initial handshake response message coming from the Listener side **DOES
NOT REWRITE** the `Version` field (it's simply blindly copied from the
handshake request message received).

2. The size of the handshake message must be **exactly** equal to the legacy UDT
handshake structure, otherwise the message is silently rejected.

As of SRT version 1.3.0 with HSv5 the handshake must only satisfy the minimum
size. However, the code cannot rely on this until each peer is certain about
the SRT version of the other.

Even in HSv5, the **Caller** must first set two fields in the initial handshake
message:
- `Version` = 4
- `Type` = `UDT_DGRAM`

The version recognition relies on the fact that the **Listener** returns a
version of 5 (or potentially higher) if it is capable, but the **Caller** must 
set the `Version` to 4 to make sure that the Listener copies this value, which 
is how an HSv4 client is recognized. This allows SRT to handle the following 
combinations:

1. **HSv5 Caller vs. HSv4 Listener:** The Listener returns version 4 to the Caller,
so the Caller knows it should use HSv4, and then continues the handshake the old way.

2. **HSv4 Caller vs. HSv5 Listener:** The Caller sends version 4 and the Listener
returns version 5. The Caller ignores this value, however, and sends the
second phase of the handshake still using version 4. This is how the Listener
recognizes the HSv4 client.

3. **Both HSv5:** The Listener responds with version 5 (or potentially higher in
future) and the HSv5 Caller recognizes this value as HSv5 (or higher). The Caller
then initiates the second phase of the handshake according to HSv5
rules.

With **Rendezvous** there's no problem because both sides try to
connect to one another, so there's no copying of the handshake data. Each
side crafts its own handshake individually. If the value of the `Version`
field is 5 from the very beginning, and if there are any extension flags set in 
the `Type` field (see note below), the rules of HSv5 apply. But if one party is 
using version 4, the handshake continues as HSv4.

**NOTE**: Previously, the `Type` field contained only the extension flags, but 
now it also contains the encryption flag. So for HSv5 rules to apply the 
extension flag needs to be expressly set.

[Return to top of page](#srt-handshake)


## The "UDT Legacy" and "SRT Extended" Handshakes

### UDT Legacy Handshake

The first versions of SRT did not change anything in the UDT handshake mechanisms, 
which are identified as *HSv4*. Here the connection process is the same as it was 
in UDT, and any extended SRT handshake operations are done after the HSv4 handshake 
is established.

The HSv5 handshake was first introduced in SRT version 1.3.0. It includes all
the extended SRT handshake operations in the overall handshake process (known as 
"integrated handshake"), which means that these data are considered exchanged and 
agreed upon at the moment when the connection is established.

### Initiator and Responder

The addition of a new handshake mechanism necessitates the introduction of two 
new roles: "Initiator" and "Responder":


- **Initiator:** Starts the extended SRT handshake process and sends appropriate
SRT extended handshake requests

- **Responder:** Expects the SRT extended handshake requests to be sent by the
Initiator and sends SRT extended handshake responses back

There are two basic types of SRT handshake extensions that are exchanged in both
handshake versions (HSv5 introduces some more extensions):

- `SRT_CMD_HSREQ`: Exchanges the basic SRT information
- `SRT_CMD_KMREQ`: Exchanges the wrapped stream encryption key (used only if
encryption is requested)

The **Initiator** and **Responder** roles are assigned differently in *HSv4*
and *HSv5*.

For an *HSv4* handshake the assignments are simple:

- **Initiator** is the sender, which is the party that has set the `SRTO_SENDER`
socket option to *true*.
- **Responder** is the receiver, which is the party that has set `SRTO_SENDER` 
to *false* (default).

Note that these roles are independent of the connection mode 
(Caller/Listener/Rendezvous), and that the behavior is undefined if `SRTO_SENDER` 
has the same value on both parties.

For an **HSv5** handshake, the roles are dependent of the connection mode:

- For Caller-Listener connections:
   - the Caller is the **Initiator**
   - the Listener is the **Responder**
   
- For Rendezvous connections:
   - The **Initiator** and **Responder** roles are assigned based on the initial
data interchange during the handshake 
(see [**The Rendezvous Handshake**](#the-rendezvous-handshake) below)

Note that if the handshake can be done as HSv5, the connection is always
considered bidirectional and the `SRTO_SENDER` flag is unused.

[Return to top of page](#srt-handshake)


### The Request Type Field

The `ReqType` field in the **Handshake Structure** (see [above](#handshake-structure)) 
indicates the handshake message type.

**Caller-Listener Request Types:**

1. Caller to Listener: `URQ_INDUCTION`
2. Listener to Caller: `URQ_INDUCTION` (reports cookie)
3. Caller to Listener: `URQ_CONCLUSION` (uses previously returned cookie)
4. Listener to Caller: `URQ_CONCLUSION` (confirms connection established)

**Rendezvous Request Types:**

1. After starting the connection: `URQ_WAVEAHAND`
2. After receiving the above message from the peer: `URQ_CONCLUSION`
3. After receiving the above message from the peer: `URQ_AGREEMENT`.

Note that the **Rendezvous** process is different in HSv4 and HSv5, as the latter 
is based on a state machine.

In case when the connection process has failed when the party was about to
send the `URQ_CONCLUSION` handshake, this field will contain appropriate
error value. This value starts from 1000 (see `UDTRequestType` in `handshake.h`,
since `URQ_FAILURE_TYPES` symbol) added with the value of the rejection
reason (see `SRT_REJECT_REASON` in `srt.h`).

[Return to top of page](#srt-handshake)


### The Type Field

There are two possible interpretations of the `Type` field. The first is the 
legacy UDT "socket type", of which there are two: `UDT_STREAM` and `UDT_DGRAM` 
(in SRT only `UDT_DGRAM` is allowed). This legacy interpretation is applied in
the following circumstances:
 - in an `URQ_INDUCTION` message sent initially by the Caller
 - in an `URQ_INDUCTION` message sent back by the HSv4 Listener 
 - in an `URQ_CONCLUSION` message, if the other party was detected as HSv4
 
For more information on Induction and Conclusion see the 
[Caller-Listener Handshake](#the-caller-listener-handshake) section below.

UDT interpreted the `Type` field as either a **Stream** or **Message** type, 
and rejected the connection if the parties each used a different type. Since SRT 
only uses the **Message** type, HSv5 uses only the `UDT_DGRAM` value for this field 
in cases where the message is going to be sent to an HSv4 party (which follows 
the UDT interpretation).

In all other cases `Type` follows the HSv5 interpretation and consists of the
following:
- an upper 16-bit field (0 - 15) reserved for **encryption flags**
- a lower 16-bit field (16 - 31) reserved for **extension flags**

The **extension flags** field should have the following value:
 - in a `URQ_CONCLUSION` message, it should contain a combination
of extension flags (with the `HS_EXT_` prefix)
 - in a `URQ_INDUCTION` message sent back by the Listener it should contain
`SrtHSRequest::SRT_MAGIC_CODE` (0x4A17)
 - in all other cases it should be 0.

The **encryption flags** currently occupy only 3 out of 16 bits, which are used 
to advertise a value for `PBKEYLEN` (packet based key length). This value is taken
from the `SRTO_PBKEYLEN` option, divided by 8, giving possible values of:
 - 2 (AES-128)
 - 3 (AES-192)
 - 4 (AES-256)
 - 0 (PBKEYLEN not advertised)

The `PBKEYLEN` advertisement is required due to the fact that while the Sender
should decide the `PBKEYLEN`, in HSv5 the Sender might be the Responder. Therefore
`PBKEYLEN` is advertised to the Initiator so that it gets this value before it 
starts creating the SEK on its side, to be then sent to the Responder.

**REMINDER:** Initiator and Responder roles are assigned differently in HSv4 
and HSv5. See the **[Initiator and Responder](#initiator-and-responder)** 
section above.

The specification of `PBKEYLEN` is decided by the Sender. When the transmission 
is bidirectional, this value must be agreed upon at the outset because when both 
are set, the Responder wins. For Caller-Listener connections it is reasonable to 
set this value on the Listener only. In the case of Rendezvous the only reasonable 
approach is to decide upon the correct value from the different sources and to 
set it on both parties (note that **AES-128** is the default).

[Return to top of page](#srt-handshake)


## The Caller-Listener Handshake

This section describes the handshaking process where a Listener is
waiting for an incoming packet on a bound UDP port, which should be an SRT
handshake command (`UMSG_HANDSHAKE`) from a Caller. The process has two phases: 
*induction* and *conclusion*.


### The Induction Phase

The Caller begins by sending an "induction" message, which contains the following 
(significant) fields:

- **Version:** must always be 4
- **Type:** `UDT_DGRAM` (2)
- **ReqType:** `URQ_INDUCTION`
- **ID:** Socket ID of the Caller
- **Cookie:** 0

The **Destination Socket ID** (in the SRT header) in this message is 0, which is
interpreted as a connection request.

**NOTE:** This phase serves only to set a cookie on the Listener so that it 
doesn't allocate resources, thus mitigating a potential DOS attack that might be 
perpetrated by flooding the Listener with handshake commands.

An **HSv4** Listener responds with **exactly the same values**, except:

- **ID:** Socket ID of the HSv4 Listener
- **SYN Cookie:** a cookie that is crafted based on host, port and current time 
with 1 minute accuracy

An **HSv5** Listener responds with the following:

- **Version:** 5
- **Type:** 
  - Extension Field (lower 16 bits): `SrtHSRequest::SRT_MAGIC_CODE` 
  - Encryption Field (upper 16 bits): Advertised `PBKEYLEN`
- **ReqType:** (UDT Connection Type) `URQ_INDUCTION`
- **ID:** Socket ID of the HSv5 Listener
- **SYN Cookie:** a cookie that is crafted based on host, port and current time 
with 1 minute accuracy

**NOTE:** The HSv5 Listener still doesn't know the version of the Caller, and it
responds with the same set of values regardless of whether the Caller is 
version 4 or 5.

The important differences between HSv4 and HSv5 in this respect are:

1. The **HSv4** party completely ignores the values reported in `Version` and
`Type`.  It is, however, interested in the `Cookie` value, as this must be
passed to the next phase. It does interpret these fields, but only in the
"conclusion" message.

2. The **HSv5** party does interpret the values in `Version` and `Type`. If it
receives the value 5 in `Version`, it understands that it comes from an HSv5
party, so it knows that it should prepare the proper HSv5 messages in the next
phase.  It also checks the following in the `Type` field: 

	- whether the lower 16-bit field (extension flags) contains the magic 
value (see the **[Type Field](#the-type-field)** section above); otherwise the 
connection is rejected. This is a contingency for the case where someone who, 
in attempting to extend UDT independently, increases the `Version` value to 5 
and tries to test it against SRT. 

    - whether the upper 16-bit field (encryption flags) contain a non-zero
value, which is interpreted as an advertised `PBKEYLEN` (in which case it is
written into the value of the `SRTO_PBKEYLEN` option).

[Return to top of page](#srt-handshake)


### The Conclusion Phase

Once the Caller gets its cookie, it sends a `URQ_CONCLUSION` handshake
message to the Listener.

The following values are set by an HSv4 Caller. Note that the same values must
be used by an HSv5 Caller when the Listener has returned Version 4 in
its `URQ_INDUCTION` response:

- **Version:** 4
- **Type:** `UDT_DGRAM` (SRT must have this legacy UDT socket type only)
- **ReqType:** `URQ_CONCLUSION`
- **ID:** Socket ID of the Caller
- **Cookie:** the cookie previously received in the induction phase

If an HSv5 Caller receives a confirmation from a Listener that it can use the 
version 5 handshake, it fills in the following values:

- **Version:** 5
- **Type:** appropriate Extension Flags and Encryption Flags (see below)
- **ReqType:** `URQ_CONCLUSION`
- **ID:** Socket ID of the Caller 
- **Cookie:** the cookie previously received in the induction phase

The Destination Socket ID (in the SRT header, `PH_ID` field) in this message is the
socket ID that was previously received in the induction phase in the `ID` field
in the handshake structure.

The **Type** field contains:

- **Encryption Flags:** advertised `PBKEYLEN` (see above)
- **Extension Flags:** The `HS_EXT_` prefixed flags defined in `CHandShake` - see the
  **[SRT Extended Handshake](#the-srt-extended-handshake)** section below.

The Listener responds with the same values shown above, without the cookie (which 
isn't needed here), as well as the extensions for HSv5 (which will probably be 
exactly the same).

**IMPORTANT:** There isn't any "negotiation" here. If the values passed in the 
handshake are in any way not acceptable by the other side, the connection will 
be rejected. The only case when the Listener can have precedence over the Caller 
is the advertised `PBKEYLEN` in the `Encryption Flags` field in `Type` field. 
The value for latency is always agreed to be the greater of those reported 
by each party.

[Return to top of page](#srt-handshake)


## The Rendezvous Handshake

When two parties attempt to connect in **Rendezvous** mode, they are considered 
to be  equivalent: Both are connecting, but neither is listening, and they expect 
to be  contacted (over the same port number for both parties) specifically by 
the same party with which they are trying to connect. Therefore, it's perfectly 
safe to assume that, at  some point, each party will have agreed upon the 
connection, and that no induction-conclusion phase split is required. Even so, 
the Rendezvous handshake process is more complicated.

The basics of a Rendezvous handshake are the same in HSv4 and HSv5 - the 
description of the HSv4 process is a good introduction for HSv5. However, HSv5
has more data to exchange and more conditions to be taken into account. 

[Return to top of page](#srt-handshake)


### HSv4 Rendezvous Process

Initially, each party sends an SRT control message of type `UMSG_HANDSHAKE` to 
the other, with the following fields:

- **Version:** 4 (HSv4 only)
- **Type:** `UDT_DGRAM` (HSv4 only)
- **ReqType:** `URQ_WAVEAHAND`
- **ID:** Socket ID of the party sending this message
- **Cookie:** 0

When the `srt_connect()` function is first called by an application, each party
sends this message to its peer, and then tries to read a packet from its
underlying UDP socket to see if the other party is alive. Upon reception of an
`UMSG_HANDSHAKE` message, each party initiates the second (conclusion) phase by
sending this message:

- **Version:** 4
- **Type:** `UDT_DGRAM`
- **ReqType:** `URQ_CONCLUSION`
- **ID:** Socket ID of the party sending this message
- **Cookie:** 0

At this point, they are considered to be connected. When either party receives 
this message from its peer again, it sends another message with the `ReqType`
field set as `URQ_AGREEMENT`. This is a formal conclusion to the handshake
process, required to inform the peer that it can stop sending conclusion
messages (note that this is UDP, so neither party can assume that the message
has reached its peer).

With HSv4 there's no debate about who is the Initiator and who is the Responder 
because this transaction is unidirectional, so the party that has set the 
`SRTO_SENDER` flag is the Initiator and the other is Responder (as is usual 
with HSv4).

[Return to top of page](#srt-handshake)


### HSv5 Rendezvous Process

The HSv5 Rendezvous process introduces a state machine, and therefore is slightly 
different from HSv4, although it is still based on the same message request types. 
Both parties start with `URQ_WAVEAHAND` and use a `Version` value of 5. The 
version recognition is easy - the HSv4 client does not look at the `Version` value, 
whereas HSv5 clients can quickly recognize the version from the `Version` field.
The parties only continue with the HSv5 Rendezvous process when `Version` = 5
for both. Otherwise the process continues exclusively according to *HSv4* rules.

With HSv5 Rendezvous, both parties create a cookie for a process called a 
"cookie contest". This is necessary for the assignment of Initiator and Responder 
roles. Each party generates a cookie value (a 32-bit number) based on the host, 
port, and current time with 1 minute accuracy. This value is scrambled using
an MD5 sum calculation. The cookie values are then compared with one another.

Since you can't have two sockets on the same machine bound to the same device
and port and operating independently, it's virtually impossible that the
parties will generate identical cookies. However, this situation may occur if an 
application tries to "connect to itself" - that is, either connects to a local 
IP address, when the socket is bound to INADDR_ANY, or to the same IP address to 
which the socket was bound. If the cookies are identical (for any reason), the 
connection will not be made until new, unique cookies are generated (after a 
delay of up to one minute). In the case of an application "connecting to itself", 
the cookies will always be identical, and so the connection will never be made.

When one party's cookie value is greater than its peer's, it wins the cookie 
contest and becomes Initiator (the other party becomes the Responder).

At this point there are two "handshake flows" possible (at least theoretically):
*serial*  and *parallel*. 

#### Serial Handshake Flow

In the **serial** handshake flow, one party is always first, and the other follows.
That is, while both parties are repeatedly sending `URQ_WAVEAHAND` messages, at
some point one party - let's say Alice - will find she has received a 
`URQ_WAVEAHAND` message before she can send her next one, so she sends a 
`URQ_CONCLUSION` message in response. Meantime, Bob (Alice's peer) has missed 
her `URQ_WAVEAHAND` messages, and so Alice's `URQ_CONCLUSION` is the first message 
Bob has received from her.

This process can be described easily as a series of exchanges between the first 
and following parties (Alice and Bob, respectively):

1. Initially, both parties are in the *waving* state. Alice sends a handshake
message to Bob:
	- **Version:** 5
	- **Type:** Extension field: 0, Encryption field: advertised `PBKEYLEN`.
	- **ReqType:** `URQ_WAVEAHAND`
	- **ID:** Alice's socket ID
	- **Cookie:** Created based on host/port and current time

Keep in mind that while Alice doesn't yet know if she is sending this message to 
an HSv4 or HSv5 peer, the values from these fields would not be interpreted by 
an HSv4 peer when the **ReqType** is `URQ_WAVEAHAND`.

2. Bob receives Alice's `URQ_WAVEAHAND` message, switches to the *attention* 
state. Since Bob now knows Alice's cookie, he performs a "cookie contest" 
(compares both cookie values). If Bob's cookie is greater than Alice's, he will 
become the **Initiator**. Otherwise, he will become the **Responder**.

**IMPORTANT**: The resolution of the [Handshake Role](#initiator-and-responder) 
(Initiator or Responder) is essential to further processing. 

Then Bob responds:

- **Version:** 5
- **Type:**
   - *Extension field:* appropriate flags if Initiator, otherwise 0
   - *Encryption field:* advertised `PBKEYLEN`
- **ReqType:** `URQ_CONCLUSION`    

**NOTE:** If Bob is the Initiator and encryption is on, he will use either his
own `PBKEYLEN` or the one received from Alice (if she has advertised
`PBKEYLEN`).
	
3. Alice receives Bob's `URQ_CONCLUSION` message. While at this point she also 
performs the "cookie contest", the outcome will be the same. She switches to the 
*fine* state, and sends:

	- **Version:** 5
	- **Type:** Appropriate extension flags and encryption flags
	- **ReqType:** `URQ_CONCLUSION`

**NOTE:** Both parties always send extension flags at this point, which will
contain `SRT_CMD_HSREQ` if the message comes from an Initiator, or
`SRT_CMD_HSRSP` if it comes from a Responder. If the Initiator has received a
previous message from the Responder containing an advertised `PBKEYLEN` in the
encryption flags field (in the `Type` field), it will be used as the key length
for key generation sent next in the `SRT_CMD_KMREQ` block.

4. Bob receives Alice's `URQ_CONCLUSION` message, and then does one of the 
following (depending on Bob's role):

	- If Bob is the Initiator (Alice's message contains `SRT_CMD_HSRSP`), he:
		- switches to the *connected* state
		- sends Alice a message with `ReqType` = `URQ_AGREEMENT`, but containing 
		  no SRT extensions (*Extension flags* in `Type` should be 0)
		  
	- If Bob is the Responder (Alice's message contains `SRT_CMD_HSREQ`), he:
		- switches to *initiated* state
		- sends Alice a message with ReqType = `URQ_CONCLUSION` that also contains
		  extensions with `SRT_CMD_HSRSP`
        - awaits a confirmation from Alice that she is also connected (preferably 
          by `URQ_AGREEMENT` message)

5. Alice receives the above message, enters into the *connected* state, and 
then does one of the following (depending on Alice's role):

    - If Alice is the Initiator (received `URQ_CONCLUSION` with `SRT_CMD_HSRSP`), 
    she sends Bob a message with `ReqType` = `URQ_AGREEMENT`. 

    - If Alice is the Responder, the received message has `ReqType` = `URQ_AGREEMENT` 
    and in response she does nothing. 

6. At this point, if Bob was Initiator, he is connected already. If he was a 
Responder, he should receive the above `URQ_AGREEMENT` message, after which he
switches to the *connected* state. In the case where the UDP packet with the 
agreement message gets lost, Bob will still enter the *connected* state once
he receives anything else from Alice. If Bob is going to send, however, he
has to continue sending the same `URQ_CONCLUSION` until he gets the confirmation
from Alice.

[Return to top of page](#srt-handshake)


#### Parallel Handshake Flow

The serial handshake flow described above happens in almost every case.

There is, however, a very rare (but still possible) **parallel** flow that only 
occurs if the messages with `URQ_WAVEAHAND` are sent and received by both peers 
at precisely the same time. This *might* happen in one of these situations:

- if both Alice and Bob start sending `URQ_WAVEAHAND` messages perfectly 
simultaneously,  
or 
- if Bob starts later but sends his `URQ_WAVEAHAND` message during the 
gap between the moment when Alice had earlier sent her message,
and the moment when that message is received (that is, if each party receives the 
message from its peer immediately after having sent its own),  
or
- if, at the beginning of `srt_connect`, Alice receives the first message from 
Bob exactly during the very short gap between the time Alice is adding a socket 
to the connector list and when she sends her first `URQ_WAVEAHAND` message

The resulting flow is very much like Bob's behaviour in the serial handshake flow, 
but for both parties. Alice and Bob will go through the same state transitions:

    Waving -> Attention -> Initiated -> Connected

In the *Attention* state they know each other's cookies, so they can assign
roles. It is important to understand that, in contrast to serial flows,
which are mostly based on request-response cycles, here everything
happens completely asynchronously: the state switches upon reception
of a particular handshake message with appropriate contents (the
Initiator must attach the `HSREQ` extension, and Responder must attach the 
`HSRSP` extension). 

Here's how the parallel handshake flow works, based on roles:

**Initiator:**

1. `Waving`
   - Receives `URQ_WAVEAHAND` message 
   - Switches to `Attention`
   - Sends `URQ_CONCLUSION` + `HSREQ`
2. `Attention`
   - Receives `URQ_CONCLUSION` message, which:
     - contains no extensions:
       - switches to `Initiated`, still sends `URQ_CONCLUSION` + `HSREQ`
     - contains `HSRSP` extension: 
       - switches to `Connected`, sends `URQ_AGREEMENT`
3. `Initiated`
   - Receives `URQ_CONCLUSION` message, which:
     - Contains no extensions: 
       - REMAINS IN THIS STATE, still sends `URQ_CONCLUSION` + `HSREQ`
     - contains `HSRSP` extension: 
       - switches to `Connected`, sends `URQ_AGREEMENT`
4. `Connected`
   - May receive `URQ_CONCLUSION` and respond with `URQ_AGREEMENT`, but normally 
by now it should already have received payload packets.

**Responder:**

1. `Waving`
   - Receives `URQ_WAVEAHAND` message
   - Switches to `Attention`
   - Sends `URQ_CONCLUSION` message (with no extensions)
2. `Attention`
   - Receives `URQ_CONCLUSION` message with `HSREQ`  
     **NOTE:** This message might contain no extensions, in which case the party 
     shall simply send the empty `URQ_CONCLUSION` message, as before, and remain 
     in this state. 
   - Switches to `Initiated` and sends `URQ_CONCLUSION` message with `HSRSP`
3. `Initiated`
   - Receives:
     - `URQ_CONCLUSION` message with `HSREQ` 
       - responds with `URQ_CONCLUSION` with `HSRSP` and remains in this state
     - `URQ_AGREEMENT` message 
       - responds with `URQ_AGREEMENT` and switches to `Connected`
     - Payload packet 
       - responds with `URQ_AGREEMENT` and switches to `Connected`
4. `Connected`
    - Is not expecting to receive any handshake messages anymore. The
`URQ_AGREEMENT` message is always sent only once or per every final 
`URQ_CONCLUSION`message.

Note that any of these packets may be missing, and the sending party will
never become aware. The missing packet problem is resolved this way:

1. If the Responder misses the `URQ_CONCLUSION` + `HSREQ` message, it simply 
continues sending empty `URQ_CONCLUSION` messages. Only upon reception of 
`URQ_CONCLUSION` + `HSREQ` does it respond with `URQ_CONCLUSION` + `HSRSP`.

2. If the Initiator misses the `URQ_CONCLUSION` + `HSRSP` response from the 
Responder, it continues sending `URQ_CONCLUSION` + `HSREQ`. The Responder must 
always respond with `URQ_CONCLUSION` + `HSRSP` when the Initiator sends 
`URQ_CONCLUSION` + `HSREQ`, even if it has already received and interpreted it.

3. When the Initiator switches to the `Connected` state it responds with a
`URQ_AGREEMENT` message, which may be missed by the Responder. Nonetheless, the 
Initiator may start sending data packets because it considers itself connected 
- it doesn't know that the Responder has not yet switched to the `Connected` state. 
Therefore it is exceptionally allowed that when the Responder is in the `Initiated`
state and receives a data packet (or any control packet that is normally sent only 
between connected parties) over this connection, it may switch to the `Connected`
state just as if it had received a `URQ_AGREEMENT` message.

4. If the the Initiator is already switched to the `Connected` state it will not 
bother the Responder with any more handshake messages. But the Responder may be 
completely unaware of that (having missed the `URQ_AGREEMENT` message from the 
Initiator). Therefore it doesn't exit the connecting state (still blocks on 
`srt_connect` or doesn't signal connection readiness), which means that it 
continues sending `URQ_CONCLUSION` + `HSRSP` messages until it receives any 
packet that will make it switch to the `Connected` state (normally 
`URQ_AGREEMENT`). Only then does it exit the connecting state and the 
application can start transmission.

[Return to top of page](#srt-handshake)


### Rendezvous Between Different Versions

When one of the parties in a handshake supports HSv5 and the other only
HSv4, the handshake is conducted according to the rules described in the 
**[HSv4 Rendezvous Process](#hsv4-rendezvous-process)** section above.

Note, though, that in the first phase the `URQ_WAVEAHAND` request type sent
by the HSv5 party contains the `m_iVersion` and `m_iType` fields filled in as
required for version 5. This happens only for the "waving" phase, and fortunately
HSv4 clients ignore these fields. When switching to the conclusion phase, the
HSv5 client is already aware that the peer is HSv4 and fills the fields of the
conclusion handshake message according to the rules of HSv4.

[Return to top of page](#srt-handshake)


## The SRT Extended Handshake

### HSv4 Extended Handshake Process

The HSv4 extended handshake process starts **after the connection is considered
established**. Whatever problems may occur after this point *will only affect 
data transmission*.

Here is a representation of the HSv4 extended handshake packet structure 
(including the first four 32-bit segments of the SRT header):

```
   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |1|         Type=0x7fff         |    Ext {HSREQ(1),HSRSP(2)}    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                  Additional Info = undefined                  |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                       Time Stamp (µsec)                       |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                     Destination Socket ID                     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                     SRT Version {<10300h}                     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           SRT Flags                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |        TsbPd Resv = 0         |     TsbPdDelay {20..8000}     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                          Reserved = 0                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

The HSv4 extended handshake is performed with the use of the [aforementioned](#overview) 
"SRT Extended Messages", using control messages with major type `UMSG_EXT`.

Note that these command messages, although sent over an established connection, 
are still simply UDP packets. As such they are subject to all the problematic 
UDP protocol phenomena, such as packet loss (packet recovery applies exclusively 
to the payload packets). Therefore messages are sent "stubbornly" (with a
slight delay between subsequent retries) until the peer responds, with some
maximum number of retries before giving up. It's very important to understand
that the first message from an Initiator is sent at the same moment when the
application requests transmission of the first data packet. This data packet is
**not** held back until the extended SRT handshake is finished. The first
command message is sent, followed by the first data packet, and the rest of the
transmission continues without having the extended SRT handshake yet agreed
upon.

This means that the initial few data packets might be sent without having the 
appropriate SRT settings already working, which may raise two concerns:

- *There is a delay in the application of latency to received packets* - At first, 
packets are being delivered immediately. It is only when the `SRT_CMD_HSREQ` 
message is processed that latency is applied to the received packets. The 
time stamp based packet delivery mechanism (TSBPD) isn't working until then.

- *There is a delay in the application of encryption (if used) to received 
packets* - Packets can't be decrypted until the `SRT_CMD_KMREQ` is processed and 
the keys installed. The data packets are still encrypted, but the receiver can't
decrypt them and will drop them.

The codes for commands used are the same in HSv4 and HSv5 processes. In
HSv4 these are minor message type codes used with the `UMSG_EXT` command,
whereas in HSv5 they are in the "command" part of the extension block. The
messages that are sent as "REQ" parts will be repeatedly sent until they get
a corresponding "RSP" part, up to some timeout, after which they give up and
stay with a pure UDT connection.

[Return to top of page](#srt-handshake)


### HSv5 Extended Handshake Process

Here is a representation of the HSv5 **integrated** handshake packet structure
(without SRT header):

```
   0                   1                   2                   3
   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  ---
   |                        UDT Version {5}                        |   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   |
   |       Encryption Flags        |        Extension Flags        |   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   |
   |                Initial Packet Sequence Number                 |   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   |
   |                      Maximum Packet Size                      |   H
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   A
   |                    Maximum Flow Window Size                   |   N
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   D
   |                        Connection Type                        |   S
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   H
   |                           Socket ID                           |   A
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   K
   |                           SYN Cookie                          |   E
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   |
   |                        Peer IP Address                        |   |
   |                                                               |   |
   |                                                               |   |
   |                                                               |   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  ---
   |   Ext Type=SRT_CMD_HSREQ(1)   |         Ext Size {3}          |   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   H
   |                    SRT Version {>=10300h}                     |   S
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   R
   |                           SRT Flags                           |   E
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   Q
   |     RcvTsbPdDelay {20..8000}  |   SndTsbPdDelay {20..8000}    |   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  ---
   |    Ext Type=SRT_CMD_KMREQ(3)  |      Ext Size (bytes/4)       |   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   |
   |0| V{1} PT{2}|          Sign {2029h}           |  Resv {0}  |KK|   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   |
   |                           KEKI {0}                            |   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   |
   |  Cipher {2} |    Auth {0}     |     SE {2}    |   Resv1 {0}   |   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   |
   |           Recv2 {0}           | Slen(bytes)/4 | klen(bytes)/4 |   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   |
   |                           Salt[Slen]                          |   |
   |                                                               |   |
   |                                                               |   K
   |                                                               |   M
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   R
   |                   Wrap[((KK+1/2)*Klen) + 8]                   |   E
   |                                                               |   Q
   |                                                               |   |
   |                                                               |   |
   |                                                               |   |
   |                                                               |   |
   |                                                               |   |
   |                                                               |   |
   |                                                               |   |
   |                                                               |   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  ---
```

The **Extension Flags** subfield in the `Type` field in a conclusion handshake
message contains one of these flags:

- `HS_EXT_HSREQ`: defines SRT characteristic data; always present
- `HS_EXT_KMREQ`: if using encryption, defines encryption block
- `HS_EXT_CONFIG`: informs about having extra configuration data attached

The above schema shows the HSv5 packet structure, which can be split into
three parts:

1. The Handshake data part (up to "Peer IP Address" field)
2. The HSREQ extension
3. The KMREQ extension

Note that extensions are added only in certain situations (as described
above), so sometimes there are no extensions at all. When extensions are added,
the HSREQ extension is always present. The KMREQ extension is added only if
encryption is requested (the passphrase is set by the `SRTO_PASSPHRASE` socket
option). There might be also other extensions placed after HSREQ and KMREQ.

Every extension block has the following structure:

(1) a 16-bit command symbol  
(2) 16-bit block size (number of 32-bit words following this field)  
(3) a number of 32-bit fields, as specified in (2) above

What is contained in a block depends on the extension command code.

The data being received in the extension blocks in the conclusion message
undergo further verification. If the values are not acceptable, the
connection will be rejected. This may happen in the following situations:

1. The `Version` field contains 0. This means that the peer rejected the
handshake.

2. The `Version` field was higher than 4, but no extensions were added (no
extension flags set), while the rules state that they should be present. This is
considered an error in the case of a `URQ_CONCLUSION` message sent by the
Initiator to the Responder (there can be an initial conclusion message without
extensions sent by the Responder to the Initiator in Rendezvous connections).

3. Processing of any of the extension data has failed (also due to an internal
error).

4. Each side declares a transmission type that is not compatible with the
other. This will be described further, along with other new HSv5 features;
the HSv4 client supports only and exclusively one transmission type, which
is *Live*. This is indicated in the `Type` field in the HSv4 handshake, which 
must be equal to `UDT_DGRAM` (2), and in the HSv5 by the extra *Smoother* 
block declaration (see below). In any case, when there's no *Smoother*
declared, *Live* is assumed. Otherwise the Smoother type must be exactly
the same on both sides.

**NOTE:** The `TsbPd Resv` and `TsbPdDelay` fields both refer to latency,
but the use is different in HSv4 and HSv5.

In HSv4, only the lower 16 bits (`TsbPdDelay`) are used. The upper 16 bits
(`TsbPd Resv`) are simply unused. There's only one direction, so `HSREQ` is
sent by the Sender, `HSRSP` by the Receiver.  `HSREQ` contains only the Sender
latency, and `HSRSP` contains only the Receiver latency.

This is different from HSv5, in which the latency value for the sending
direction in the lower 16 bits (`SndTsbPdDelay`, 16 - 31 in network order) and
for receiving direction is placed in the upper 16 bits (`RcvTsbpdDelay`, 0 -
15). The communication is bidirectional, so there are two latency values, one
per direction. Therefore both HSREQ and HSREQ messages contain both the Sender
and Receiver latency values.

[Return to top of page](#srt-handshake)


### SRT Extension Commands

#### HSREQ and HSRSP

The `SRT_CMD_HSREQ` message contains three 32-bit fields designated as:

- `SRT_HS_VERSION`: string (0x00XXYYZZ) representing SRT version XX.YY.ZZ
- `SRT_HS_FLAGS`: the SRT flags (see below)
- `SRT_HS_LATENCY`: the latency specification

```
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                    SRT Version {>=10300h}                     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                           SRT Flags                           |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |(HSv4)  TsbPd Resv = 0         |     TsbPdDelay {20..8000}     |
   |(HSv5) RcvTsbPdDelay {20..8000}|   SndTsbPdDelay {20..8000}    |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

The flags (`SRT Flags` field) are the following bits, in order:

(0) `SRT_OPT_TSBPDSND`: The party will be sending in TSBPD (Time Stamp Based 
Packet Delivery) mode.

This is used by the Sender party to specify that it will use TSBPD mode.
The Responder should respond with its setting for TSBPD reception; if it isn't 
using TSBPD for reception, it responds with its reception TSBPD flag not set. 
In HSv4, this is only used by the Initiator.

(1) `SRT_OPT_TSBPDRCV`: The party expects to receive in TSBPD mode.

This is used by a party to specify that it expects to receive in TSBPD mode.
The Responder should respond to this setting with TSBPD sending
mode (HSv5 only) and set the sending TSBPD flag appropriately. In HSv4 this is 
only used by the Responder party.

(2) `SRT_OPT_HAICRYPT`: The party includes `haicrypt` (legacy flag).

This **special legacy compatibility flag** should be always set. See below 
for more details.

(3) `SRT_OPT_TLPKTDROP`: The party will do TLPKTDROP.

Declares the `SRTO_TLPKTDROP` flag of the party. This is important
because both parties must cooperate in this process. In HSv5, if both
directions are TSBPD, both use this setting. While it is not always
necessary to set this flag in live mode, it is the default and most
recommended setting.

(4) `SRT_OPT_NAKREPORT`: The party will do periodic NAK reporting.

Declares the `SRTO_NAKREPORT` flag of the party. This flag means
that periodic NAK reports will be sent (repeated `UMSG_LOSSREPORT`
message when the sender seems to linger with retransmission).

(5) `SRT_OPT_REXMITFLG`: The party uses the REXMIT flag.

This **special legacy compatibility flag** should be always set. See below 
for more details.

(6) `SRT_OPT_STREAM`: The party uses stream type transmission.

This is introduced in HSv5 only. When set, the party is using a stream
type transmission (file transmission with no boundaries). In HSv4 this
flag does not exist, and therefore it's always clear, which corresponds
to the fact that HSv4 supports Live mode only.

**Special Legacy Compatibility Flags**

The `SRT_OPT_HAICRYPT` and `SRT_OPT_REXMITFLG` fields define special cases for 
the interpretation of the contents in the SRT header for payload packets.

The SRT header contains an unusual field designated as `PH_MSGNO`,
which contains first some extra flags that occupy the most significant
bits in this field (the rest are assigned to the Message Number).
Some of these extra flags were already in UDT, but SRT added some
more by stealing bits from the Message Number subfield:

1. **Encryption Key** flags (2 bits). Controlled by `SRT_OPT_HAICRYPT`,
this field contains a value that declares whether the payload is
encrypted and with which key.

2. **Retransmission** flag (1 bit). Controlled by `SRT_OPT_REXMITFLG`, this flag 
is 0 when a packet is sent the first time, and 1 when it is retransmitted (i.e. 
requested in a loss report). When the incoming packet is late (one with a
sequence number older than the newest received so far), this flag allows the
Receiver to distinguish between a retransmitted packet and a reordered packet.
This is used by the "reorder tolerance" feature described in the API
documentation under `SRTO_LOSSMAXTTL` socket option.

As of version 1.2.0 both these fields are in use, and therefore both these
flags must always be set. In theory, there might still exist some SRT versions 
older than 1.2.0 where these flags are not used, and these extra bits remain 
part of the "Message Number" subfield.

In practice there are no versions around that do not use encryption bits, 
although there might be some old SRT versions still in use that do not include 
the Retransmission field, which was introduced in version 1.2.0. In practice 
both these flags must be set in the version that has them defined. They might 
be reused in future for something else, once all versions below 1.2.0 are 
decommissioned, but the default is for them to be set.

The `SRT_HS_LATENCY` field defines Sender/Receiver latency.

It is split into two 16-bit parts. The usage differs in HSv4 and HSv5.

In **HSv4** only the lower part (bits 16 - 31) is used. The upper part (bits 0 - 15) 
is always 0. The interpretation of this field is as follows:
 - Receiver party: Receiver latency
 - Sender party: Sender latency

In **HSv5** both 16-bit parts of the field are used, and interpreted 
as follows::
 - Upper 16 bits (0 - 15): Receiver latency
 - Lower 16 bits (16 - 31): Sender latency

The characteristics of Sender and Receiver latency are the following:

1. **Sender latency** is the minimum latency that the Sender wants the
Receiver to use.

2. **Receiver latency** is the (minimum) value that the Receiver
wishes to apply to the stream that it will be receiving.

Once these values are exchanged via the extended handshake, an **effective 
latency** is established, which is always the maximum of the two. Note that 
latency is defined in a specified direction. In HSv5, a connection is 
bidirectional, and a separate latency is defined for each direction.

The Initiator sends an `HSREQ` message, which declares the values on its side.
The Responder calculates the maximum values between what it receives in the 
`HSREQ`and its own values, then sends an `HSRSP` with the effective latencies.

Here is an example of an **HSv5 bidirectional transmission** between Alice and 
Bob, where Alice is Initiator:

1. Alice and Bob set the following latency values:

   - Alice: `SRTO_PEERLATENCY` = 250 ms, `SRTO_RCVLATENCY` = 550 ms
   - Bob: `SRTO_PEERLATENCY` = 500 ms, `SRTO_RCVLATENCY` = 300 ms

2. Alice defines the latency field in the HSREQ message:
```
    hs[SRT_HS_LATENCY] = { 250, 550 }; // { Lower, Upper }
```
3. Bob receives it, sets his options, and responds with `HSRSP`:
```
    SRTO_RCVLATENCY = max(300, 250);  //<-- 250:Alice's PEERLATENCY
    SRTO_PEERLATENCY = max(500, 550); //<-- 550:Alice's RCVLATENCY
    hs[SRT_HS_LATENCY] = { 550, 300 };
```
4. Alice receives this `HSRSP` and sets:
```
    SRTO_RCVLATENCY = 550;
    SRTO_PEERLATENCY = 300;
```
We now have the **effective latency** values:

   - For transmissions from Alice to Bob: 300ms
   - For transmissions from Bob to Alice: 550ms

Here is an example of an *HSv4* exchange, which is simpler because there's only 
one direction. We'll refer to Alice to Bob again to be consistent with the 
Initiator/Responder roles in the HSv5 example:

1. Alice sets `SRTO_LATENCY` to 250 ms

2. Bob sets `SRTO_LATENCY` to 300 ms

3. Alice sends `hs[SRT_HS_LATENCY] = { 250, 0 };` to Bob

4. Bob does `SRTO_LATENCY = max(300, 250);`

5. Bob sends `hs[SRT_HS_LATENCY] = {300, 0};` to Alice

6. Alice sets `SRTO_LATENCY` to 300

Note that the `SRTO_LATENCY` option in HSv5 sets both `SRTO_RCVLATENCY` and
`SRTO_PEERLATENCY` to the same value, although when reading, `SRTO_LATENCY`
is an alias to `SRTO_RCVLATENCY`.

Why is the Sender latency updated to the effective latency for that direction?
Because the `TLPKTDROP` mechanism, which is used by default in Live mode, may 
cause the Sender to decide to stop retransmitting packets that are known to be 
too late to retransmit. This latency value is one of the factors taken into 
account to calculate the time threshold for `TLPKTDROP`.

[Return to top of page](#srt-handshake)


#### KMREQ and KMRSP

`KMREQ` and `KMRSP` contain the KMX (key material exchange) message used for
encryption. The most important part of this message is the
AES-wrapped key (see the [Encryption documentation](encryption.md) for
details). If the encryption process on the Responder side was successful,
the response contains the same message for confirmation. Otherwise it's
one single 32-bit value that contains the value of `SRT_KMSTATE` type,
as an error status.

Note that when the encryption settings are different at each end, then
the connection is still allowed, but with the following restrictions:

- If the Initiator declares encryption, but the Responder does not, then
the Responder responds with `SRT_KM_S_NOSECRET` status. This means
that the Responder will not be able to decrypt data sent by the Initiator,
but the Responder can still send unencrypted data to the Initiator.

- If the Initiator did not declare encryption, but the Responder did, then
the Responder will attach `SRT_CMD_KMRSP` (despite the fact that the Initiator 
did not send `SRT_CMD_KMREQ`) with `SRT_KM_S_UNSECURED` status. The
Responder won't be able to send data to the Initiator (more precisely,
it will send scrambled data, not able to be decrypted), but the Initiator
will still be able to send unencrypted data to the Responder.

- If both have declared encryption, but have set different passwords,
the Responder will send a `KMRSP` block with an `SRT_KM_S_BADSECRET` value.
The transmission in both directions will be "scrambled" (encrypted and
not decryptable).

The value of the encryption status can be retrieved from the
`SRTO_SNDKMSTATE` and `SRTO_RCVKMSTATE` options. The legacy (or
unidirectional) option `SRTO_KMSTATE` resolves to `SRTO_RCVKMSTATE`
by default, unless the `SRTO_SENDER` option is set to *true*, in which
case it resolves to `SRTO_SNDKMSTATE`.

The values retrieved from these options depend on the result of the KMX
process:

1. If only one party declares encryption, the KM state will be one of 
the following:

   - For the party that declares no encryption:
     - `RCVKMSTATE: NOSECRET`
     - `SNDKMSTATE: UNSECURED`
     - Result: This party can send payloads unencrypted, but it can't
     decrypt packets received from its peer.

   - For the party that declares encryption:
     - `RCVKMSTATE: UNSECURED`
     - `SNDKMSTATE: NOSECRET`
     - Result: This party can receive unencrypted payloads from its peer, and 
     will be able to send encrypted payloads to the peer, but the peer won't 
     decrypt them.

2. If both declare encryption, but they have different passwords, then both 
states are `SRT_KM_S_BADSECRET`. In such a situation both sides may send payloads, 
but the other party won't decrypt them.

3. If both declare encryption and the password is the same on both sides, then 
both states are `SRT_KM_S_SECURED`. The transmission will be correctly performed 
with encryption in both directions.

Note that due to the introduction of the bidirectional feature in HSv5 (and
therefore the Initiator and Responder roles), the old HSv4 method of initializing
the crypto objects used for security is used only in one of the directions.
This is now called **"forward KMX"**:

1. The Initiator initializes its Sender Crypto (TXC) with preconfigured values.
The SEK and SALT values are random-generated.
2. The Initiator sends a KMX message to the Receiver.
3. The Receiver deploys the KMX message into its Receiver Crypto (RXC)

This is the general process of Security Association done for the
"forward direction", that is, when done by the Sender. However, as there's
only one KMX process in the handshake, in HSv5 this must also initialize 
the crypto in the opposite direction. This is accomplished by **"reverse KMX"**:

1. The Initiator initializes its Sender Crypto (TXC), like above, and then
**clones it** to the Receiver Crypto.
2. The Initiator sends a KMX message to the Responder.
3. The Responder deploys the KMX message into its Receiver Crypto (RXC)
4. The Responder initializes its Sender Crypto by **cloning** the Receiver
Crypto, that is, by extracting the SEK and SALT from the Receiver Crypto
and using them to initialize the Sender Crypto (clone the keys).

This way the Sender (being a Responder) has the Sender Crypto initialized in a 
manner very similar to that of the Initiator. The only difference is that the SEK
and SALT parameters in the crypto:
 - are random-generated on the Initiator side
 - are extracted (on the Responder side) from the Receiver Crypto, which was
configured by the incoming KMX message

The extra operations defined as "reverse KMX" happen exclusively in the HSv5 
handshake.

The encryption key (SEK) is normally configured to be refreshed after a 
predefined number of packets has been sent. To ensure the "soft handoff" 
to the new key, this process consists of three activities performed in order:

1. Pre-announcing of the key (SEK is sent by Sender to Receiver)
2. Switching the key (at some point packets are encrypted with the new key)
3. Decommissioning the key (removing the old, unused key)

Pre-announcing is done using an SRT Extended Message with the `SRT_CMD_KMREQ` 
extended type, where only the "forward KMX" part is done. When the transmission 
is bidirectional, the key refreshing process happens completely independently 
for each direction, and it's always initiated by the sending side, independently 
of Initiator and Responder roles (actually, these roles are significant only up 
to the moment when the connection is considered established).

The decision as to when exactly to perform particular activities belonging to 
the key refreshing process is made when the **number of sent packets** exceeds
a certain value (up to the moment of the connection or previous refresh), which 
is controlled by the `SRTO_KMREFRESHRATE` and `SRTO_KMPREANNOUNCE` options:

1. Pre-announce: when # of sent packets > `SRTO_KMREFRESHRATE - SRTO_KMPREANNOUNCE`
2. Key switch: when # of sent packets > `SRTO_KMREFRESHRATE`
3. Decommission: when # of sent packets > `SRTO_KMREFRESHRATE + SRTO_KMPREANNOUNCE`

In other words, `SRTO_KMREFRESHRATE` is the exact number of transmitted packets
for which a key switch happens. The Pre-announce happens `SRTO_KMPREANNOUNCE`
packets earlier, and Decommission happens `SRTO_KMPREANNOUNCE` packets later.
The `SRTO_KMPREANNOUNCE` value serves as an intermediate delay to make sure
that from the moment of switching the keys the new key is deployed on the 
Receiver, and that the old key is not decommissioned until the last
packet encrypted with that key is received.

The following activities occur when keys are refreshed:

1. **Pre-announce:** The new key is generated and sent to the Receiver
using the SRT Extended Message `SRT_CMD_KMREQ`. The received key is deployed
into the Receiver Crypto. The Receiver sends back the same message through
`SRT_CMD_KMRSP` as a confirmation that the refresh was successful (if it wasn't,
the message contains an error code).

2. **Key Switch:** The Encryption Flags in the `PH_MSGNO` field get toggled
between `EK_EVEN` and `EK_ODD`. From this moment on, the opposite (newly
generated) key is used.

3. **Decommission:** The old key (the key that was used with the previous flag
state) is decommissioned on both the Sender and Receiver sides. The place
for the key remains open for future key refreshing.

**NOTE** The handlers for `KMREQ` and `KMRSP` are the same for handling the 
request coming through an SRT Extended Message and through the handshake 
extension blocks, except that in case of the SRT Extended Message only one 
direction (forward KMX) is updated. HSv4 relies only on these messages, so 
there's no difference between initial and refreshed KM exchange. In HSv5 the 
initial KM exchange is done within the handshake in both directions, and then 
the key refresh process is started by the Sender and it updates the key for one
direction only.

[Return to top of page](#srt-handshake)


#### Congestion controller

This is a feature supported by HSv5 only. This adds functionality that has
existed in UDT as "Congestion control class", but implemented with SRT
workflows and requirements in mind. In SRT, the congestion control
mechanism must be set the same on both sides and is identified by
a character string. The extension type is set to `SRT_CMD_CONGESTION`.

The extension block contains the length of the content in 4-byte words.
The content is encoded as a string extended to full 4-byte chunks with
padding NUL characters if needed, and then inverted on each 4-byte mark.
For example, a "STREAM" string would be extended to `STREAM@@` and then
inverted into `ERTS@@MA` (where `@` marks the NUL character).

The value is a string with the name of the SRT Congestion Controller type. The
default one is called "live". The SRT 1.3.0 version contains an additional
optional Congestion Controller type called "file". Within the "file" Congestion
Controller it is possible to designate a stream mode and a message mode (the
"live" one may only use the message mode, with one message per packet).

This extension is optional and when not present the "live" Congestion Controller
is assumed. For an HSv4 party, which doesn't support this feature, it is always
the case.

The "file" type reintroduces the old UDT features for stream transmission 
(together with the `SRT_OPT_STREAM` flag) and messages that can span multiple UDP 
packets. The Congestion Controller controls the way the transmission is
handled, how various transmission settings are applied, and how to handle any
special phenomena that happen during transmission.

The "file" Congestion Controller is based completely on the original `CUDTCC`
class from UDT, and the rules for congestion control are completely copied from
there. However, it contains many changes and allows the selection of the
original UDT code in places that have been modified in SRT to support live
transmission.

[Return to top of page](#srt-handshake)


#### Stream ID (SID)

This feature is supported by HSv5 only. Its value is a string of
the user's choice that can be passed from the Caller to the Listener. The
symbol for this extension is `SRT_CMD_SID`.

The extension block for this extension is encoded the same way as described
for Congestion Controler above.

The Stream ID is a string of up to 512 characters that a Caller can pass to a
Listener (it's actually passed from an Initiator to a Responder in general, but
in Rendezvous mode this feature doesn't make sense). To use this feature, an
application should set it on a Caller socket using the `SRTO_STREAMID` option.
Upon connection, the accepted socket on the Listener side will have exactly the
same value set, and it can be retrieved using the same option. For more details
about the prospective use of this option, please refer to the
[API description document](API.md) and [SRT Access Control guidelines](AccessControl.md).


[Return to top of page](#srt-handshake)

