SRT Handshake
=============


Overview
--------

SRT is a connection protocol, and as such it embraces the concepts of "connection"
and "session". The UDP system protocol is used by SRT for sending data as well as
special control packets, also referred to as "commands".

An SRT connection is characterized by the fact that it is:

- first engaged by a *handshake* process
- maintained as long as any packets are being exchanged in a timely manner
- considered closed when the appropriate close command has been received, or the
connection has timed out

Just like its predecessor UDT, SRT supports two connection configurations:

1. Caller-Listener, where one side waits for the other to initiate a connection
2. Rendezvous, where both sides attempt to initiate a connection

As SRT development has evolved, two handshaking mechanisms have emerged:

1. the legacy UDT handshake, with the "SRT" part of the handshake implemented as 
the extended control messages; this is the only mechanism in SRT versions 1.2 and 
lower, and is known as *HSv4* (where the number 4 refers to the last UDT handshake 
version)
2. the new "integrated handshake", known as *HSv5*, where all the
required information concerning the connection is interchanged completely in the
handshake process

The version compatibility requirements are such that if one side of
the connection only understands *HSv4*, the connection is made according to
*HSv4* rules. Otherwise, if both sides are at SRT version 1.3.0 or greater, 
*HSv5* is used. As the new handshake supports several features that might be 
mandatory for
a particular application, it is also possible to reject an HSv4-to-HSv5 connection 
by setting the `SRTO_MINVERSION` socket option. The value for this option is an 
integer with the version encoded in hex, e.g.

    int req_version = 0x00010300; // 1.3.0
	srt_setsockflag(s, SRTO_MINVERSION, &req_version, sizeof(int));

**IMPORTANT:** If your application doesn't place the `SRTO_MINVERSION` restriction
on the other connection party, then not only must it not use any features
that require *HSv5*, but it must also comply with the SRT requirements associated 
with *HSv4*,
which means that it assumes only Live mode, in one direction, and the sender
party must set the `SRTO_SENDER` socket flag to `true`.


Short introduction to control packets
-------------------------------------

Every UDP packet carrying the SRT traffic contains the SRT header, which
consists of 4 following 32-bit major fields:

 - `PH_SEQNO`
 - `PH_MSGNO`
 - `PH_TIMESTAMP`
 - `PH_ID`

Their interpretation largely depends on what type of the packet is; only the `PH_ID`
field is always interpreted as "target socket ID", although this value may also be
a special value 0, which means that this is a connection request. `PH_TIMESTAMP`
is usually the time when the packet was sent, although its real interpretation may
vary depending on the type, and it's not important for the handshake. Most important
here are first two fields, which's interpretation mainly depends on the most significant
bit in the `PH_SEQNO` major field:

 - 1: Control packet
 - 0: Data (payload) packet

Details for Data packet will be discussed at **Extension flags**. For Control packet
these two fields are interpreted respectively as:

 - `PH_SEQNO`:
   - Bit 31: set
   - Bits 16-30: Message Type (see enum UDTMessageType)
   - Bits 0-15: Message Extended type
- `PH_MSGNO`: Additional data

The Additional Data field is used as extra place for data used by some control messages
and its interpretation depends on particular message type. At least the handshake messages
don't use it.

The type subfields (in `PH_SEQNO` field) have two major possibilities:

1. The Message Type is one of the values enumerated as UDTMessageType, except `UMSG_EXT`.
In this case, the type is determined by this value only, and the Message Extended Type
value should be always 0.
2. The Message Type is `UMSG_EXT`. In this case the actual message type is contained
in the Message Extended Type.

The Extended Message mechanism is theoretically open for further extensions, however
SRT uses some of them for its own purpose. This will be later referred to as "SRT
Extended Message".



Handshake Structure
-----------------------

The handshake structure contains the following 32-bit fields in order:

| Field             | Description                                                                                                                                             |
| :-------------:   |:--------------------------------------------------------------------------------------------------------------------------------------------------------|
| `Version`         | Contains number 4 in this version.                                                                                                                      |
| `Type`            | In SRT versions up to 1.2.0 (HSv4) must be the value of `UDT_DGRAM`, which is 2. For usage in later versions of SRT see the "Type field" section below. |
| `ISN`             | Initial Sequence Number; the sequence number for the first data packet                                                                                  |
| `MSS`             | Maximum Segment Size, which is typically 1500, but can be less                                                                                          |
| `FlightFlagSize`  | Maximum number of buffers allowed to be "in flight" (sent and not ACK-ed)                                                                               |
| `ReqType`         | Request type (see below)                                                                                                                                |
| `ID`              | The SOURCE socket ID to which the message is destined (target is in SRT header)                                                                         |
| `Cookie`          | Cookie used for various processing (see below)                                                                                                          |
| `PeerIP`          | (Four 32-bit fields) This field is a placeholder for the sender's IP address, either IPv4 or IPv6, but is not actually used for anything in HSv4        |



The HSv4 (UDT-legacy based) handshake is based on two rules:

1. The complete handshake process, which establishes the connection, is the same
as the UDT handshake.

2. The required SRT data interchange is done **after the connection is established**
using extension commands:

    - `HSREQ`, which exchanges special SRT flags as well as latency value
    - `KMREQ` (optional), which exchanges the wrapped stream encryption key, used
with encryption

**IMPORTANT:** There are two rules in the UDT code that continue to apply to SRT
version 1.2.0 and earlier:

1. The initial handshake response message **DOES NOT REWRITE** the `Version`
field (it's simply blindly copied from the handshake request message
received).

2. The size of the handshake message must be **exactly** equal to the legacy UDT
handshake structure, otherwise the message is rejected.

In SRT version 1.3.0 with HSv5 the handshake must only satisfy the minimum
size. However, the code cannot rely on this until each peer is certain about
the SRT version of the other.

Even in HSv5, the Caller must first set two fields:
- `m_iVersion` = 4
- `m_iType` = `UDT_DGRAM`

The version recognition relies  on the fact that the **Listener** returns a
version of 5 (or potentially higher) if it is capable, but the **Caller** must 
set the `m_iVersion` to 4 to make sure that the Listener copies this value,
which is how an HSv4 client is recognized. This allows SRT to handle the following 
combinations:

1. **HSv5 Caller vs. HSv4 Listener:** The Listener returns version 4 to the Caller,
so the Caller knows it should use HSv4, and then continues the handshake the old way.
2. **HSv4 caller vs. HSv5 listener:** The caller sends version 4 and the listener
returns version 5. The caller ignores this value, however, and sends the
second phase of the handshake still using version 4. This is how the listener
recognizes the HSv4 client.
3. **Both HSv5:** The listener responds with version 5 (or potentially higher in
future) and the HSv5 caller recognizes this value as HSv5 (or higher). The caller
then initiates the second phase of the handshake according to HSv5
rules.

With **Rendezvous** there's no problem because both sides try to
connect to one another, so there's no copying of the data handshake. Each
side crafts its own handshake individually. If the value of the `Version`
field is 5 from the very beginning, and the `Type` field is set, the rules of 
HSv5 apply. But if one party is using version 4, the handshake continues as HSv4.


The "UDT Legacy" and "SRT Extended" Handshakes
-------------------------------------------------------

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

The handshake change necessitates the introduction of two new terms: "Initiator" 
and "Responder", which are interpreted differently in HSv4 and HSv5 handshakes. 
These names are assigned to particular sides of the connection, depending on how
the SRT extensions in the UDT handshake are being handled.

For an **HSv4** handshake the assignments are simple: the
**Initiator** is the side that has set the `SRTO_SENDER` flag, and the **Responder** 
is the other side. Not that this is **_independent_** of the Caller and Listener 
roles. Effectively, the Initiator side is simultaneously a data sender, and this 
side will start the extended handshake process as soon as any data are sent. This 
is the reason why this flag must always be set on the Sender if the application
is intended to be used with HSv4-only clients.

For an **HSv5** handshake, the Caller-Listener vs. Rendezvous handshake processes 
are defined differently (the latter is described in the **HSv5 rendezvous** section 
below). For Caller-Listener handshakes it's simple: the *Caller* is the **Initiator**, 
the *Listener* is the **Responder**.

The roles of Initiator and Responder are the following:

- **Initiator:** starts the extended SRT handshake process and sends appropriate
request messages (`SRT_CMD_HSREQ` and optionally `SRT_CMD_KMREQ`)
- **Responder:** expects the above messages to be sent by the Initiator and sends
response messages back (`SRT_CMD_HSRSP` and optionally `SRT_CMD_KMRSP`).

These two are always interchanged in both versions of the handshake, although
HSv5 is capable of adding more.

Request Type
----------------

There are sections within the handshake command packet that are indicated by 
various `RequestType` field values:

**Caller-Listener Request Types:**

1. Caller to Listener: `URQ_INDUCTION`
2. Listener to Caller: `URQ_INDUCTION` (reports cookie)
3. Caller to Listener: `URQ_CONCLUSION` (uses previously returned cookie)
4. Listener to Caller: `URQ_CONCLUSION` (confirms connection established)

**Rendezvous Request Types:**

1. The very first of two parties that happens to send anything to its peer sends: 
`URQ_WAVEAHAND`. This is referred to as *waving*. 
2. The party that has received anything from its peer sends `URQ_CONCLUSION`. 
The peer may respond with another ` URQ_CONCLUSION`, as needed. 
3. When a party is satisfied with what has been sent in a `URQ_CONCLUSION` request, 
it sends `URQ_AGREEMENT` to inform its peer that the Request Type is already
established and it can stop sending.

Note that the **Rendezvous** process is different in HSv4 and HSv5, as the latter 
is based on a state machine.


The Type field
--------------

There are two possible interpretations of the `m_iType` field. The first is the 
legacy UDT "socket type", of which there are two: `UDT_STREAM` and `UDT_DGRAM` 
(in SRT only `UDT_DGRAM` is allowed). This legacy interpretation is applied in
the following circumstances:
 - In `URQ_INDUCTION` message sent initially by the caller
 - In `URQ_INDUCTION` message sent back by the HSv4 listener 
 - In `URQ_CONCLUSION` message, if the party was detected as HSv4

In all other cases `m_iType` uses the HSv5 interpretation and consists of the
following:
- an upper 16-bit field reserved for **encryption flags**
- a lower 16-bit field reserved for **extension flags**

The **extension flags** field has the following characteristics:
 - In `URQ_INDUCTION` sent back by the Listener it should contain
`SrtHSRequest::SRT_MAGIC_CODE` (0x4A17)
 - In `URQ_CONCLUSION` should contain appropriate set of `HS_EXT_*` flags 
 (discussed below)
 - In all other cases it should be 0.

The **encryption flags** currently occupy only 3 bits and they comprise a
declaration of "advertised PB key length". Whichever site declares the
`PBKEYLEN` will be advertising it. Therefore this will contain the value taken
from the `SRTO_PBKEYLEN` option, divided by 8, so the possible values are:
 - 2 (AES-128)
 - 3 (AES-192)
 - 4 (AES-256)
 - 0 (PBKEYLEN not advertised; either no encryption, or agent is considered
Receiver)

The `PBKEYLEN` advertisement is required due to the fact that the Sender
should decide the `PBKEYLEN`, but in HSv5 it will not necessarily be an Initiator
(unlike HSv4). In order to allow the Responder to specify its own
`PBKEYLEN`, this field is used to advertise it before the Initiator starts
creating a `KMREQ` message.

When a Caller advertises `PBKEYLEN`, it has no effect on the handshake; 
the Caller will just create a `KMREQ` message with this key length. When the
Listener advertises `PBKEYLEN`, however, it will be taken by the Caller as the 
agreed-upon value. In such a situation the Caller should not have set its own 
`PBKEYLEN`; `PBKEYLEN` should be set by the machine declared as Sender. Similarly, 
in Rendezvous, if one side is a Sender, but not an Initiator, the advertised 
`PBKEYLEN` will provide this information before it starts preparing its KMREQ 
message.


The Caller-Listener Handshake
-----------------------------

This section describes the handshaking process where a Listener is
waiting for an incoming packet on a bound UDP port, which should be an SRT
handshake command (`UMSG_HANDSHAKE`) from a Caller. The process has two phases: 
*induction* and *conclusion*.


### The Induction Phase

The Caller begins by sending an "induction" message, which contains the following 
(significant) fields:

- **Version:** must always contain 4
- **Type:** `UDT_DGRAM` (2)
- **ReqType:** `URQ_INDUCTION`
- **ID:** Agent's socket ID
- **Cookie:** 0

The target socket ID (in the SRT header) in this message is 0, which is
interpreted as a connection request.

**NOTE:** This phase serves only to set a cookie on the Listener so that it 
doesn't allocate resources, thus mitigating a potential DOS attack that might be 
perpetrated by flooding the Listener with handshake commands.

An **HSv4** Listener responds with the following:

- **Version:** value copied from the handshake request command (that is, 4)
- **Type:** value copied from handshake request command (that is, `UDT_DGRAM`, 2)
- **ReqType:** `URQ_INDUCTION`
- **ID:** Agent's socket ID
- **Cookie:** a cookie that is crafted based on host, port and current time with 
1 minute accuracy

An **HSv5** Listener responds with the following:

- **Version:** 5
- **Type: Lower 16:** `SrtHSRequest::SRT_MAGIC_CODE`. Upper 16: Advertised `PBKEYLEN`
- **ReqType:** `URQ_INDUCTION`
- **ID:** Agent's socket ID
- **Cookie:** a cookie that is crafted based on host, port and current time with 
1 minute accuracy

(Note: The HSv5 listener still doesn't know the version of the caller and it
responds the same set of values regardless if the caller is version 4 or 5.)

The important differences between HSv4 and HSv5 in this respect are:

1. The **HSv4** client completely ignores the values reported in `Version` and `Type`.
It is, however, interested in the `Cookie` value, as this must be passed to
the next phase. It does interpret these fields, but only in the "conclusion"
message.

2. The **HSv5** client does interpret the values in `Version` and `Type`. If it
receives the value 5 in `Version`, it understands that it comes from an HSv5 client, 
so it knows that it should prepare the proper HSv5 messages in the next phase. 
It also checks the following in the `Type` field:
	- whether the lower 16-bit field (extension flags) contains the magic
value, otherwise the connection is rejected. This is a contingency for the case
where someone who, in attempting to extend UDT independently, increases the
`Version` value to 5 and tries to test it against SRT.
    - whether the upper 16-bit field (encryption flags) contain a nonzero
value, which is interpreted as an advertised `PBKEYLEN` (in which case it is
written into the value of the `SRTO_PBKEYLEN` option).

Note that if both parties set `PBKEYLEN`, this results in unwanted fallback behavior, 
where the value advertised by the Listener wins. In order to prevent this from 
happening, do not set the `SRTO_PBKEYLEN` option on the side that is intended to 
be a Receiver. If you want to have an encrypted bidirectional transmission, the 
best way would be to give the Listener the sole ability to set this value; 
in Rendezvous you would have to remember to set exactly the same value of
`PBKEYLEN` on both sides.

### The Conclusion Phase

The HSv4 Caller sends an "conclusion" message, which contains the following 
(significant) fields:

- **Version:** 4
- **Type:** `UDT_DGRAM` (as the type of the socket, UDT legacy, SRT must have this
one only)
- **ReqType:** `URQ_CONCLUSION`
- **ID:** Agent's socket ID
- **Cookie:** a cookie previously received in the induction phase

The HSv5 Caller must send exactly these contents when it has received a value
of 4 in the `Version` field from the Listener, and continue the processing 
according to HSv4 rules. When it receives a `Version` value of 5, it sends:

- **Version:** 5
- **Type:** Appropriate Extension Flags and Encryption Flags (see below)
- **ReqType:** `URQ_CONCLUSION`
- **ID:** Your socket ID
- **Cookie:** a cookie previously received in the induction phase

The target socket ID (in the SRT header) in this message is the socket ID that
was previously received in the induction phase in the `ID` field in the handshake
structure.

The **Type** field contains:
    - Encryption Flags: advertised PBKEYLEN (see above)
    - Extension Flags: The `HS_EXT_` prefixed flags defined in `CHandShake`, see 
**SRT Extended Handshake** section below.

The Listener responds with the same values shown above, without the cookie (which 
isn't needed here), as well as the extensions for HSv5 (which will be probably be 
exactly the same).

**IMPORTANT:** There isn't any "negotiation" here. If the
values passed in the handshake are in any way not acceptable by the other side,
the connection will be rejected. The only case when the Listener can
have precedense over the Caller is the advertised `PBKEYLEN` in the
`Encryption Flags` field in `Type` field. The value for latency is always
agreed to be the greater of those reported by each party.

The Rendezvous Handshake
------------------------

When two parties attempt to connect in Rendezvous mode, they are considered to be 
equivalent. Both have bound ports (with the same port number). And when they do 
connect, neither of them is listening (each is expecting to receive packets from 
the other), which means that it's perfectly safe to assume each party has agreed 
upon the connection, and that no induction-conclusion phase split is required as 
a safety precaution. Even so, the Rendezvous handshake process is more complicated.

The Rendezvous process is similar in HSv4 and HSv5, although for HSv5 the whole
process is defined separately due to the introduction of an extra state
machine. You can consider the HSv4 process description to be an introduction 
for HSv5.

### HSv4 Rendezvous Process

In the beginning, both parties are sending an SRT control message to each other,
of type `UMSG_HANDSHAKE` with the following fields:

- **Version:** 4 (HSv4 only)
- **Type:** `UDT_DGRAM` (HSv4 only)
- **ReqType:** `URQ_WAVEAHAND`
- **ID:** Agent's socket ID
- **Cookie:** 0

When the `srt_connect()` function is first called by the
application each party sends this message to its peer, and then tries to read
a packet from its underlying UDP socket to see if the other party is alive.
Upon reception of an `UMSG_HANDSHAKE` message, each party initiates
the second, conclusion phase by sending this message:

- **Version:** 4
- **Type:** `UDT_DGRAM`
- **ReqType:** `URQ_CONCLUSION`
- **ID:** Your socket ID
- **Cookie:** 0

At this point, they are considered to be connected. When either party receives 
this message from its peer again, it sends another message with the `ReqType`
field set as `URQ_AGREEMENT`. This is a formal conclusion to the handshake
process, required to inform the peer that it can stop sending conclusion
messages (note that this is UDP, so neither party can assume that the message
has reached its peer).

With HSv4 there's no debate about who is the Initiator and who is the Responder 
because  this transaction is unidirectional, so the party that has set the 
`SRTO_SENDER` flag is the Initiator and the other is Responder (as is usual 
with HSv4).

### HSv5 Rendezvous Process

This process introduces a state machine and therefore is slightly different 
from HSv4, although it is still based on the same message request types. Both 
parties start with `URQ_WAVEAHAND` and use a `Version` value of 5. The version 
recognition is easy -- the HSv4 client does not look at the `Version` value, 
whereas HSv5 clients can quickly recognize the version from the `Version` field.
The parties only continue with the HSv5 Rendezvous process when `Version` = 5
for both.

With HSv5 Rendezvous, both parties create a cookie. This is necessary
for the Initiator and Responder roles assignment -- a process called a 
"cookie contest". Each party generates a cookie value (a 32-bit number) that 
can be compared with one another. The only requirement is that both must use 
the same port number. Since you can't have two sockets on the same machine 
bound to the same device and port and operating independently), it's virtually 
impossible that the parties will generate identical cookies. Nonetheless, there 
is a simple fallback for when they appear to be equal, which is that the 
connection will not be made until the cookies are regenerated (which should 
happen after a minute or so). 

When one party's cookie value is greater than its peer's, it wins the cookie 
contest and becomes Initiator (the other party becomes the Responder).

At this point there are two conditions possible (at least theoretically):
*parallel*  and *serial*. 

The **parallel** condition only occurs if the messages with `URQ_WAVEAHAND` are 
sent and received by both peers at precisely the same time (with microsecond 
accuracy), and both machines have also spent exactly the same amount of time on 
network data processing, so neither has exited the initial state at the moment
they sent the message. Although the state machine predicts its possibility, 
the parallel condition is so unlikely it isn't even possible to cause it to 
happen with current testing methods.

In the **serial** condition, one party is always first,
and the other follows. That is, while both parties are repeatedly sending 
`URQ_WAVEAHAND` messages, at some point one will find it has received an 
`URQ_WAVEAHAND` message before it can send its next one.


This means that the **very first** message
sent by the "following" party will be the one with `ReqType` equal
to `URQ_CONCLUSION`. This process can be described easily as a series of exchanges 
between the first and following parties (Alice and Bob, respectively):


1. Initially, both parties are in the *waving* state. Alice sends a handshake
message to Bob:
	- **Version:** 5
	- **Type:** Extension field: 0, Encryption field: advertised `PBKEYLEN`.
	- **ReqType:** `URQ_WAVEAHAND`
	- **ID:** Alice's socket ID
	- **Cookie:** Created based on host/port and current time

(Note: Consider that this message is sent to a party that isn't yet known if
it is HSv4 or HSv5; however the values from these fields are not interpreted
by HSv4 client when the **ReqType** is `URQ_WAVEAHAND`).

2. Bob receives Alice's `URQ_WAVEAHAND` message, switches to the *attention* state.
As now Bob knows Alice's cookie, he performs the "cookie context" (compares
both cookie values) and depending on whether his cookie is less or greater, it
will become Responder or Initiator respectively. **IMPORTANT**: the Handshake
Role resolution (being Initiator or Responder) is essential for the further
processing. Then Bob responds:
	- **Version:** 5
	- **Type:**
        - *Extension field:* appropriate flags, if initiator, otherwise 0
        - *Encryption field:* advertised `PBKEYLEN`
	- **ReqType:** `URQ_CONCLUSION`
    
    **NOTE:** If Bob is the initiator and encryption is on, he will
      use either his own `PBKEYLEN` or the one received from Alice (if
      she has advertised `PBKEYLEN`).
	
3. Alice receives Bob's `URQ_CONCLUSION` message, switches to the *fine* state
(note that only at that point does she have a chance to perform the "cookie
contest" on her side), and sends:
	- **Version:** 5
	- **Type:** Appropriate extension flags and encryption flags
	- **ReqType:** `URQ_CONCLUSION`

It is important to note here that both parties always send extension flags at 
this point, which will contain `SRT_CMD_HSREQ` if the message comes from an 
Initiator, or `SRT_CMD_HSRSP` if it comes from a Responder. If the Initiator has 
received a previous message from the Responder containing an advertised `PBKEYLEN` 
in the encryption flags field (in the `Type` field), it will be used as the key 
length for preparing the `SRT_CMD_KMREQ` block.

4. Bob receives Alice's `URQ_CONCLUSION` message, and then does one of the 
following (depending on Bob's role):
	- If Bob is the Initiator (Alice's message contains `SRT_CMD_HSRSP`)
		- switches to the *connected* state
		- sends Alice a message with ReqType = `URQ_AGREEMENT`
		- the message contains no SRT extensions (`Type` should be 0)
	- If Bob is the Responder (Alice's message contains `SRT_CMD_HSREQ`)
		- switches to *initiated* state
		- sends Alice a message with ReqType = `URQ_CONCLUSION`
		- the message contains extensions with `SRT_CMD_HSRSP`

5. Alice receives the above message and enters into the *connected* state, and 
then does one of the following (depending on Alice's role):
    - If Alice is the Initiator (received `URQ_CONCLUSION` with `SRT_CMD_HSRSP`),
she sends Bob a message with `ReqType` = `URQ_AGREEMENT`. 
    - If Alice is the Responder, the received message has `ReqType` = `URQ_AGREEMENT`
and in response she does nothing. 

6. At this point, if Bob was Initiator, he is connected already. If he was a 
Responder, he should receive the above `URQ_AGREEMENT` message, after which he
switches to the *connected* state. In the case where the UDP packet with the 
agreement message gets lost, Bob will still enter the *connected* state once
he receives anything else from Alice, or when the application requests him to 
send any data.

### The parallel arrangement considerations

The handshake flow described above happens in so-called "99% of cases" and
it's called "serial arrangement". It relies on the fact that one of the peers
is sending `URQ_WAVEAHAND` messages in small time distances as it waits for
any response from the peer. Most often it happens, however, that at the moment
when the peer starts, it misses the previous handshake message, and it has to
take time until the agent sends the handshake message again. As the peer
starts, it sends the `URQ_WAVEAHAND` hanshake message, which is interpreted by
the agent immediately and the agent therefore won't send any more "waveahands",
but it responds with `URQ_CONCLUSION`. In result, the peer will never receive
neither one `URQ_WAVEAHAND` message from the agent.

There is, however, possible the "parallel arrangement" where both parties
start at more-less the same time, or it can be also possible when the second
party starts at the exactly perfect moment just a few microseconds before
the first one sends the `URQ_WAVEAHAND` message. The second party will still
start with sending `URQ_WAVEAHAND`, but this way it may simply receive the
`URQ_WAVEAHAND` message "out of band" (the message not being a response for
anything).

This is a very rare case, although possible, and therefore it's also handled.
The resulting flow is very much like Bob party described above for both parties.
Both parties are then passing the same state transitions:

    Attention -> Initiated -> Connected

In Attention state they know each other's cookies, so they can assign the
roles. Important to understand is that, in contradiction to serial arrangement,
where the flow is mostly based on request-response cycles, here everything
happens completely asynchronously: the state switching happens upon reception
of particular handshake message with appropriate contents, that is, the
Initiator must attach the HSREQ extension, and Responder must attach HSRSP
extension. Let's then describe the flow in complete, separately for Initiator
and Responder:

Initiator:

1. Waving. Receives Waveahand. Switches to `Attention`. Sends `URQ_CONCLUSION` + HSREQ.
2. Attention. Receives Conclusion, which:
    - Contains no extensions: Switches to `Initiated`, still sends `URQ_CONCLUSION` + HSREQ
    - Contains HSRSP extension: Switches to `Connected`, send `URQ_AGREEMENT`
3. Initiated. Receives Conclusion, which
    - Contains no extensions: REMAINS IN THIS STATE, still sends `URQ_CONCLUSION` + HSREQ
    - Contains HSRSP extension: Switches to `Connected`, send `URQ_AGREEMENT`
4. Connected: may receive Conclusion, respond with `URQ_AGREEMENT`, but normally since
now it should receive Payload packets already.

Responder:

1. Waving. Receives Waveahand. Switches to `Attention`. Sends `URQ_CONCLUSION` (no extensions).
2. Attention. Receives Conclusion with HSREQ (note: there might be some potential
possibility that this message contains no extensions, in which case the party shall
simply send the empty Conclusion, as before, and remain in this state). Switches to
`Initiated` and sends Conclusion with HSRSP.
3. Initiated. Receives:
    - Conclusion with HSREQ - respond with Conclusion+HSRSP and remain in this state.
    - `URQ_AGREEMENT` message - respond with `URQ_AGREEMENT` and switch to `Connected`
    - A Payload packet - respond with `URQ_AGREEMENT` and switch to `Connected`
4. Connected: it's not expected to receive any handshake messages anymore. The
`URQ_AGREEMENT` message is sent always only once or per every final Conclusion
message.

Note that each of these packets may be missing, of which the sending party is never
aware, so here this problem is resolved this way:

1. When the Responder missed the Conclusion+HSREQ message, it simply continues
sending empty Conclusion. Only upon reception of Conclusion+HSREQ does it respond
with Conclusion+HSRSP.
2. When the Initiator missed the Conclusion+HSRSP response from Responder, it
continues sending Conclusion+HSREQ. The Responder must respond this way always
when the Initiator sent Conclusion+HSREQ, no matter if it has already received
it once and interpreted.
3. When Initiator turned into Connected state and responded with Agreement,
which has been missed, it may start sending data packets because it considers
itself connected, but it doesn't know that the Responder did not switch into
Connected state yet. Therefore it is exceptionally allowed that from Initiated
state a received data packet (or even any other control packet that is normally
sent only between connected sites) for this connection may switch it to Connected
state as good as the Agreement message.
4. The problem might be when the Responder is going to send data, the Initiator
is already switched to Connected and sees no reason of bothering the Responder
with any more handshake, but the Responder is completely unaware of that.
Therefore until it switches to Connected, it doesn't exit the connecting state
(still blocks on `srt_connect` or doesn't signal connection readiness), which
means that it continues sending Conclusion+HSRSP, until it receives any packet
that will make it switch to Connected state, normally Agreement. Only then does
it exit the connecting state and the application is allowed to start transmission.


### Rendezvous Between Different Versions

When one of the parties in a handshake supports HSv5 and the other only
HSv4, the handshake is conducted according to the rules described
in the **HSv4 Rendezvous Process** section (above).

Note though that in the first phase the `URQ_WAVEAHAND` request type sent
by the HSv5 client contains the `m_iVersion` and `m_iType` fields filled as
required for version 5. This happens only for the "waving" phase, and fortunately
HSv4 clients ignore these fields. When switching to the conclusion phase, the
HSv5 client is already aware that the peer is HSv4 and fills the fields of the
conclusion handshake message according to the rules of HSv4.




The SRT Extended Handshake
--------------------------

### HSv4 Extended Handshake Process

The HSv4 extended handshake process starts **after the connection is considered
established**. Whatever problems may occur after this point *will only affect 
data transmission*.

Here the handshake is performed with the use of aforementioned "SRT Extended
Messages", using control messages with major type `UMSG_EXT`.


Note that these command messages, although sent over an established connection, 
are still simply UDP packets and as such they are subject to all the problematic 
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

- *There is a delay in the application of latency to recevied packets* -- At first, 
packets are being delivered immediately. It is only when the `SRT_CMD_HSREQ` 
message is processed that latency is applied to the received packets (the 
TSBPD mechanism isn't working until then).

- *There is a delay in the receiving application of encryption (if used)* --
Packets can't be decrypted until the `SRT_CMD_KMREQ` is processed and the
keys installed. The data packets are still encrypted, but the receiver can't
decrypt them and will drop them.

The codes for commands used are the same in HSv4 and HSv5 processes. In
HSv4 these are minor message type codes used with the `UMSG_EXT` command,
whereas in HSv5 they are in the "command" part of the extension block. The
messages that are sent as "REQ" parts will be repeatedly sent until they get
a corresponding "RSP" part, up to some timeout, after which they give up and
stay with a pure UDT connection.


### HSv5 Extended Handshake Process

The **Extension Flags** subfield in the `Type` field in a conclusion handshake
message contains one of these flags:

- `HS_EXT_HSREQ`: defines SRT characteristic data; practically always present
- `HS_EXT_KMREQ`: if using encryption, defines encryption block
- `HS_EXT_CONFIG`: informs about having extra configuration data attached

The extension block has the following structure:

- 16-bit command symbol
- 16-bit block size (number of 32-bit words following this field)
- followed by any number of 32-bit fields of a given size

What is contained in a block depends on the extension command code.

The data being received in the extension blocks in the conclusion message
undergo further verification. If the values are not acceptable, the
connection will be rejected. This may happen in the following situations:

1. The `Version` field contains 0. This means that the peer rejected the
handshake.

2. The `Version` field was higher than 4, but no extensions were added (size not 
larger than in an HSv4 handshake, or no extension flags present). This is
only in case of Caller-Listener arrangment because in Rendezvous such a
situation may still happen temporarily.

3. Processing of HSREQ/KMREQ message has failed (e.g. due to an internal error)

4. Each side declares a transmission type that is not compatible with the
other. This will be described further along with other new HSv5 features;
the HSv4 client supports only and exclusively one transmission type, which
is *Live*. This is indicated in the `Type` field in the HSv4 handshake that
shall be equal to `UDT_DGRAM` (2), and in the HSv5 by the extra *Smoother*
block declaration (see below). In any case, when there's no *Smoother*
declared, *Live* is assumed. Otherwise the Smoother type must be exactly
the same on both sides.


### SRT Extension Commands

#### HSREQ and HSRSP

The `SRT_CMD_HSREQ` message contains three 32-bit fields designated as:

- `SRT_HS_VERSION`: the 0x00XXYYZZ, encodes the XX.YY.ZZ SRT version
- `SRT_HS_FLAGS`: the SRT flags (see below)
- `SRT_HS_LATENCY`: the latency specification

The flags are the following bits, in order:

(0) `SRT_OPT_TSBPDSND` : The party will be sending in TSBPD (Timestamp-based 
Packet Delivery) mode.

This is used by the Sender party to specify that it will use TSBPD mode.
The Responder should respond with its setting for TSBPD reception; if it isn't 
using TSBPD for reception, it responds with its reception TSBPD flag not set. 
In HSv4, this is only used by the Initiator.

(1) `SRT_OPT_TSBPDRCV` : The party expects to receive in TSBPD mode.

This is used by a party to specify that it expects to receive in TSBPD mode.
The Responder should respond to this setting with TSBPD sending
mode (HSv5 only) and set the sending TSBPD flag appropriately. In HSv4 this is 
only used by the Responder party.

(2) `SRT_OPT_HAICRYPT` : The party includes haicrypt (legacy flag).

This flag should be always set. It's a **Special legacy compatibility flag**,
see below for more details.

(3) `SRT_OPT_TLPKTDROP`: The party will do TLPKTDROP.

Declares the `SRTO_TLPKTDROP` flag of the party. This is important
because both parties must cooperate in this process. In HSv5, if both
directions are TSBPD, both use this setting. This flag must always be
 set in live mode.

(4) `SRT_OPT_NAKREPORT`: The party will do periodic NAK reporting.

Declares the `SRTO_NAKREPORT` flag of the party. This flag means
that periodic NAK reports will be sent (repeated `UMSG_LOSSREPORT`
message when the sender seems to linger with retransmission).

(5) `SRT_OPT_REXMITFLG`: The party uses the REXMIT flag.

This flag should be always set. It's a **Special legacy compatibility flag**,
see below for more details.

(6) `SRT_OPT_STREAM`   : The party uses stream type transmission

This is introduced in HSv5 only. When set, the party is using a stream
type transmission (file transmission with no boundaries). In HSv4 this
flag does not exist and therefore it's always clear, which corresponds
with the fact that HSv4 supports the Live mode only.

**Special legacy compatibility flags**

`SRT_OPT_HAICRYPT` and `SRT_OPT_REXMITFLG` define special cases of
interpretation of the contents in the SRT header for payload packets.

The SRT header contains an unusual field designated as `PH_MSGNO`,
which contains first some extra flags that occupy the most significant
bits in this field, and the rest is assigned to the Message Number.
Some of these extra flags were already in UDT, but SRT added some
more by stealing more bits from the Message Number subfield:

1. Encryption Key flags (2 bits). Controlled by `SRT_OPT_HAICRYPT`,
this field contains a value that declares as to whether the payload is
encrypted and with which key.

2. Retransmission flag (1 bit). Controlled by `SRT_OPT_REXMITFLG`,
this flag is clear when the payload was sent originally and set,
when the payload was retransmitted.

As of 1.2.0 version, both these fields are in use, and therefore both these
flags must be always set. At least in theory there might still exist some SRT
versions older than 1.2.0 where these flags are not used, and these extra bits
are part of the "Message Number" subfield.

In practice there are no versions around that do not use
encryption bits, although there might be some old SRT versions still
in use that do not include the Retransmission field, which was introduced
in version 1.2.0. In practice both these flags must be set in the
version that has them defined. They might be reused in future for
something else, once all versions below 1.2.0 are decommissioned,
but the default values for them is to be set.

**Sender/Receiver Latency** (`SRT_HS_LATENCY` field)

The latency specification is split into two 16-bit fields. The usage
differs in HSv4 and HSv5.

In HSv4 only the Lower part is used (the Higher part is always 0), and the
meaning is:
 - On the Receiver party: Receiver latency
 - On the Sender party: Sender latency

In HSv5 both fields are used:
 - Higher: Receiver latency
 - Lower: Sender latency

The characteristics of Sender and Receiver latency are the following:

1. **Sender latency** is the minimum latency that the sender decided that the
receiver should set.

2. **Receiver latency** is the value that the Receiver wishes to apply to
the stream that it will be receiving.

The **Effective Latency** is always the maximum value of these two. The
exchange of these values is such that the Initiator sends the HSREQ message
declares them on its side, the Responder "fixes" them by calculating the
maximum value of the received ones and the corresponding its own ones,
then it sends HSRSP with the effective values.

As this can be really confusing, here is the detailed explanation how
it works in particular handshake version:

1. HSv4. There's only one direction, and the party that set `SRTO_SENDER`
is declared a sender, the party that did not set it is the receiver. There's
only one option, `SRTO_LATENCY`, which sets the "sender latency" on the sender
and "receiver latency" on the receiver. Only the lower field is used in this
exchange, that is, the meaning of this field in HSv4 is "sender latency" for
the sender party and "receiver latency" for the receiver party.

2. HSv5. This is bidirectional-capable, so the latency setting is
per direction. Let's imagine two boxes, Alice and Bob:

 - when Alice sends a stream to Bob, then Alice sets `SRTO_PEERLATENCY` and Bob sets
   `SRTO_RCVLATENCY`; this value on Alice is then placed into the Lower field
   and the value to be set on Bob is placed to the Higher field
 - what is placed by Alice in the Higher field is the value from `SRTO_RCVLATENCY`,
   and so the value placed by Bob into Lower field is from `SRTO_PEERLATENCY`,
   and this latency touches upon, this time, the stream that is sent from
   Bob to Alice

(Note that `SRTO_LATENCY` option on HSv5 sets both `SRTO_RCVLATENCY` and
`SRTO_PEERLATENCY` to the same value, although when reading, `SRTO_LATENCY`
is an alias to `SRTO_RCVLATENCY`).

No matter the direction, the Initiator sets both of these values, then the
Responder "fixes" them (chooses the maximum between the received value
and its own) and sends them back (in HSv5 in reverse order). It can be
symbolically shown as:

Filling the handshake:

On HSv4
    alice.hsreq.latency = { alice[peerlatency], 0 };
    bob.hsreq.latency = { bob[rcvlatency], 0 };

On HSv5 (both Alice and Bob do the same):

    alice.hsreq.latency = { alice[peerlatency], alice[rcvlatency] };

Fixing the latency value (note that it is simplified by assigning to two
variables in one expression, but obviously both assignments happen separately
on every endpoint):

    alice[peerlatency] = bob[rcvlatency] = max(alice[peerlatency], bob[rcvlatency]);

(On Hsv5 also):

    alice[rcvlatency] = bob[peerlatency] = max(alice[rcvlatency], bob[peerlatency]);

If you wonder, why the sender latency should be updated to the effective
latency for that direction, it's because the TLPKTDROP mechanism, which is
normally on in the live mode, may cause that the sender decide to stop
retransmitting packets, which are known to be too late to retransmit anyway
(even if they are sent, the receiver will drop them anyway because it has
already forgotten them, agreed to drop them, and continue delivery of the
packets that follow it). This value is needed to decide whether there is still
time to recover the packet, or it's already too late.

#### KMREQ and KMRSP

This message contains the KMX (key material exchange) message
used for encryption. The most important part of this message is the
AES-wrapped key (see the [Encryption documentation](encryption.md) for
details). If the encryption process on the Responder side was successful,
the response contains the same message for confirmation, otherwise it's
one single 32-bit value that contains the value of `SRT_KMSTATE` type,
as an error status.

Note that when the encryption settings are different at each end, then
the connection is still allowed, but with the following restrictions:

- If the Initiator declares encryption, but the Responder does not, then
the Responder responds KMRSP with `SRT_KM_S_NOSECRET` status. This means
that the Responder will not be able to decrypt data sent by the Initiator,
but the Responder can still send unencrypted data to the Initiator.
- If the Initiator did not declare encryption, but the Responder did, then
the Responder will attach `SRT_CMD_KMRSP` (despite the fact that the Initiator 
did not send `SRT_CMD_KMREQ`) with `SRT_KM_S_UNSECURED` status.
- If both have declared encryption, but have set different passwords,
the Responder will send a KMRSP block with `SRT_KM_S_BADSECRET` value.

The value of the encryption status can be retrieved from the
`SRTO_SNDKMSTATE` and `SRTO_RCVKMSTATE` options. The legacy (or
unidirectional) option `SRTO_KMSTATE` resolves to `SRTO_RCVKMSTATE`
by default, unless the `SRTO_SENDER` option is set to true, in which
case it resolves to `SRTO_SNDKMSTATE`.

The values retrieved from these options depend on the result of the KMX
process:

1. If only one party declared encryption, the KM state will be one of the following:
- For the party that declares no encryption:
   - RCVKMSTATE: NOSECRET
   - SNDKMSTATE: UNSECURED
   - Result: This party can send payloads unencrypted, but it can't
     decrypt packets received from its peer.
- For the party that declares encryption:
   - RCVKMSTATE: UNSECURED
   - SNDKMSTATE: NOSECRET
   - Result: This party can receive unencrypted payloads from its
     peer, and will be able to send encrypted payloads to the
     peer, but the peer won't decrypt them.

2. If both declare encryption, but they have a different password,
then both states are `SRT_KM_S_BADSECRET`. In such a situation both
sides may send payloads, but the other party won't decrypt it.

3. If both declare encryption and the password is the same on both
sides, then both states are `SRT_KM_S_SECURED`. The transmission
shall be correctly performed with encryption in both directions.

Note that due to introducing the bidirectional feature in HSv5 (and therefore
the Initiator and Responder roles), the "old way" (HSv4) method of initializing
the crypto objects used for security is used only in one of the directions.
This is now called "forward KMX":

1. The Initiator initializes its Sender Crypto (TXC) with preconfigured values.
2. The Initiator sends a KMX message to the receiver.
3. The Receiver deploys the KMX message into its Receiver Crypto (RXC)

This process is the general process of Security Association done for the
"forward direction", that is, when done by the Sender. However as there's
only one KMX process in the handshake, in HSv5 this must also initialize 
the cryptos in the opposite direction. This is accomplished by "reverse KMX":

1. The Initiator initializes its Sender Crypto (TXC), and **clones it** to the
Receiver Crypto.
2. The Initiator sends KMX message to the Responder.
3. The Responder deploys the KMX message into its Receiver Crypto (RXC), and
**clones it** to its Sender Crypto.

This way the Sender (being a Responder) has the Sender Crypto initialized in a 
manner very similar to that of Initiator. The only difference is that the SEK
and SALT parameters in the crypto:
 - In Initiator, they are taken from Random Number Generator
 - In Responder, they are extracted from the Receiver Crypto, which was
configured by the incoming KMX message

The extra operations defined as "reverse KMX" happen exclusively
in the HSv5 handshake.

The encryption key (SEK) is normally configured to be refreshed after
a predefined number of packets has been sent. In order to ensure the
"soft handoff" to the new key, this process consists of three following
activities performed in that order:
 - Preannouncing of the key (SEK is sent by Sender to Receiver)
 - Switching the key (at some point packets are encrypted with the new key)
 - Decommissioning the key (remove the old, unused key)

The Preannouncing part is being done using the SRT Extended Message with
`SRT_CMD_KMREQ` extended type. This time only the "forward KMX" part is done.
When the transmission is bidirectional, the key refreshing process happens
completely independently on each direction, and it's always initiated by the
sending side, independently on Initiator and Responder roles (actually these
roles are significant only up to the moment when the connection is considered
established).

The decision as to when exactly to do particular activities belonging to
the key refreshing process is made when the **number of sent packets** exceeds
appropriate value (towards the moment of the connection or previous
refreshing), which is controlled by the `SRTO_KMREFRESHRATE` and
`SRTO_KMPREANNOUNCE` options:

1. Pre-announce: when reaches `SRTO_KMREFRESHRATE - SRTO_KMPREANNOUNCE`
2. Key switch: when reaches `SRTO_KMREFRESHRATE`
3. Decommission: when reaches `SRTO_KMREFRESHRATE + SRTO_KMPREANNOUNCE`

(In other words, `SRTO_KMREFRESHRATE` is the exact number of transmitted packets
for which Key-switch happens, the Pre-announce happens `SRTO_KMPREANNOUNCE`
packets earlier, and Decommission happens `SRTO_KMPREANNOUNCE` packets later.)

The following things are done for the refreshing activities:

1. **Pre-announce:** the new key is generated and sent to the receiver
using the SRT Extended Message `SRT_CMD_KMREQ`. The received key is deployed
into the Receiver Crypto. The Receiver sends back the same message through
`SRT_CMD_KMRSP` as a confirmation that the refreshing was successful, or
the message contains an error code, if it wasn't.
2. **Key Switch:** The Encryption Flags in the `PH_MSGNO` field get toggled
between `EK_EVEN` and `EK_ODD`. From this moment on, the opposite (newly
generated) key is used.
3. **Decommission:** the old key (the key that was used with the previous flag
state) is decommissioned on both the Sender and Receiver sides. The place
for the key remains open for future key refreshing.

**NOTE** In the implementation the handlers for KMREQ and KMRSP are
the same for handling the request coming through the SRT Extended Message
and through the handshake extension blocks, except that in case of SRT Extended
Message only one direction is updated (forward KMX only). HSv4 relies only
on these messages, so there's no difference between initial and refreshed
KM exchange. In HSv5 the initial KM exchange is done within the handshake
in both directions, and then the key refreshing updates the key for one
direction only.


#### Smoother

This is a feature supported by HSv5 only. This value contains a string
with the name of the SRT Smoother type.

The versions that support HSv5 contain an additional functionality, similar to
the "congestion control" class from UDT, which is called "Smoother". The
default Smoother is called "live" and the 1.3.0 version contains an additional
optional smoother type caller "file". Within the "file" smoother there's also a
possible transmission of stream mode and message mode (the "live" smoother may
only use the message mode and with one message per packet).

The smoother part is not obligatory when not present. The "live" Smoother
is used by default. For an HSv4 client, which doesn't contain this feature, the
Smoother type is assumed to be "live".

The "file" Smoother type, the second built-in one, reintroduced the old
UDT features for stream transmission (together with the `SRT_OPT_STREAM` flag)
and messages that can span through multiple UDP packets. The Smoothers
differ in the way the transmission is handled, how various transmission
settings are applied, and how to handle any special phenomena that happen
during transmission.

The "file" Smoother is based completely on the original `CUDTCC` class from UDT
and the rules for congestion control are completely copied from there. It
contains however much more changes and allows to select the original UDT code
in places that must have been modified in SRT in order to support the live
transmission.


#### Stream ID (SID)

This feature is supported by HSv5 only. This value contains a string of
the user's choice that can be passed from the Caller to the Listener.

The Stream ID is a string up to 512 characters that an Initiator can pass to
a Responder. To use this feature, an application should set it on a Caller
socket using `SRTO_STREAMID` option. Upon connection, the accepted socket
on the listener side will have the exactly same value set and it can be
retrieved using the same option. This gives the listener a chance to decide
what to do with this connection - such as to decide which stream to send
in the case where the Listener is the stream Sender. This feature is not
predicted to be used for Rendezvous connection, as it hardly makes sense.

