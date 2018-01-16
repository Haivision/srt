The SRT terminology
===================

This section intends to collect various terminology being used in SRT so that
everyone is on the same page.

The collection is random and not lexicographically sorted. This page is used
rather as a collaboration space than a documentation.


Party
-----

This most precisely describes the application that is using SRT, or even more
precisely, it's spoken this way about some part of it using exactly one SRT
Socket (\*). Every socket in SRT is independent, although there are several
things that are global, that is, shared by all sockets used by one application.
Note that there can be running multiple applications using SRT on one machine,
independently, as well as one application may use more than one SRT socket.


Socket (SRT Socket)
-------------------

A unit identifying a point of communication, which is predicted to be connected
to another such unit. The data can be exchanged by "writing to" one socket, so
that the data written this way are next received on the other party. Note that
the "SRT Socket" has completely nothing to do with any kind of network socket
in the system network library (such as UDP socket), the name is just inherited
by analogy. Although the connection still bases on a system-understood term of
port (as it uses UDP as a transport layer), and in this case this is a UDP port
from the system point of view, the SRT socket should not be confused with UDP
socket - see Multiplexer for details.


Multiplexer
-----------

The best direct understanding of this term is that it's a "utility wrapper
around a UDT socket". The name refers to one of its important functionalities,
that is, it may integrate - if required - data coming from various sources and
send them then using this UDP socket. This also works the other way around -
"switching". The data coming into the socket, after being interpreted, are
redirected to the correct SRT Socket for further processing.

Please note that one multiplexer may potentially serve multiple SRT sockets at
a time, despite that it is tightly bound to one UDP socket. Note that
Multiplexers are global objects for an application, so this can be shared
between sockets only within one application. The Multiplexer uses a UDP socket
bound to a port, be it explicitly, or by autoselection, when the port is
configured as 0 and the first sending through that UDP socket is done - note
then that a network port is bound to an application, and therefore to the
multiplexer that uses the UDP socket bound to it.


Agent and Peer
--------------

In short, they should be understood simply as "this party" and "the other
party". These terms are used when reconsidering some functionality or state
running within the frames of a given socket (see: Party), which is expected to
be connected to some other party. The party, where the considered activity
is running, is called "agent", whereas the other party, to which this party is
expected to be connected, is called "peer".


Cookie
------

This is some value characteristic for particular party, which is calculated
basing on the current IP address and time. The cookie is valid for about 1
minute, so in case of incorrect cookie, the cookie for the previous minute can
be tried again as a fallback.

This cookie is usually sent with the handshake message to the other party.
There are two purposes of it:

* In caller-listener configuration, it's required for a second phase of
  connection. The listener returns the cookie and the caller must call again
  with the same cookie. This is to protect against flooders.
* In HSv5 rendezvous configuration it's required to decide the role of each
  party - so-called "cookie contest". The party, which's cookie is greater,
  is a winner, the other is a loser. This is to resolve the initial equivalence
  into the initiator-responder role split.


Listener and Caller, Induction, Conclusion
------------------------------------------

One of the most known method of connecting is the listener-caller
configuration. One party - listener - awaits a connection request and accepts a
connection when the request comes. The other party - caller - sends the
connection request. Upon successful connection, the listener is spawning a
child socket and since then this spawned socket and the socket on the other
network node are interconnected 1 to 1, whereas the listening socket continues
waiting for another connection, as long as the accept function is called on it
again. Potentially this configuration allows for multiple callers to connect to
one listener. The listener socket creates its own *multiplexer* and it is used
then to both send out the packets to the socket on the other side, as well as
switch the incoming packet to the correct target SRT socket.

Obviously, when preparing the connection, the caller knows the IP address and
port of the other party, whereas the listener doesn't know anything, it will
take the address of the connecting party as a good deal. Due to that the caller
party cannot be trusted, the connection is done using two-step handshake: First
the caller sends the INDUCTION request to the listener, with handshake version
HSv4 (legacy requirement) and 0 cookie. The listener responds with INDUCTION
response, which should contain the correct handshake version (should be 5 in
case of HSv5-capable client) and the cookie. Upon reception of this, the caller
party sends the CONCLUSION request, which contains this cookie. The HSv5
capable caller when receiving HSv4 induction response, should prepare the HSv4
handshake, that is, according to UDT4 definition without extensions. When
receiving HSv5 handshake version in INDUCTION response, the CONCLUSION response
should contain this version of handshake plus extensions: HSREQ, required
always, plus optionally KMREQ, if encryption is used. The listener should
process the received data and send the CONCLUSION response, also with
extensions in case of HSv5.


Sender and Receiver
-------------------

The distinction of Sender and Receiver is a legacy of HSv4 handshake version.
In this version, the party that is going to send the data must set
`SRTO_SENDER` socket option and then it should only write to the socket (the
data in the opposite direction can still be sent, but it can't be a live
stream). This party becomes then a Sender, whereas the other party, which does
not set this option, becomes a Receiver and should only read from the socket.

In HSv5 these terms mean nothing - both can send and receive data, so every
party is a sender and receiver simultaneously.


Initiator and Responder
-----------------------

These terms are required for the SRT Handshake data interchange, which is done:

* in HSv4 using separate control message exchange after the connection is
  established
* in HSv5 using SRT handshake extensions

The general explanation of these terms is: Initiator is the party that starts
the process of SRT handshake (sends HSREQ), whereas Responder does the
challenge response (sends HSRSP), and does nothing about the data that this
handshake concerns until it receives the challenge from the Initiator.

In HSv4 the Initiator and Responder terms are bound exactly to Sender and
Receiver terms respectively - regardless of the connection method. This is
because it's based on extra control message exchange, and this is started
always by the party declared as sender.

In HSv5 the SRT handshake is built in to the overall UDT handshake process, and
there are no sender and receiver roles. This is then dependent exactly on the
connection method.

In case of caller-listener connection configuration, Initiator and Responder
are Caller and Listener respectively.

In case of HSv5 rendezvous mode, both sides are by definition equivalent, so
the role of Initiator and Responder is selected according to a cookie-based
role selection algorithm ("cookie contest"): both parties create a cookie and
send it to the other party with the first handshake message, so each party
knows its own cookie and the other party's cookie. The party, which's cookie
value (as integer number) is greater, becomes an Initiator, the other one
becomes a Responder. In this mode also the Initiator sends the conclusion
handshake with SRT extension blocks, whereas Responder sends simply the
handshake. At the end, the Responder should send the agreement handshake.


Rendezvous
----------

The rendezvous mode differs to listener-caller configuration in that both
parties know about each other, that is, they know their IP address and port,
moreover, they both use the same port. This is something like both listener and
caller in one, and they both try to connect to one another. Both parties are
equivalent as a side of connection.

In HSv4 the method relies on sending first the rendezvous handshake
("waveahand"), and each party should send conclusion upon receiving of this
message, then finally the agreement message finalizes the connection process.

In HSv5 the rendezvous process has been redesigned so that the data exchange
is possible within the handshake.

The rough description of a handshake needs introducting several terms. Due to
a small random factor, there are two possibilities that may occur with
rendezvous: The "parallel arrangement", in which each party is referred to as
just "party", or "serial arrangement", in which one is master and one is slave.
It depends completely on a random conditions, which arrangement will result,
and if serial arrangement happened, which one is master and slave. It's worth
noting though that the parallel arrangement is virtually impossible to happen
because it needs that two connecting parties start the process in an exactly
perfect synchronization, in particular, one party must start exactly at the
time when the other party is already started and listens, but the packet sent
by this party hasn't reached the other party yet; according to the estimations
there is about a 10ms time gap needed to be fit in to make it happen.

In the beginning, each party sends the handshake message type `URQ_WAVEAHAND`.
What happens next depends on whether it was really received.

In parallel arrangement, each party receives the handshake message type
`URQ_WAVEAHAND` as the firstmost message. Therefore every party goes then the
same way:

* switches to ATTENTION state. Responds with `URQ_CONCLUSION` message. If it's
a winner of Cookie Contest, it attaches a HSREQ extension
* Upon reception of `URQ_CONCLUSION` message it switches to INITIATED state.
If it was a winner, it should receive an empty message, if loser, HSREQ
extension
* In INITIATED state it sends `URQ_CONCLUSION` message to the party. If it's
a winner, then it sends an empty conclusion, otherwise it sends HSRSP
extension.
* Then, upon reception of `URQ_CONCLUSION` message:
   * if it was a winner, it should get HSRSP extension and switches to
     CONNECTED state, with sending `URQ_AGREEMENT` to the other party
   * if it was a loser, it remains in INITIATED state, and the conclusion
     message should be simply and empty response on empty request.
* When received `URQ_AGREEMENT` message (should be a loser), it switches to
CONNECTED state.

In serial arrangement, master is the party that receives `URQ_CONCLUSION` as a
first message, and slave is the party that receives the `URQ_WAVEAHAND` message
first. This way the process is easier and completely serial, basing on
request-response cycles:

1. The slave party upon reception of waveahand message, switches to ATTENTION
state and sends the `URQ_CONCLUSION` message. If it was a winner of cookie
contest, it sends the HSREQ extension.
2. The master party receives the `URQ_CONCLUSION` message first and so switches
to FINE state and sends `URQ_CONCLUSION` message in response. If it was a
loser, then it interprets incoming HSREQ extension and attaches HSRSP extension
to the outgoing message, otherwise it attaches HSREQ extension.
3. The slave receives `URQ_CONCLUSION` message. If it was a winner, then it
receives the HSRSP extension in this message, so it concludes the handshake
and switches to CONNECTED state, with sending the `URQ_AGREEMENT` to the master
peer. If it was a loser, then it receives HSREQ extension in this message, so
it switches to INITIATED state, and sends `URQ_CONCLUSION` with attached HSRSP
extension.
4. The master if it was a loser, should receive `URQ_AGREEMENT` message, switch
to CONNECTED state and do nothing more. If it was a winner, it should receive
`URQ_CONCLUSION` message with HSRSP, so it interprets the extension and
switches to CONNECTED state as well, but this time it also sends
`URQ_AGREEMENT` to the slave peer.
5. If the slave was a winner, it should not receive any more messages. If it
was a loser, it should receive `URQ_AGREEMENT` message, upon reception of which
it should switch to CONNECTED state.



