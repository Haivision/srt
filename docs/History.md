History of SRT
==============

Motivation
----------

In our Network Video products we have always been interested in some protocol
that would be both fast and reliable and best suited for transmitting a live
video stream. Sending the stream over UDP is the fastest possible method that a
network system can offer - but it still has disadvantages, like no delivery
guarantee. Although FEC can be used to recover lost packets, the conditions
in which this suffices are still limited. Also encryption, very important for
live streaming, can be at best implemented also only on top of it. TCP, on the
other hand, tries to be universal and therefore not overload the network, which
comes at the cost of limiting the bandwidth. This works against the live
streaming and timely delivery.

Last time various initiatives have been undertaken in order to improve TCP
algorithms, combine it with encryption, make it fit various requirements and
cases of network data transfer, like e.g. Google tries with its QUIC project.
Although it sounds very promising, it will take time to standardize, implement
and spread across current systems. With a userspace solution on top of UDP we
can specialize our algorithms narrowly, just to suit our needs, and have the
solution right in the hand.


The beginning with UDT4
-----------------------

We have found then a library that implements the logics of a connection term,
similar to TCP, implemented as a userspace solution on top of UDP, called
"UDP-based Data Tranfer" (UDT). This was mainly predicted to be used as a
method for a fast file transfer. It implemented basic functionality of a
connection, sending data over the connection, sequencing and retransmission
mechanism.

We started with adding an experimental support for UDT version 4 in our
software. This allowed us to see how it works as well as what disadvantages
this solution has in case of a use for live streaming. That's how we started
with adding several extra features that should make this library more suitable
for live streaming.

UDT4 was already partially prepared for extensions: the configurable congestion
control class (CCC) and user-extended message type. The base features have been
first implenented with the use of this. For example, the SRT-specific data
exchange required for the connection to work has been done by using the
extended-type message that should constitute the "SRT handshake". This was an
easy solution that could bring us closer to our needs quickly - although it
carried out disadvantages:

* The receiver can't decrypt packets and doesn't deliver packets with
predefined latency, until appropriate messages are exchanged
* This introduces extra entropy to the connection process

We believed to fix these problems in future.


Smooth delivery: latency under control
--------------------------------------

One of the first things required for the live streaming was that the delivery
time **between sibling packets** should remain the same as it was at the
source. Of course, it's not possible to overcome the problem of that for some
packets passing throught the network it will take more time to deliver than
the others, however in some extreme cases it caused that the difference between
sibling packets has either caused extra delay for the receiver to wait for the
data, causing an enforced pause, or one delayed packet could make the packet
next to it come too fast and therefore could not be processed when the
processing facility had strict time requirements for delivering the data. This
did not work well with the decoders.

The solution was the feature called Timestamp-Based Packet Delivery (TSBPD).
This has used an extra delay to wait before the packet is to be delivered to
the receiver application (default: 125ms). Basing on the timestamp of the
received packet, predefined delay and the RTT, there was calculated the
"playout time", that is, the local time when this packet should be delivered to
the receiver application. If it's too early for the delivery, the thread that
waits for data reception or epoll signal is sleeping.

It all starts with getting the time information during the SRT handshake and
then calculating the representative local time for every remote timestamp. Once
the packet exchange is being done, this time base is known as well as RTT
(round-trip time, time that elapses between sending a packet and getting a
packet as a direct response for that one).

We've implemented this by adding an extra thread that took over the delivery
part. The thread connected so far to data delivery has been changed so that it
only "checks in" the data and signals the tsbpd thread to inform it about the
buffer state update. Whenever the tsbpd thread is awake, it checks the packet
at the top of the receiver buffer and calculates its expected time to play. If
this time has come already (or it's past that time), the packet is delivered
immediately, otherwise the tsbpd thread falls asleep for the remaining time
(still, it can be woken up upon reception of the next packet).


Timely delivery more important than error correction: TLPKTDROP
---------------------------------------------------------------

Although SRT is trying to deliver perfectly the data that were send, and
retransmit packets that were lost in the network, this still is a live
streaming, and in case of a real bad luck a packet that was lost might be
either lost again, or simply took some unfortunately long time to deliver.
Packets are delivered according to their sequence numbers, and every packet
has a timestamp that is then used to calculate the playout time. So, the
receiver can wait for the packet to be retransmited, but not infinitely, as
after some time there's a time to play the next packet, and we still don't
have any guarantee as to when the lost packet is going to be delivered. 

This feature must be turned on to work, however it's rather inevitable in case
of live streaming. This simply causes that the receiver agrees for having the
packet lost, as it is more important to timely deliver the data that are
already received and wait in the buffer, than wait virtually-infinite time for
the lost data. The same thing also works on the sender side (this feature is
also agreed upon during the SRT handshake): when the sender knows that it is
already too late to retransmit the lost packet because it will never make it
on time, and even if delivered, the receiver will drop it anyway, it won't be
retransmitted anymore.

In the tsbpd thread - see above - it uses such a method that it sleeps for
the time needed to play the next packet, no matter if this is a subsequent
packet, or some aheadcoming packet next to a lost packet. If a lost packet
comes in either on time, or at least earlier than the time comes to play
the aheadcoming packet, it will effectively wake the tsbpd thread, and it will
then see a packet ready to play that should have been played already. But
if the time to play the aheadcoming packet will come earlier than the lost
packet would be received, then the aheadcoming packet is played anyway, and
the non-recovered packet is removed from the loss list.

This is being done by doing "artificial acknowledgement" - that is, the
receiver removes the lost packet from the loss list, and the further
acknowledgement control packets will treat this sequence as if the packet was
actually delivered.

Of course, this feature makes that the user agree for having scratches in the
video in case of a very bad luck. How probable this bad luck is - it depends
on the network conditions and the configured latency. The only alternative to
this, however, is to have a pause in the video urgently, and this is considered
rather more annoying. The only thing that could be done is to make the
probability for this to happen as low as possible.


Retransmission enhancement: periodic NAK report
-----------------------------------------------

As in UDT every single packet can potentially be lost, including the control
packets that carry over the packet loss information and acknowledgement, we
have to ensure that the sending party has really received the loss report and
is really aiming for delivering the lost packet as fast as possible. The
NAKREPORT feature causes that after the `UMSG_LOSSREPORT` was sent immediately
after seeing a hole in the sequences, this message, containing this time all so
far notified lost packet sequences, will be periodically sent back to the
sender, until the lost packets are acknowledged (including artificial
acknowledgement).

This feature can be also turned on and off using a socket option.


Retransmission enhancement: fast retransmission
-----------------------------------------------

As in UDT every single packet can potentially be lost, including the
retransmitted packets, the only "clear state" for particular packet is when the
acknowledgement packet comes in, which marks the last sequence number up to
which all packets are "clear". If this packet is not received by the sender on
time, be it due to being lost or lingering too long, or simply somehow the next
acknowledgement still did not clear this packet from the loss list, even though
the lossreport wasn't sent or wasn't repeated (also might be lost), the sender
decides to "retransmit the packet anyway, even if not explicitly requested, as
this packet was still most probably lost".


Encryption
----------

Whatever you send as a video through the public internet, somewhere it can be
sniffed and leaked in result - so to ensure the privacy for sending the video,
encryption is inevitable.

We've created then a special library wrapper, which has used OpenSSL under the
hood, just was suited for our needs: haicrypt. The encryption in SRT was then
implemented with the use of:

* key material exchange that should ensure decryption of encrypted data using
the shared secret (refreshed after some time)
* added encryption status to the packet flags, and encryption/decryption calls
around the packet sending and reception


Refactoring and bugfixing
-------------------------

Last but not least is the work we've also done towards the original UDT4 code,
beside our enhancements oriented for live video streaming. It would be boring
to enumerate all the bugs we've fixed there - although many of them were
related to race conditions and sometimes data consistency. We are aware of that
there may still be several bugs and there are also some design flaws coming
from original UDT4 code, in addition to the bugs that we might have introduced
during our work. All these things will be cleaned up in time and according to
the importance and priorities.

As it comes to code refactoring there are several things worth highlighting.
First of all, lots of symbolic names have been introduced, like for:

* Control message types: symbols with `UMSG_` prefix.
* Handshake request types: symbols with `URQ_` prefix.
* Error types used by `CUDTException` class: with `MJ_` and `MN_` prefixes

If you are interested, you can try to compare the source files with the
original UDT4 and see how many things have been changed. There are some new
files added in SRT, although they usually contain completely new things towards
UDT4.

Another important work was splitting the quite entangled `CRcvBuffer::worker`
method into smaller pieces, which highly contributed to getting close to
understanding how this whole thing really works, as well as clarifying the
design of mutex-locking around this functionality.

You should probably noticed also that the congestion control has been
completely removed and the remaining things are oriented to encryption only.
This class was good as a part of UDT4, which allowed the users - including us -
to experiment a bit with various congestion control algorithms, but such a
flexibility was no longer required in SRT.

The handshake extension was later rewritten into something called "integrated
handshake", known also as HSv5. The UDT4 library did not allow nor predict the
need of an extended handshake data, and the problem was that the version
recognition in the handshake wasn't really ever done. The integrated handshake,
beside changing the version into 5, has added also the messages, so far
implemented in the form of user-extended messages interchanged after the formal
connection has been established, into the process of UDT handshake. This has
solved the main problem with proper data delivery since the very beginning in
case of encrypted connection. But the most important part is that the sides
using this version no longer require to exchange the extra messages for having
a proper SRT connection, as well as since HSv5 the connection is always
bidirectional. This ensures that the connection is established a little bit
faster than with HSv4 (and still a lot faster than with TCP) and it's encrypted
and has a working latency control since the very beginning.

Due to a problem in the initial implementation, in which the first portion of
handshake exchange the response contained the version specification exactly
copied from the request message, the newer handshake versions since HSv5 must
use the method of setting the oldest supported version in the handshake
induction request, and the type field in the handshake induction response must
be set to a magic in order to confirm that such a version in the induction
response was intentionally set. This is to distinguish it from older clients
using HSv4 only, which were responding with the exactly same data that received
from the induction request, with the exception of the cookie. Future versions
of this library (including forks) are then allowed to break the backward
compatibility with HSv4 and recognize the "UDT4 legacy" clients by not having
the magic value in the type field in the induction response.

