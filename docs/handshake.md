SRT Handshake
=============


Overview
--------

SRT is a connection protocol and as such it maintains the term of connection
and session. UDP system protocol is used here for both sending data and
special control packets known otherwise as "commands".

The connection is emulated by the fact that:

- it's first engaged by the *handshake* process
- it's considered maintained as long as any packets have been sent lately
- it's considered closed when appropriate close command was received, or the
connection is timed out

SRT, just like it's legacy and predecessor UDT, supports two connection
arrangements:

- the caller-listener configuration, when one side waits for the other to connect
- the rendezvous configuration, when both sides connect to one another

During the development, there have been created two methods of making a handshake
in SRT:

- the legacy UDT handshake with "SRT part" of the handshake done as the
extended control messages, known as *HSv4* (number 4 is the last version
number in UDT handshake); this was up to SRT version 1.2
- the new "integrated handshake", known as *HSv5* (number 5 was chosen to allow the
new version to distinguish the handshake version of the other party), where all the
required information concerning the connection is interchanged completely in the
handshake process

The version compatibility requirements are such that if at least one side of
the connection understands only *HSv4*, the connection is made according to
*HSv4* rules, otherwise (if both are at least version 1.3.0) *HSv5* is used. As
the new handshake supports several features that might be mandatory for
particular application, there's also a possibility to reject connection, if the
other party is too old by setting `SRTO_MINVERSION` socket option. The value
for this option is an integer with the version encoded in hex, e.g.

    int req_version = 0x00010300; // 1.3.0
	srt_setsockflag(s, SRTO_MINVERSION, &req_version, sizeof(int));

Important thing is that if your application doesn't put that restriction
on the other connection party, then not only must it not use any features
that require *HSv5*, but it must also alignt to the requirement of *HSv4* SRT,
which means that it assumes only Live mode, in one direction, and the sender
party must set the `SRTO_SENDER` socket flag to true.



The handshake structure
-----------------------

The handshake structure contains the following 32-bit fields in order:

- Version
	- Contains number 4 in this version.
- Type
    - In SRT up to 1.2.0 (HSv4) must be the value of `UDT_DGRAM`, which is 2.
	- In SRT higher versions it will be explained below in "Type field" chapter
- ISN
	- Initial Sequence Number, the sequence number for the first data packet
- MSS
	- Maximum Segment Size, which is typically 1500, but can be less
- FlightFlagSize
	- Maximum number of buffers allowed to be "in flight" (sent and not ACK-ed)
- ReqType
	- request type (see below)
- ID
	- the SOURCE socket ID to which the message is destined (target is in SRT
header)
- Cookie
	- Cookie used for various processing (see below)
- PeerIP (4 32-bit fields)
	- This field is prepared to keep the sender's IP address, either IPv4 or IPv6.
This field isn't actually used for anything

The HSv4 (UDT-legacy based) handshake bases on two rules:

- The complete handshake process, which establishes the connection, is the same
as the UDT handshake.
- The required SRT data interchange is done **after the connection is established**
using the extension commands.

There are two things that are done as SRT handshake extension with the
extension commands:

- HSREQ, which exchanges special SRT flags as well as latency value
- KMREQ (optional), which exchanges the wrapped stream encryption key, used
when you use encryption

There are two important rules in the UDT code that have lasted up to SRT
version 1.2.0:

1. The initial handshake response message **DOES NOT REWRITE** the Version
field (it's simply blindly copied from the handshake requrest message
received).

2. The size of the handshake message must be **exactly** equal to the legacy UDT
handshake structure, otherwise the message is rejected.

In version 1.3.0 and HSv5 handshake the handshake must only satisfy the minimum
size, however the code cannot rely on this until both peers are certain about
the version of one another.

The caller then, even in HSv5, must first set the fields:
- `m_iVersion` = 4
- `m_iType` = `UDT_DGRAM`

The version recognition relies then on the fact that the **listener** return
version 5 (or potentially higher) if it is capable, but the caller must set
version 4 there to catch the listener that blindly copies this value back,
which is how HSv4 client is recognized. The following combination then are
handled:

1. HSv5 caller vs. HSv4 listener: The listener returns version 4 to the caller,
so the caller knows this is HSv4 and then continues the handshake the old way.
2. HSv4 caller vs. HSv5 listener: The caller sends version 4 and the listener
returns with version 5. The caller ignores this value, however, and sends the
second phase handshake still with version 4 - this is how the listener
recognizes the HSv4 client.
3. Both HSv5: the listener responds with version 5 (or potentially higher in
future) and HSv5 caller recognizes this value as HSv5 (or higher). The caller
prepares then the second phase of the handshake already according to HSv5
rules.

With Rendezvous there's no such problem because in this case both sides try to
connect to one another, so there's no copying of the data handshake - each
party crafts its own handshake individually. In this case the value of Version
field is 5 since the very beginning, and Type field is set also according to
the rules of HSv5, unless the other party responds Version 4, in which case
the handshake continues with version 4.



The UDT legacy handshake and the SRT extended handshake
-------------------------------------------------------

### The UDT legacy

The first versions of SRT did not change anything in the handshake process. These
versions are identified as *HSv4*. Here the connection process is the same as it
was in UDT, and any extended SRT handshake things are being done afterwards.

In the version 1.3.0 there was introduced the HSv5 handshake, which includes all
the extended SRT parts in the overall handshake process (known as "integrated
handshake"), which means that these data are considered exchanged and agreed
upon at the moment when the connection is considered established.

### Initiator and responder

Due to the handshake change, there must have been the terms of initiator and
responder introduced, which are interpreted differently in HSv4 and HSv5
handshake. This concerns how the SRT extension towards the UDT handshake are
being handled, and these names are assigned to particular sides of the
connection.

For HSv4 handshake the assignment of Initiator and Responder are simple: The
Initiator is the side that has set the `SRTO_SENDER` flag, and the Responder is
the other side, **independently** on the Caller and Listener roles. Effectively
the Initiator side is simultaneously a data sender, and this side will start
with the extended handshake process once any data are started to be sent. This
is the reason why this flag must be set on sender always if the application
predicts to be used with HSv4-only clients.

For HSv5 handshake this is defined differently for caller-listener arrangement and
for rendezvous arrangement. How it is defined for rendezvous, it will be described
at **HSv5 rendezvous** handshake process. For caller-listener arrangement it's also
simple: the caller is initiator, the listener is responder.

The roles of Initiator and Responder are the following:

- Initiator starts the extended SRT handshake process and sends appropriate
request messages (`SRT_CMD_HSREQ` and optionally `SRT_CMD_KMREQ`)
- Responder expects the above messages to be sent by the Initiator and sends
response messages back (`SRT_CMD_HSRSP` and optionally `SRT_CMD_KMRSP`).

These two are always interchanged in both versions of the handshake, although
HSv5 is capable of adding more.


The Request type
----------------

Within the handshake command packet there are also various phases of the
handshake, which are marked by appropriate RequestType field value. Various
values are used in various arrangements:

### Caller-listener request types

1. Caller to Listener: `URQ_INDUCTION`
2. Listener to Caller: `URQ_INDUCTION` (reports cookie)
3. Caller to Listener: `URQ_CONCLUSION` (uses previously returned cookie)
4. Listener to Caller: `URQ_CONCLUSION` (confirms connection established)

### Rendezvous request types

1. Waving. The very first of two parties that happens to be the first one that
sends anything to the peer, sends: `URQ_WAVEAHAND`.
2. The party that has received anything from the peer, sends `URQ_CONCLUSION`.
The peer may respond with another `URQ_CONCLUSION`, as needed.
3. The party that is satisfied with what has been sent in `URQ_CONCLUSION`
request, sends `URQ_AGREEMENT` as an information to the peer that it's already
established and it can stop sending.

The redezvous process is different in HSv4 and HSv5, as the latter bases on
a state machine.


The Type field
--------------

There are two possible interpretations of the `m_iType` field, the legacy one
is the "socket type", which in UDT are two possible: `UDT_STREAM` and
`UDT_DGRAM` (of which in SRT only the last one is allowed). The legacy
interpretation is applied only to the very first message ever exchanged, that
is, `URQ_INDUCTION` type message sent as first by the caller.

In all other cases the `m_iType` consists of the following fields:
- Upper 16-bit field reserved for **encryption flags**
- Lower 16-bit field reserved for **extension flags**

The **extension flags** are:
 - In `URQ_INDUCTION` sent back by the listener it should contain
`SrtHSRequest::SRT_MAGIC_CODE` (0x4A17)
 - In `URQ_CONCLUSION` should contain appropriate set of `HS_EXT_*` flags (will
be discussed below)
 - In all other cases it should be 0.

The **encryption flags** currently occupy only 3 bits and they comprise a
declaration of "advertized PB key length". Whichever site declares the
PBKEYLEN, will be advertizing it. Therefore this will contain the value taken
from the `SRTO_PBKEYLEN` option, divided by 8, so the possible values are:
 - 2 - AES-128
 - 3 - AES-192
 - 4 - AES-256
 - 0 - PBKEYLEN not advertised (either no encryption, or agent is considered
receiver)

The PBKEYLEN advertisement is required due to the fact that in HSv5 the sender
should decide about PBKEYLEN, but it will not necessarily be an initiator
(unlike HSv4). In order to allow the responder site to still decide about
PBKEYLEN, this field is used to advertise it before the initiator starts
creating KMREQ message.

When the caller is advertizing PBKEYLEN, it makes no effect on the handshake -
this will just make it create KMREQ message with this key length. When the
listener is advertizing it, however, then it will be taken by the caller as a
good deal (in which situation the caller should not have PBKEYLEN set -
PBKEYLEN should be set by the machine declared as sender). Similarly in
Rendezvous, if this happens for a site to be a sender, but not initiator, this
advertized PBKEYLEN will provide this information before it starts preparing
KMREQ message.


The caller-listener handshake
-----------------------------

This configuration is when you have one machine that exposes a listener and is
waiting for an incoming packet on the bound UDP port that should be an SRT
handshake command (`UMSG_HANDSHAKE`).


### The induction phase

The caller then sends first the "induction" message, which contains (only
significant fields are mentioned):

- Version: must always contain 4.
- Type: `UDT_DGRAM` (2)
- ReqType: `URQ_INDUCTION`
- ID: Your socket ID
- Cookie: 0

The target socket ID (in the SRT header) in this message is 0, which is
interpreted as a connection request.

This phase is only to get the cookie so that the listener doesn't allocate
resources when it's challenged by a prospective DOS attack by getting flooded
by handshake commands.

The HSv4 listener responds:

- Version: value copied from the handshake request command (that is, 4)
- Type: value copied from handshake request command (that is, `UDT_DGRAM`, 2)
- ReqType: `URQ_INDUCTION`
- ID: Your socket ID
- Cookie: crafted the cookie out of host, port and current time with 1 minute
accuracy

The HSv5 listener responds:

- Version: 5
- Type: Lower 16: `SrtHSRequest::SRT_MAGIC_CODE`. Upper 16: Advertised PBKEYLEN
- ReqType: `URQ_INDUCTION`
- ID: Your socket ID
- Cookie: crafted the cookie out of host, port and current time with 1 minute
accuracy

What is important in this difference is that:

1. The HSv4 client ignores completely the values reported in Version and Type.
It is, however, interested with the value in Cookie, as it must be passed to
the next phase. It does interpret these fields also, but int the "conclusion"
message.

2. The HSv5 client does interpret the values in Version and Type. If it
receives the value 5 in Version, it understands that it was the HSv5 client, so
it knows that it should next prepare the proper HSv5 messages in the next
phase. It checks also the value in Type field:
 - whether the lower 16-bit field contains the magic value, otherwise
the connection is rejected. This is a fallback for a case when someone else
would like to extend UDT with their own facilities, increased the value to 5
and tried to test it against SRT.
 - whether there is an advertized PBKEYLEN in the upper 16-bit field, and if
it's not 0, it's written into the value of `SRTO_PBKEYLEN` option.

Note that in a situation when both parties set PBKEYLEN, this results in an
"Unwanted Fallback Behavior", that is, in this case the value advertised by
listener side wins. In order to prevent this from happening, do not set the
`SRTO_PBKEYLEN` option on the side that is predicted to be a receiver. In
case when you wanted to have an encrypted bidirectional transmission, probably
the best way would be to make a listener side the decisive power for this
value, and in rendezvous simply remember to set exactly the same value of
PBKEYLEN on both sides.


### The conclusion phase

The HSv4 conclusion started by the caller party is:

- Version: 4
- Type: `UDT_DGRAM` (as the type of the socket, UDT legacy, SRT must have this
one only)
- ReqType: `URQ_CONCLUSION`
- ID: Your socket ID
- Cookie: a cookie previously received in the induction phase

The HSv5 caller must send this exactly contents when it has received `4` value
in Version field from the listener, and continue the processing according to
HSv4 rules. When it receives Version 5, it sends:

- Version: 5
- Type: Appropriate Extension Flags (see below) and Encryption Flags
- ReqType: `URQ_CONCLUSION`
- ID: Your socket ID
- Cookie: a cookie previously received in the induction phase

The target socket ID (in the SRT header) in this message is the socket ID that
was previously received in the induction phase in the ID field in the handshake
structure.

The value of Type contains the bit flags (see values with `HS_EXT_` prefix in
`CHandShake` structure). The details will be described in **HSv5 extensions**.

The listener responds with merely the same things as above, without the cookie,
which isn't needed here, as well as the extensions for HSv5 will be probably
exactly the same.

Important thing is that there's no predicted any "negotiation" here. If the
values passed in the handshake are anyhow not acceptable by the other side,
the connection will be rejected. The only case when the listener decision can
have precedense over caller's decision is the advertised PBKEYLEN in the
Encryption Flags field in Type field, and the values of latency are always
agreed upon as the greater one of those reported by each party.


The rendezvous handshake
------------------------

The rendezvous arrangement relies on the rule that both sides are equivalent
and both try to connect to one another. Both have bound the ports (the same
port number must be used for both parties), and both do connect, but none of
them is listening (packets are being expected to be received from the exact
other party, the same to which the agent is simultaneously trying to connect),
which means that it's perfectly safe to take the other party as a good deal and
no induction-conclusion phase split as safety precaution is required. Still,
the process is more complicated.

The rendezvous process is similar in HSv4 and HSv5, although for HSv5 the whole
process is defined separately due to having introduced also an extra state
machine. You can treat HSv4 process description as introduction for HSv5.

### HSv4 rendezvous process

In the beginning, both sides are sending to the peer an SRT control message
of type `UMSG_HANDSHAKE` with the following fields:

- Version: 4 (as per HSv4)
- Type: `UDT_DGRAM` (for any version, as a regard for legacy SRT)
- ReqType: `URQ_WAVEAHAND`
- ID: Your socket ID
- Cookie: 0

Every party upon the first call of `srt_connect()` function called by the
application first sends this message to the peer, and then tries to read
a packet from its underlying UDP socket to see if the other party is alive.
Upon reception of this `UMSG_HANDSHAKE` message, particular party turns into
the second, conclusion phase, and so it sends this message:

- Version: 4
- Type: `UDT_DGRAM`
- ReqType: `URQ_CONCLUSION`
- ID: Your socket ID
- Cookie: 0

after which the party is considered connected. When a party receives this
message from the peer, it sends formally the next message with ReqType
field set as `URQ_AGREEMENT`. This is however only a formal final conclusion
of the handshake process, required from the party that is sending the
conclusion message, just to inform it that it can already stop sending them
(note that this is UDP so the party cannot assume that the message has
reached the other party).

With HSv4 there's no dillema who is Initiator and who is Responder because
still this is unidirectional, so the party that has set `SRTO_SENDER` flag
is the Initiator and the other is responder, like always in HSv4.


### HSv5 rendezvous process

This process introduces a state machine and therefore it's slightly
different, although still bases on the same message request types. Still
both start with `URQ_WAVEAHAND` and use Version 5. The version recognition is
easy - the HSv4 client does not look into the Version number, and HSv5 can
quickly recognize the version from the Version field, and it continues with the
HSv5 rendezvous process only when Version 5 is found.

With HSv5 rendezvous, both parties create the cookie. This is necessary
for something completely different than it was in caller-listener: this
is needed for the Initiator and Responder roles assignment. This process
is called "cookie contest". The cookie value is simply a 32-bit number,
so they are simply compared with one another. It's considered virtually
impossible that cookies result identical in both parties (as the requirement is
that both must use the same port number, you can't have two sockets on the same
machine bound to the same device and port and operate independently), so
there's only a simple fallback predicted that when they appear to be equal,
then the connection will not be made until they regenerate the cookies (which
should happen after a minute). When the cookie value is greater in the agent's
cookie than the peer's cookie, it's a winner and becomes Initiator, otherwise
it becomes a Responder.

In this case there are two possible (at least theoretically) arrangements: the
*parallel* arrangement and *serial* arrangement. The parallel arrangement is
highly unlikely, and although the state machine predicts the possibility of it,
it wasn't even tested if it works because this is so unlikely that it's not
even possible to cause this to happen within the current testing possibilities.
This can only be possible if the messages with `URQ_WAVEAHAND` are sent and
reach both peers at perfectly the exactly same time with microsecond accuracy,
and both machines have also spent exactly the same amount of time for network
data processing, so none of them has exited the initial state at the moment
when they considered sending the message to the peer. This arrangement will not
be further described.

In the serial arrangement there's always a situation that one is first,
and the other follows, that is, one repeats sending waveahand messages,
and the other starts in a situation that there's already one waveahand
message waiting for it, which means that the **very first** message
send by the "following" party will be the one with ReqType equal
to `URQ_CONCLUSION`. This process then can be described easily in series,
with designating the first and follower parties as A and B.

1. Both parties are in *waving* state. The A party sends initial handshake
message to B:
	- Version: 5
	- Type: Extension field: 0, Encryption field: advertise PBKEYLEN.
	- ReqType: `URQ_WAVEAHAND`
	- ID: Your socket ID
	- Cookie: created out of host/port and current time

2. B receives `URQ_WAVEAHAND` message, switches to *attention* state and sends:
	- Version: 5
	- Type:
        - Extension field: appropriate flags, if initiator, otherwise 0
        - Encryption field: advertised PBKEYLEN
	- ReqType: `URQ_CONCLUSION`
    - NOTE: when this side is initiator and encryption is on, it will
      use the PBKEYLEN either its own, or received from the peer, if
      the peer has advertised PBKEYLEN.
	
3. A receives `URQ_CONCLUSION` message, switches to *fine* state and sends:
	- Version: 5
	- Type: Appropriate extension flags and encryption flags
	- ReqType: `URQ_CONCLUSION`

It is important to note here that at this moment it always sends the extension,
however this extension will contain `SRT_CMD_HSREQ` if this was an Initiator,
or `SRT_CMD_HSRSP` if it was a Responder. Again, if this side is Initiator,
and the previous message received from the peer contained the advertised PBKEYLEN
in the Encryption Flags field in the Type field, it will be used as a key
length for the prepared `SRT_CMD_KMREQ` block with KMREQ extension.

4. B receives `URQ_CONCLUSION` message, and then depending on the role:
	- If Initiator (so the message contains `SRT_CMD_HSRSP`)
		- switches to *connected* state
		- sends to B the message with ReqType = `URQ_AGREEMENT`
		- the message contains no SRT extensions (Type should be 0)
	- If Responder (so the message contains `SRT_CMD_HSREQ`)
		- Switches to *initiated* state
		- sends to B the message with ReqType = `URQ_CONCLUSION`
		- the message contains extensions with `SRT_CMD_HSRSP`

5. A receives the above message and turns into *connected* state. Then
if it was Initiator (received `URQ_CONCLUSION` with `SRT_CMD_HSRSP`),
it sends to B the message with ReqType = `URQ_AGREEMENT`. Otherwise
(when Responder), the received message has ReqType = `URQ_AGREEMENT`
and in response the agent should do nothing. 

6. If B was Initiator, it's connected already. If it was a Responder it
should receive the above `URQ_AGREEMENT` message, after which it
switches to *connected* state. In case when the UDP packet with agreement
message was lost, this side will still turn into *connected* state once
it receives anything else from the peer, or when the application request
it to send any data.



The SRT extended handshake
--------------------------

### HSv4 extension handshake process

The HSv4 extension handshake process starts **after the connection is considered
established**. It is important to know that whatever problem may occur here by
any reason, it may now only result in problems with data transmission. The
extension process is using the UDT command extension mechanism - a command that
is recognized with major code as `UMSG_EXT` and with minor code contained in
the less significant half (which is always 0 for standard messages).

Note that these messages, although sent between interconnected sites, are still
sent as simply UDP packets and as such they undergo all the problematic UDP
protocol phenomena, such as packet loss (the packet recovery concerns
exclusively the payload). Therefore they are being sent "stubbornly" (with
slight delay between subsequent retries) until the peer responds, with some
"give up" maximum number of retries. It's very important to understand that the
first message sent by the Initiator side is done at the moment when the
application requests to send the first data packet, and this data packet is
**not** held back until the extended SRT handshake is finished. This is simply
sending first the first message, followed by this data packet, and the
transmission starts already without having the extended SRT handshake yet
agreed upon.

Because of this situation, it means that the initial few packets are being sent
still without having the appropriate SRT settings already working, which concerns
two facilities:

- TSBPD mechanism to apply a latency to the received packets - they are being
at first delivered immediately, only when the `SRT_CMD_HSREQ` message is processed
is the received packet applied the latency to; also the TLPKTDROP mechanism
isn't working until then
- Encryption (if used): the packets can't be decrypted until the `SRT_CMD_KMREQ`
is processed and the keys installed (the data packets are still encrypted, just
the receiver can't decrypt them and will drop them). The sent packets are still
encrypted, but uselessly.

The codes for commands are used the same in HSv4 and HSv5 process, just in
the HSv4 this is a minor code used with `UMSG_EXT` command, and in HSv5 as
the "command" part of the extension block. The messages are sent as "REQ"
part until they get something as "RSP" part, up to some timeout, after which
they give up and stay with pure UDT connection.


### HSv5 extension handshake process

The Type field in conclusion message contains one of these flags:

- `HS_EXT_HSREQ`: defines SRT characteristic data, practically always present
- `HS_EXT_KMREQ`: if using encryption, defines encryption block
- `HS_EXT_CONFIG`: informs about having extra configuration data attached

The extension block has the following structure:

- 16-bit command symbol
- 16-bit block size (excluding these two fields)
- followed by any number of 32-bit fields of given size

What is contained in the block exactly, depends on the extension command code.

The data being received in the extension blocks in the conclusion message
undergo further verification and if the values are not acceptable, the
connection will be rejected. This may happen in the following situations:

1. The Version field contains 0. This means that the peer rejected the
handshake.

2. The Version field was higher than 4, but no extensions were added (no bigger
size than HSv4 hanshake or no extension flags)

3. Processing of HSREQ/KMREQ message has failed (due to e.g. internal error)

4. Both sides declare transmission type that is not compatible with one
another. This will be described further around new HSv5 handled features;
the HSv4 client supports only and exclusively one transmission type which
is Live and this is marked by the Type field in the HSv4 handshake. In any
case, when there's no Smoother declared, it's assumed it's Live, otherwise
the Smoother type must be exactly the same on both sides.


### SRT extension commands

#### HSREQ and HSRSP

The `SRT_CMD_HSREQ` message contains three 32-bit fields designated as:

- `SRT_HS_VESION`: the 0x00XXYYZZ, encodes the XX.YY.ZZ SRT version
- `SRT_HS_FLAGS`: the SRT flags, see below
- `SRT_HS_LATENCY`: The latency specification

The flags are the following bits in order:

0. `SRT_OPT_TSBPDSND` : The party will be sending in TSBPD mode

This is used by the sender party to define it will use TSBPD mode.
The Responder should confront it with its setting of TSBPD reception
and if it isn't using TSBPD for reception, respond with its reception
TSBPD flag not set. In HSv4 only used by Initiator party.

1. `SRT_OPT_TSBPDRCV` : The party expects to receive in TSBPD mode

Like above, this declares that it wants to receive in TSBPD mode.
The Responder should confront this setting with its TSBPD sending
mode (HSv5 only) and set the sending TSBPD flag appropriately in
response. In HSv4 only used by Responder party.

2. `SRT_OPT_HAICRYPT` : The party includes haicrypt (legacy flag)

This is a legacy flag to recognize very early SRT version without
haicrypt and therefore with encryption bits being part of the
message number. Since 1.2.0 version (the oldest public version) it must be
always set (it has nothing to do with using the encryption in the connection).

3. `SRT_OPT_TLPKTDROP`: The party will do TLPKTDROP

Declares the `SRTO_TLPKTDROP` flag of the party. This is important
because both parties must cooperate in this process. In HSv5, if both
directions are TSBPD, both use this setting. This flag better be
always set in live mode.

4. `SRT_OPT_NAKREPORT`: The party will do periodic-NAK-report

Declares the `SRTO_NAKREPORT` flag of the party. This flag means
that periodic NAK reports will be sent (repeated `UMSG_LOSSREPORT`
message when the sender seems to linger with retransmission).

5. `SRT_OPT_REXMITFLG`: The party uses REXMIT flag

Declares that the client understands and handles the REXMIT flag,
which is contained in the SRT header. Without this, this bit
is a part of the message number. The REXMIT flag is clear when
the packet is sent originally and set when the packet is retransmitted.
The party that sends the payloads must know this flag of its peer
in order to know whether the format that includes this flag should
be used or not.

6. `SRT_OPT_STREAM`   : The party uses stream type transmission

This is introduced in HSv5 only. When set, the party is using a stream
type transmission (file transmission with no boundaries). In HSv4 this
flag does not exist and therefore it's always clear, which coincides
with the fact that HSv4 supports the Live mode only.

The latency specification is split into two 16-bit fields:
 - Higher: Peer/receiver latency (HSv5 only)
 - Lower: Agent/sender latency

The meaning of sender and receiver latency is the following:

1. The sender latency declares the latency that the sender proposes for the
receiver.

2. The receiver latency is the information for the sender with how big latency
it wants to receive the data. 

3. No matter the direction, the Initiator sets these both values, the
Responder "fixes" them (sets the maximum between the received ones
and its own) and sends them back, in reverse order.

4. In HSv4 there's only one direction, and the Initiator is Sender
simultaneously, so it only declares the proposed latency for the
recevier, and the receiver responds with the possibly fixed value.

The same layout is used in `SRT_CMD_HSRSP` and it's sent by responder
back to the initiator.

The `SRT_OPT_HAICRYPT` and `SRT_OPT_REXMITFLG` define special case of
interpretation of the contents in the SRT header for payload packets.
The second field (Message number) contains, beside the message number
value, also special bit fields, among which there are two introduced
in SRT towars the UDT legacy:
 - Encryption (2 bits)
 - Retransmission (1 bit)

Their presence is controlled by these two above flags, at least in
theory. In practice there are no versions around that do not include
encryption bits, although there may be still around versions of SRT
that do not include the Retransmission field, which was introduced
in version 1.2.0. In practice both these flags must be set in the
version that has them defined.


#### KMREQ and KMRSP

This message contains so-called KMX (key material exchange) message
used for encryption. The most important part of this message is the
AES-wrapped key (see [Encryption documentation](encryption.md) for
details). If the encryption process on the responder side was successful,
the response contains the same message for confirmation, otherwise it's
one single 32-bit value that contains the value of `SRT_KMSTATE` type,
as an error status.

Note that when there's a different settings set concerning encryption, then
the connection is still allowed, but with the following restrictions:

- If the initiator declares encryption, but the responder does not, then
the responder responds KMRSP with `SRT_KM_S_NOSECRET` status. This means
that the responder will not be able to decrypt data sent by the initiator,
but the responder can still send the data - unencrypted - to the initiator
- If the initiator did not declare encryption, but the responder did, then
the responder will attach `SRT_CMD_KMRSP` (despite that the initiator did not
send `SRT_CMD_KMREQ`) with `SRT_KM_S_UNSECURED` status.
- If both have declared encryption, but have set different passwords,
the responder will send KMRSP block with `SRT_KM_S_BADSECRET` value

The value of the encryption status can be retrieved from the
`SRTO_SNDKMSTATE` and `SRTO_RCVKMSTATE` options. The legacy (or
unidirectional) option `SRTO_KMSTATE` resolves to `SRTO_RCVKMSTATE`
by default, unless `SRTO_SENDER` option is set to true, in which
case it resolves to `SRTO_SNDKMSTATE`.

The values retrieved from these options depend on the result of the KMX
process:

1. If only one party declared encryption, the KM state will be:
- The party that declares no encryption
   - RCVKMSTATE: NOSECRET
   - SNDKMSTATE: UNSECURED
   - Result: This party can send payloads unencrypted, but it can't
     decrypt packets received from the peer.
- The party that declares encryption
   - RCVKMSTATE: UNSECURED
   - SNDKMSTATE: NOSECRET
   - Result: The party can receive unencrypted payloads from the
     peer, and will be able to send encrypted payloads to the
     peer, but the peer won't decrypt them.

2. If both declare encryption, but they have a different password,
then both states are `SRT_KM_S_BADSECRET`. In such a situation both
sides may send payloads, but the other party won't decrypt it.

3. If both declare encryption and the password is the same on both
sides, then both states are `SRT_KM_S_SECURED`. The transmission
shall be correctly performed with encryption in both directions.

Note that due to introducing the bidirectional feature in HSv5 and therefore
the initiator and responder roles, the "old way" (HSv4) method of initializing
the crypto objects used for security is used only in one of the directions.
This is now called "forward KMX":

1. The initiator initializes its Sender Crypto (TXC) with preconfigured values.
2. The initiator sends KMX message to the receiver.
3. The receiver deploys the KMX message into its Receiver Crypto (RXC)

This process is the general process of Security Association done for the
"forward direction", that is, when done by the sender. However as there's
only one KMX process in the handshake, in HSv5 this must initialize also
the cryptos in the opposite direction. This is accomplished by "reverse KMX":

1. The initiator initializes its Sender Crypto (TXC), and **clones it** to the
Receiver Crypto.
2. The initiator sends KMX message to the receiver.
3. The receiver deploys the KMX message into its Receiver Crypto (RXC), and
**clones it** to the Sender Crypto.

This way the sender being a Responder, has the Sender Crypto initialized very
similar way as it would have it when it was Initiator, however basing on values
extracted from the Receiver Crypto, to which configuration was deployed from
the KMX message.

The extra things defined as "reverse KMX" are happening only and exclusively
in the HSv5 handshake.

The key is then refreshed after appropriate number of packets have been sent
and it happens basing on occuring three events, which are considered happening,
when the number of sent packets exceeds given limit number taken from the
`SRTO_KMREFRESHRATE` and `SRTO_KMPREANNOUNCE` settings:

1. Pre-announce: when reaches `SRTO_KMREFRESHRATE - SRTO_KMPREANNOUNCE`
2. Key switch: when reaches `SRTO_KMREFRESHRATE`
3. Decommission: when reaches `SRTO_KMREFRESHRATE + SRTO_KMPREANNOUNCE`

and the following actions are undertaken:

1. Pre-announce: the new key is generated and sent to the receiver using
the `UMSG_EXT`-based command packet with `SRT_CMD_KMREQ`, that is, the
same way in both HSv4 and HSv5 clients. The receiver should deploy this
key to its Receiver Crypto.
2. Key Switch: The bits in the data packet header concerning encryption,
which should be `EK_EVEN` or `EK_ODD`, since now change the value to the
opposite towards it was so far. Since now also the opposite key is used
(the newly generated one).
3. Decommission: the old key (the key that was used with the previous flag
state) is decommissioned on both sender and receiver side.

Please note that in the implementation the handlers for KMREQ and KMRSP are
the same for `UMSG_EXT`-based messages and the extension blocks in the
handshake. However in case of `UMSG_EXT` only one direction is initialized
(or updated). The handshake in HSv5 initializes both directions, one with
forward KMX and one with reverse KMX, but then any key refreshing activities
happen exclusively for one direction and exclusively on the premise that the
number of sent packets exceeded given value.



#### SMOOTHER

This is a feature supported by HSv5 only. This value contains a string
with the name of the SRT Smoother type.

The versions that support HSv5 contain an additional block, similar to
the "congestion control" class from UDT, which is called Smoother. The
default Smoother is called "live" and the 1.3.0 version contains an
additional optional smoother type caller "file". Within the "file" smoother
there's also a possible transmission of stream mode and message mode (the
"live" smoother may only use the message mode and with one message per
packet).

The smoother part is not obligatory when not present, the "live" smoother
is used by default. For HSv4 client, which doesn't contain this feature, the
smoother type is assumed to be "live".

The "file" smoother type, the second builtin one, reintroduced the old
UDT features for stream transmission (together with `SRT_OPT_STREAM` flag)
and messages that can span through multiple UDP packets. The smoothers
differ in the way how the transmission is handled, how various transmission
settings are applied and how to handle any special phenomena that happen
during transmission.


#### SID

This feature is supported by HSv5 only. This value contains a string of
user's choice that can be passed from the caller to the listener.

The Stream ID is a string up to 512 characters that Initiator can pass to
Responder. The application has such access to that feature that it can be
set as a socket flag on a caller socket, and the accepted socket will have
this string also set and able to be retrieved using the same flag,
`SRTO_STREAMID`. This gives the listener a chance to make a decision of
what to do with this connection - such as decide which stream to send
in case when the listener is the stream sender.


