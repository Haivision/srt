# SRT Connection Bonding: Socket Groups

## Introduction

The general concept of the socket groups means that a separate entity,
parallel to a socket, is provided, and the operation done on a group
will be implemented using appropriate operations done on underlying
sockets.

The groups types generally split into two categories:

1. Bonding groups.

   This group category is meant to utilize multiple connections in order
   to have a group-wise connection. How particular links are then utilized
   to make a group-wise sending, depends on the particular group type. Within
   this category we have the following group types:

    - Broadcast: send the stream over all links simultaneously,
    - Main/Backup: use one link, but be prepared for a quick switch if broken,
    - Balancing: utilize all links, but one payload is sent only over one link (**UNDER DEVELOPMENT!**).

   Bonding category groups predict that a group is mirrored on the peer network
   node, so all particular links connect to the endpoint that always resolves to
   the same target application. Just possibly every link uses a different network
   path.

2. Dispatch groups.

   This category contains currently only one Multicast type (**CONCEPT! NOT IMPLEMENTED!**).

   Multicast group has a behavior dependent on the connection side and it is
   predicted to be only used in case when the listener side is a stream sender
   with possibly multiple callers being stream receivers. It utilizes the UDP
   multicast feature in order to send payloads, while the control communication
   is still sent over the unicast link.

## Details for the Group Types

### 1. Broadcast

This is the simplest bonding group type. The payload sent for a group will be
then sent over every single link in the group simultaneously. On the reception
side the payloads will be sorted out and redundant packets that have arrived
over another link are simply discarded.

This group is predicted to solve the link disturbance problems with no latency
penalty - when one link gets broken, another still works with no extra delay
and no observable disturbances for the client, as long as at least one link is
up and running.

A drawback of this method is that it always utilizes the full capacity of all
links in the group, whereas only one link at a time delivers any useful data.
Every next link in this group gives then another 100% overhead.

### 2. Main/Backup

This solution is more complicated and more challenging for the settings,
and in contradiction to Broadcast group, it costs some penalties.

In this group, only one link out of member links is used for transmission
in a normal situation. Other links may start being used when there's happening
an event of "disturbance" on a link, which makes it considered "unstable". This
term is introduced beside "broken" because SRT normally uses 5 seconds to be
sure that the link is broken, and this is way too much to be used as a latency
penalty, if you still want to have a relatively low latency.

Because of that there's a configurable timeout (with `SRTO_GROUPSTABTIMEO`
option), which is the maximum time distance between two consecutive responses
sent from the receiver back to the sender. If this time was exceeded, the link
is considered unstable. This can mean either some short-living minor
disturbance, as well as that the link is broken, just SRT hasn't a proof of
that yet.

At the moment when one link becomes unstable, another link is immediately
activated, and all packets that have been kept in the sender buffer since
the last ACK are first sent. Since this moment there are two links active
until the moment when the matter finally resolves - either the unstable
link will become stable again, or it will be broken.

The state maintenance always keep up to the following rules:

a) If you happen to have more than one link stable, choose the "best" one
and silence the others. Silencing means that the link is inactive and
payloads are not being sent over it, but the connection is still maintained
and remains ready to take over if there is a necessity.

b) Unstable links continue to be used no matter that it may mean parallel
sending for a short time. This state should last at most as long as it takes
for SRT to determie the link broken - either by getting the link broken by
itself, or by closing the link when it's remaining unstable too long time.

This mode allows also to set link priorities - the greater, the more preferred.
This priority decides mainly, which link is "best" and which is selected to
take over transmission over a broken link before others, as well as which
links should remain active should multiple links be stable at a time.
If you don't specify priorities, the second connected link need not
take over sending, although as this is resolved through sorting, then
whichever link out of those with the same priority would take over when
all links are stable is undefined.

Note that this group has an advantage over Broadcast in that it allows you
to implement link redundancy with a very little overhead, as it keeps the
extra link utilization at minimum. It costs you some penalties, however:

1. Latency penalty. The latency set on the connection used with backup
groups must be at minimum twice the value of `SRTO_GROUPSTABTIMEO` option,
or might even need to be higher in case of high bitrates - otherwise the
switch into the backup link connected with resending all non-ACK-ed packets
might not be on time as required to play them. Your latency setting must be
able to compensate not only usual loss-retransmission time, but also the
time to realize that the link might be broken and time required for resending
all unacknowledged packets, before the time to play comes for the received
packets. If this time isn't met, packets will be dropped and your advantage
of having the backup link might be impaired. According to the tests on the
local network it turns out that the most sensible unstability timeout is about
50ms, while normally ACK timeout is 30ms, so extra 100ms latency tax seems to
be an absolute minimum.

2. Bandwidth penalty. Note that in case when the Backup group activates
another link, it must resend all packets that haven't been acknowledged,
which is simply the least risk taken for a case that a link got suddenly
broken. However, how many packets have been collected, depends on a luck,
and worst case scenario it may need to resend as many packets as it is
normally collected between two ACK events - in case when the link got broken
exactly at the moment when packets were about to be acknowledged. The
link switch always means a large burst of packets to be sent at that
moment - so the mechanism needs large enough time to send them and to
consider them for delivery. However, if your bandwidth limit is too strong,
sending these packets might be dampened possibly too much to live up to
the required time to play. It is unknown as to what recommendations should
be used for that case, although it is usually more required than to
compensate a burst for retransmission and also the maximum burst size
is dependent on the bitrate, in particular, how many packets would be
collected between two acknowledgement events. It might be not that tough
as it seems from this description, as it's about starting a transmission
over an earlier not used link, so there's some chance that the link will
withstand the initial high burst of packets, while then the bitrate will
become stable - but still, some extra latency might be needed to compensate
any quite probable packet loss that may occur during this process.

### 3. Balancing (**UNDER DEVELOPMENT!**)

The idea of balancing means that there are multiple network links used for
carrying out the same transmission, however a single input signal should
distribute the incoming packets between the links so that one link can
leverage the bandwith burden of the other. Note that this group is not
directly used as protection - it is normally intended to work with a
condition that a single link out of all links in the group would not be
able to withstand the bitrate of the signal. In order to utilize a
protection, the mechanism should quickly detect a link as broken so
that packets lost on the broken link can be resent over the others,
but no such mechanism has been provided for balancing group.

As there could be various ways as to how to implement balancing
algorithm, there's a framework provided to implement various methods,
and two algorithms are currently provided:

1. `plain` (default). This is a simple round-robin - next link selected
to send the next packet is the oldest used so far.

2. `window`. This algorithm is performing cyclic measurement of the
minimum flight window and this way determines the "cost of sending"
of a packet over particular link. The link is then "paid" for sending
a packet appropriate "price", which is collected in the link's "pocket".
To send the next packet the link with lowest state of the "pocket" is
selected. The "cost of sending" measurement is being repeated once per
a time with a distance of 16 packets on each link.

There are possible also other methods and algorithms, like:

a) Explicit share definition. You declare, how much bandwidth you expect
the links to withstand as a percentage of the signal's bitrate. This
shall not exceed 100%. This is merely like the above Window algorithm,
but the "cost of sending" is defined by this percentage.

b) Bandwidth measurement. This relies on the fact that the current
sending on particular link should use only some percentage of its
overall possible bandwidth. This requires a reliable way of measuring
the bandwidth, which is currently not good enough yet. This needs to
use a similar method as in "window" algorithm, that is, start with
equal round-robin and then perform actively a measurement and update
the cost of sending by assigning so much of a share of the signal
bitrte as it is represented by the share of the link in the sum of
all maximum bandwidth values from every link.

### 4. Multicast (**CONCEPT! NOT IMPLEMENTED!**)

This group - unlike all others - is not intended to send one signal
between two network nodes over multiple links, but rather a method of
receiving a data stream sent from a stream server by multiple receivers.

Multicast sending is using the feature of UDP multicast, however the
connection concept is still in force. The concept of multicast groups
is predicted to facilitate the multicast abilities provided by the router
in the LAN, while still maintain the advantages of SRT.

When you look at the difference that UDP multicast provides you towards
a dedicated peer-to-peer sending, there are two:

* You can join a running transmission at any time, without having
the sender do something especially for you (the router IGMP subscription
does the whole job).

* The data stream is sent exactly ONCE from the stream sender to the
router, while the router sends also one data stream to the switch. How
much of a burden to the rest it is, depends then on the switch: older
ones get one signal to be picked up by those interested, newer ones
pass through this signal to only those nodes that have IGMP subscription.
Nevertheless, the advantage here is that the same data stream is sent
once instead of being sent multiple times over the same link, at least
between the stream sender and the router.

The multicast groups in SRT are intended to use this very advantage.

While the connection still must be maintained as before, the dedicated
UDP link that results from it is to carry out only the control traffic. For the
data traffic there would be a UDP multicast group IP address established and
all nodes that connect to the stream sender using a multicast group will then
receive the data stream from the multicast group.

This method has limitations on the connection setup. You should then
make a listener on the side where you want to have a stream sender, and
set it up for multicast group. Then, a connection is established over
an individual link, as usual. But beside the data that would be sent
over a dedicated link, the data being sent to the group on the sender
side will be actually sent to the multicast address (unlike in Backup
and Broadcast groups, where these are normally sent over the channel
that particular socket is using). The connecting receiver party is then
automatically subscribed to this group and it receives the data packets
over there, just as if this would be a second channel over which the
group is able to receive data.

Note that sending the data over a single link is still possible and
likely used for retransmission. The retransmission feature is still
handled on a single link, although most likely it can be allowed that
if more than 2 links report a loss of the exactly same packet, the
retransmission may use the multicast link instead of the individual
link - whichever would spare more bandwidth would be used.

Potential implementation:

Instead of `groupconnect` option set to true, you have to set the
`multicast` option to a nonempty string of any contents (just limited
to 512 characters). This string value will be a key under which the
group ID is recorded. The first connecting client will be identified
as joining this group and this way a new group will be created and
its ID recorded under this key. This is in order to allow library
users to use the feature of listener callback so that a group name
may be decided upon per user or stream resource specified.

Once the group is known, and the connection is to be accepted, the
newly accepted socket is joined to the group. The accept conditions
are just like before in the groups: the group is reported from
`srt_accept` when the first client has connected.

Once the group connection is made, the sender side can use the
group ID to send the data. Note that the group on the listener side
collects multiple sockets representing particular clients, so
sending will be for the sake of each of them. However the sending
function will not send to every individual socket, as it is with
broadcast group, but rather an extra socket will be created
inside the group, and this one will be used to send packets to
the multicast group, as configured during the handshake.

It should be decided on the listener side, which IGMP group will
be used for transmission. Clients do not know it, and:

 - for simple single-signal cases, you have just one IGMP
group configured for the listener and it's used for every
client
 - listener callback may decide particular IGNMP group to
be used for particular signal

The listener side will inform the client in the handshake,
which IGMP group will be used for transmission so that the
client can set up listening on it (join IGMP group in particular).

The client side should have then two things:

1. The group with its own socket (and queues).
2. The group will contain always one member, this time.
3. When reading packets from the queue of the socket bound
to the IGMP group, the packet will be translated and dispatched
to the socket.

What is important here is that the packet sent by the listener
side will look exactly the same no matter to which client it is
predicted - which means that the SRT header will contain the same
data. This is where groups come in: the "target ID" in the header
will have the value of the group.

In total, the listener sends the following data to the client:

1. IGMP group's IP address and port
2. group ID that will be the same on both sides (if the group ID
already exists on the client's application, the connection will be
rejected).

The listener side will then send payload packets to the IGMP group,
however all control packets will be still sent the same way as before,
that is, over a direct connection.

## Socket Groups in SRT

The general idea of groups is that there can be multiple sockets belonging
to a group, and various operations, normally done on single sockets, can
be simply done on a group. How an operation done on a group is then 
implemented by doing operations on sockets, depends on the group type and
the operation itself.

Groups have IDs exactly the same as sockets do. For group support there's a
feature added: the SRTSOCKET type is alias to `int32_t`, and it has appropriate
bit set to define that this ID designates a group, known as `SRTGROUP_MASK`.
You can test it by `id & SRTGROUP_MASK` to know if the value of `SRTSOCKET`
type designates a single socket or a group.

For groups you simply use the same operations as for single socket - it will
be internally dispatched appropriate way, depending on what kind of entity
was used. For example, when you send a payload, it will be effectively sent:

- For Broadcast group, over all sockets,
- For Main/Backup group, over all currently active links,
- For Balancing group, over a currently selected link,
- For Multicast group, over an extra socket to the multicast group.

Similarly, the reading operation will read over all links and due to
synchronized sequence numbers use them to decide the payload order: when
the very next packet has been receiver over any link, it will be delivered,
and when older than that, it will be discarded. The TSBPD mechanism is used to
determine the order in case when a packet was decided to be dropped on
particular link. That is, if a packet drop occurs, then simply the same
packet received over another link will be still earlier ready to play.

The difference in reading between groups is that:

- For Broadcast and Main/Backup groups, sequence numbers are synchronized and
used to sort packets out

- For Balancing group, message numbers are used to sort packets out
(sequence numbers are not synchronized)

- For Multicast group, there's only one link at the receiver side group,
just the group contains additional socket that should read from the multicast
group, in case when packets are expected to be read. By having the target
specified as the group id, it gets correctly dispatched to this channel's own
buffer and delivered. For this purpose, however, payloads sent over the
multicast link must have the target defined as the group ID so that all data in
the header look exactly the same way depite being intended to be received by
various different network nodes.

## How to Prepare Connection for Bonded Links

In the listener-caller setup, you have to take care of the side separately.

First of all, the caller should require minimum version from the peer
(see `SRTO_MINVERSION` flag) or otherwise the listener won't understand the
handshake extension information concerning socket groups.

The listener socket must have `SRTO_GROUPCONNECT` flag set. There are two
reasons as to why it is required:

1. This flag **allows** the socket to accept bonded connections. Without this
flag the connection that attempts to be bonded will be rejected.

2. When `srt_accept` function is being called on a listener socket that has
this flag set, the returned numeric ID identifies not a socket, but a group.

The group returned by `srt_accept` is a local group mirroring the remote group
which's ID is received in the handshake. That's also apparently the exact ID
that you should next use for sending or receiving data. Note also that
`srt_accept` returns the group only once upon connection - once there exist at
least one connected socket in the group, there will be no new connections
reported in `srt_accept`, just rather a new socket will suddenly appear in the
group data next time you anywhow read them (data reported from `srt_recvmsg2`,
`srt_sendmsg2` or `srt_group_data`).

When an accepted-off socket is being created and the group for which the request
has come in the handshake is already mirrored in the application, then this
link will be added to the existing group. Otherwise the group is created anew.
This group has a range for a single application.

You rather don't have to maintain the list of bonded connections at the
listener side because still you have no influence on who and when will connect
to you.

On the caller the matter is a little bit more complicated.

## Connect Bonded

At first, please remember that the official function to create a socket is now
`srt_create_socket` and it gets no arguments. All previous functions to create
a socket are deprecated. This is also interconnected with a change that you
don't have to know the IP domain of the socket when you create it - it will be
decided only upon the first call of `srt_bind` or `srt_connect`.

In order to have a bonded connection, instead of creating a socket, you have
to create a group. Groups may have various purposes, and not every type of
group is meant to be self-managed. For groups that are not self-managed, the
socket can be only simply added to the group by application, as well as the
application should take care of closing it when broken or no longer needed. The
groups for bonding are always self-managed, that is, they create sockets
automatically upon necessity, and automatically delete sockets that got broken.

For example, this way you create a bonding group of type "broadcast":

	grpid = srt_create_group(SRT_GTYPE_BROADCAST);

This returns a value of SRTSOCKET, which can be then normally used in most of
the socket operations, just like socket ids. In order to establish a connection
within the bonding group, simply do:

	sockid = srt_connect(grpid, address...);

Mind that in distinction to `srt_connect` used on a socket, where it returns 0
when succeeded, this time it returns the socket ID of the socket that has been
created for the purpose of this connection. This is just informative and the
application is given this socket just when it would need it, although it should
never try to do any operation on the socket, including closing it.

In order to create a second link within the bonding group, simply call
`srt_connect` again with the other address that eventually refers to the
same endpoint (note that SRT has no ability to verify it prematurely). Unlike
sockets, a group can be connected many times, and every call to this function
creates a new connection within the frames of this group. Also, as a managed
group, it always creates a new socket when you connect "the group" to the given
address.

The library, on the other hand, doesn't know how many connections you'd like
to maintain, whether the list is constant or can be dynamically modified, or
whether a dead link is not to be revived by some reason - all these things are
out of the interest of the library. It's up to the application to decide
when and by what reason the connection is to be established. All that your
application has to do is to monitor the conenctions (that is, be conscious
about that particular links are up and running or get broken) and take
appropriate action in response.

Therefore it's recommended that your application maintain the bonded link
table. It is completely up to you, if your list of bonded links is static or
may change in time during transmission; what matters is that you can always add
a new connection to the bonding group at any time by calling `srt_connect`,
and when a connection is dead, you'll be informed about it, but the link won't
be automatically revived.

There are some convenience function added though because of inability to do
operations on a single socket in case of groups at the moment when they are
required.

1. `srt_connect_group`. This does something similar to calling `srt_connect`
in a loop for multiple endpoints. However the latter is inappropriate in case
when you use the blocking mode because this would block on the first connection
attempt and will not try the next one until the previous one is connected or
the connection finally fails. This function will then try to connect to all
given endpoints at once and will block until the "group connection state" is
achieved (in case of broadcast group it simply means that at least one link
is connected - other groups may use sometimes more complicated conditions
to be satisfied for it). In non-blocking mode it will simply behave the same
as `srt_connect` run in a loop.

You have to make yourself an array with endpoints, then prepare every endpoint
using `srt_prepare_endpoint` function.

2. `srt_connect_bind`. It does the same as calling first `srt_bind` on the
source address, then `srt_connect` on the destination address, when it's
called for a single socket. When it's called for a group, then the binding
procedure is done on the newly created socket for that connection (and that's
the only way how you can define the outgoing port for a socket that belongs
to a managed group).

## Maintaining Link Activity

A link can get broken, and the only thing that the library does about it is
make you aware of it. The bonding group, as managed, will simply delete the
broken socket and that's all. Reconnecting of the broken link is completely up
to the application. Your application may also state that the link need not be
revived, so this isn't interesting for the application. If you want to revive
the link and you believe that the connection can be still made, or it's only
broken temporarily, or the link should work, simply connect to this address
again using `srt_connect`.

The simplest way to maintain the status of the sockets in the group is to call:

	srt_group_data(grpid, &sockdata, &sockdata_size);

You have to prepare an array of `SRT_SOCKGROUPDATA` type by yourself, and the
size must be properly set to `sockdata_size` before calling to at least the
number of sockets in the group. The latter is input-output, that is, it will
return the actual size of the array, possibly less than given size. If you pass
too small size, then the required size will be returned in `sockdata_size`, but
the array will not be modified at all. Therefore you must check the returned
size always and catch that case, as if this happens, you should not read data
from the array.

When you call `srt_group_data` and the size of the group is less than your last
remembered one, it means that one of the sockets got broken, which one, you can
check by seeing which of the sockets, that you remembered at the time of
connection, is lacking in the socket group data. Note that socket IDs are
created using a random number and decreasing, so dangling socket IDs will not
be reassigned to correct sockets in a "predictable time" (you'll have to create
and delete sockets about a million times to make it happen).

A recommended way is, however, to use `srt_sendmsg2` and `srt_recvmsg2`
functions, which require `SRT_MSGCTRL` structure. You should place a
`SRT_SOCKGROUPDATA` array into `SRT_MSGCTRL::grpdata` field together with its
size in `SRT_MSGCTRL::grpdata_size`, and the socket information for every
socket will be placed there, including (just once) sockets that were lately
broken and have been deleted. This last information is not present in the
result returned by `srt_group_data` - that is, sockets found broken during
the operation will be only present if you review the array that was filled
by `srt_sendmsg2` or `srt_recvmsg2`.

## Writing Data to a Bonded Link

This is very simple. Call the sending function (recommended is `srt_sendmsg2`)
to send the data, passing group ID in the place of socket ID. By recognizing
the ID as group ID, this will be resolved internally as sending the payload
by approprately using the bonded links as defined for particular group type.

The current implementation for most of the bonding groups (broadcast and
backup) relies on synchronizing the sequence numbers of the packets so that
particular payload goes always with the same sequence number on all links.

Every next link, when the group is already connected, will be first created as
"idle", and it will be activated when the opportunity and the need comes,
and at this very time will be the sequence number adjusted to match the master
sequence number in the group. The same payload will then have the same
sequence number in all sockets in the bonding group, which allows then the
payload to be retrieved in order.

Exceptionally, the current implementation of the balancing group type is
using message numbers because packets must go in order of the sequence numbers
on particular link - and in this group type packets are distributed
throughout the link and never go in the order of scheduling on one link.
Therefore this group uses message numbers for ordering.

## Reading Data from a Bonded Link

This is also simple from the user's perspective. Simply call the reading
function, such as `srt_recvmsg2`, passing the group ID instead of socket
ID.

Also the dillema of blocking and nonblocking is the same thing. With blocking
mode (`SRTO_RCVSYN`), simply wait until your payload is retrieved. The internal
group reading facility will take care that you get your payload in the right
order and at the time to play, and the redundant payloads retrieved over
different links simultaneously will be discarded.

## Checking the Status

If you call `srt_sendmsg2` or `srt_recvmsg2`, you'll get the status of every
socket in the group in a part of the `SRT_MSGCTRL` structure, where you should
set the pointer to your array of `SRT_SOCKGROUPDATA` type, and its size, so
that the status can be filled. The size of the array should simply correspond
to the number of bonded links that you use. If the passed size is too small,
then the `grpdata` field will be set to `NULL` by the call, whereas `grpdata_size`
will be always set to the required size.

In this structure you have:

- `id`: socket ID
- `status`: the `SRT_SOCKSTATUS` value (same as obtained by `srt_getsockstate`)
- `result`: result of the operation; if you can see -1 there, it means that you
can see this socket for the last time here, and it is **already closed**.
- `peeraddr`: the address to which the socket was connected

The whole idea of bonding is that a link may get broken at any time and
despite this, your application should be still capable of sending or receiving
data, when at least one link is still alive. It means then that when reading
or writing, the "sub-operation" on one of the sockets in the bond might have
failed due to that the link got broken. But if the operation can continue
successfully on another link, the overall operation is still considered
succeeded, though you might be interested of what happened on the link. Only if
**all links** get broken is the operation considered failed.

There are some differences as to what exactly happens in this case, depending
on the socket group type:

1. Broadcast: the data are being sent over all links anyway, so it doesn't make
much difference except that the broken socket must be taken care of.

2. Main/Backup: usually when a socket is broken, there has been a disturbance
notified much earlier and therefore another link already active. The bonding
group is allowed to keep as many active links as required for at least one
link to remain stable. A broken socket is then simply a possible resolution for
a volatile "unstable" state of the member socket.

3. Balancing: the exact implementation of this group is still underway and
what exactly happens may depend on possible subtype of this group. The current
proof-of-concept implementation behaves like the broadcast group - if one
of the links goes broken, then there are less members to distribute packets
through. Usually the group may have defined some critical conditions that must
be satisfied so that the transmission can continue, mainly basing on that the
critical network capacity needed for transmission is provided. In this case, if
the bonded capacity drops below critical capacity, the whole bonded link should
get broken.

On the listener side, the situation is similar. When you read as listener, you
still read if at least one link is alive, when you send - sending succeeds when
at least one link is alive. When the link gets broken, though, you can't do
anything anyway, so the listener doesn't have to worry about anything, except
the situation that all links are gone, but this is then reported just like in
a situation of one socket - the reading or writing operation fails. Only then
is the connection lost completely and is sending or receiving impossible.

Most important is what the caller side should do. When your link gets broken,
it's up to you to restore it, so you should do `srt_connect` for that link
again and count on that it will be re-established, while simultaneously the
transmission continues over existing links.

A single call to `srt_connect` may also break, like any other operation. When
it happens while another link is running, this link will simply never reach
the state of "idle", and will be deleted before it could be used.

And finally, a group can be closed. In this case, it internally closes first
all sockets that are members of this group, then the group itself is deleted.

## Application Support

Currently only the `srt-test-live` application is supporting a syntax for
socket groups.

The syntax is as usual with "source" and "target", however you can specify
multiple sources or multiple targets when you want you want to utilize
socket groups. For this case, the `-g` option is predicted, which should
be best understood as a split-point between specification of source and
target.

The general syntax (there will be also a simplified syntax, so read on) when
you want to have a source signal as a group:

```
./srt-test-live <SRT-link1> <SRT-link2> -g <target-URI>
```

and for sending over a groupwise link:

```
./srt-test-live <source-URI> -g <SRT-link1> <SRT-link2> ...
```

Using multiple SRT URI specifications here is still simplified because the
most direct (but hardest in use) method to specify a groupwise link is:

```
srt:////group?type=<grouptype>&nodes=host1:port1,host2:port2 (&other=options...)
```

Which would be the same as specifying:

```
srt://host1:port1 srt://host2:port2
```

except that options that are group related must be specified the same way
in every URI.

But, as this can be handled with SRT type URI only, and as usually single
socket options apply the same for every link anyway, there's a simplified
syntax - HIGHLY RECOMMENDED - for specifying the group - let's take an
example with additionally setting the `latency` option (REMEMBER: when
specifying the argument with `&` inside in the POSIX shell, you need to enclose
it with apostrophes or put backslash before it):

```
srt://*?type=broadcast&latency=500 host1:5000 host2:5000 host3:5000
```

By specifying the SRT URI with placing `*` as a host, you define this as
a "header URI" for the whole group. The nodes themselves are then specified
in the arguments following this one. The list of nodes is terminated either
by the end of arguments or other options, including the `-g` option that
can be followed by the target URI specification, in case when the group
was specified as a source.

So, a complete command line to read from a group connected over links
to hosts "alpha" and "beta", both with port 5000, and then resending it
to local UDP multicast `239.255.133.10:5999` would be:

```
./srt-test-live srt://*?type=broadcast alpha:5000 beta:5000 -g udp://239.255.133.10:5999
```

Note that this specifies the caller. On the side where you want to
set up a listener where you'd receive a caller's connection you must
set the `groupconnect` option (here let's say you get the source signal
from a device that streams to this machine to port 5555):

```
./srt-test-live udp://:5555 srt://:5000?groupconnect=true
```

At the caller side you can also use some group-member specific options.
Currently there exists only one option dedicated for the Backup group
type, which is priority parameter with a `weight` key. In the simplified
syntax it should be attached to the member parameter:

```
./srt-test-live srt://*?type=backup alpha:5000?weight=0 beta:5000?weight=1 -g udp://239.255.133.10:5999
```

Priorities in the Backup group type define which links should be preferred
over the others when deciding to silence links in a situation of multiple
stable links. Also at the moment when the group is connected, the link with
highest priority is preferred for activation (greatest weight value), and if
another link is connected with higher priority it also takes over.

Here the `beta` host has higher priority than `alpha`, so when both
links are established, it should use the host `beta` to send the data,
switch to `alpha` when this one is broken, and then switch back to `beta`,
when this link is back online.

The stability timeout can be configured through `groupstabtimeo` option.
Note that with increased stability timeout, the necessary latency penalty
grows as well.
