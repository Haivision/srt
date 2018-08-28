
Abstract
========

Redundancy is a technique of sending the data simultaneously over more than
one network link. This is mainly predicted to maintain the possible problem of
uncertainty of the link stability, which may also help, in extreme cases, with
the "spike" problems resulting in short-living network congestion. In particular,
when it happens that one of the links unexpectedly delays or gets broken, the
data should be still continuously received over the other link, with no observable
disturbances for the client, as long as at least one link is up and running.


Socket groups in SRT
====================

The general idea of groups is that there can be multiple sockets belonging
to a group, and various operations, normally done on single sockets, can
be simply done on a group. How an operation done on a group is then 
implemented by doing operations on sockets, depends on the group type and
the operation itself.

Groups have IDs exactly the same as sockets do. For group support there's a
feature added: the SRTSOCKET type is alias to `int32_t`, and it has appropriate
bit set to define that this ID designates a group, known as `SRTGROUP_MASK`.
Test it by `id & SRTGROUP_MASK` to know if the value of `SRTSOCKET` type
designates a single socket or a group.

In case of redundancy, the socket group is used to collect multiple sockets,
each one for one link. Then the sending operation done on a group sends the
payload over all of the underlying sockets, minding only that the given payload
will be sent in one UDP packet with sequence number identical on all sockets
in the group. Similarly, the reading operation reads a payload over all links
one after another, tracing the sequence numbers of the packet. If the sequence
number of the packet is greater than the last delivered, it's switched and
the payload is delivered to the application; otherwise the payload is
discarded (as duplicated).


How to prepare connection for redundancy
========================================

In the listener-caller setup, you have to take care of the side separately.

The listener socket must have `SRTO_GROUPCONNECT` flag set. This flag simply
**allows** the socket to accept redundant connections. Without this flag the
connection that attempts to be redundant will be rejected. The caller should
also require minimum version from the peer, otherwise it simply won't understand
the handshake extension information concerning redundancy.

When a listener is connected, and the group ID is passed in the handshake,
the resulting accepted socket isn't returned in `srt_accept`. The returned
value is the group ID of the group that is a local group mirroring the group
which's ID is received in the handshake. This group is always returned by the
`srt_accept` in such a case, although it may be an existing or newly created
local group. Note that logically it's the exact ID that you should next use
for sending or receiving data.

When a socket for accept is being created, and the group in the handshake
information **is already mirrored** in the application anywhere (there's a
global group list per application, just like the socket list), then the
newly created accepted socket for the connection is automatically added to
this group, otherwise the group is newly created. It doesn't matter then,
on how many sockets you listen at a time, and whether you listen on different
network devices.

Normally when your `srt_accept` returns with a value, all you have to do is
to check if this is some existing group (returned already earlier by
`srt_accept`), or a new group. This may matter for the statistics and when
you can see a "second connection" with an existing group, you can simply
ignore it - all required things that make the link added to the redundancy
group is done automatically.

You rather don't have to maintain the list of redundant connections at the
listener side because still you have no influence on who and when will connect
to you.

POSSIBLE EXTENSION: `srt_group_accept` might be created for a case when
you have multiple listener sockets, possibly each one on a different 
network device an port, and you want to get returned when a socket was
accepted from whichever of them, and automatically added to the group.
This would be useful only in blocking mode because in non-blocking mode
you simply receive `SRT_EPOLL_OUT` bit set for the exact listener socket that
received the connection, so then you simply do `srt_accept` on that very socket.

On the caller the matter is a little bit more complicated.


Connect redundant
=================

I remind that the function to create a new socket is currently `srt_create_socket`
and it gets no arguments. All previous functions to create a socket are deprecated.

In order to connect with redundancy, instead of creating a socket, you have
to create a group. Groups may have various purposes, and not every type of
group is meant to be self-managed. For groups that are not self-managed, the
socket can be only simply added to the group by application, as well as the
application should take care of closing it when broken or no longer needed. The
groups for redundancy are always self-managed, that is, they create sockets
automatically upon necessity, and automatically delete sockets that got broken.

For redundancy then, you create a group this way:

	grpid = srt_create_group(SRT_GTYPE_REDUNDANCY);

This returns a value of SRTSOCKET, which can be then normally used in most of
the socket operations, just like socket ids. In order to establish a connection
within the redundancy group, simply do:

	sockid = srt_connect(grpid, address...);

Mind that in distinction to `srt_connect` used on a socket, where it returns 0
when succeeded, this time it returns the socket ID of the socket that has been
created for the purpose of this connection. This is just informative and the
application is given this socket just when it would need it, although it should
never try to do any operation on the socket, including closing it.

In order to create a second link within the redundancy group, simply... call
`srt_connect` again! Unlike socket, a group can be connected many times, and
every call to this function creates a new connection within the frames of this
group. Also, as a managed group, it always creates a new socket when you
connect "the group" to the given address.

The library, on the other hand, doesn't know how many connections you'd like
to maintain, whether the list is constant or can be dynamically modified, or
whether a dead link is not to be revived by some reason - all these things are
out of the interest of the library. It's up to the application to decide
when and by what reason the connection is to be established. All that your
application has to do is to monitor the conenctions (that is, be conscious
about that particular links are up and running or get broken) and take
appropriate action in response.

Therefore it's recommended that your application maintain the redundancy link
table. It is completely up to you, if your list of redundant links is static or
may change in time during transmission; what matters is that you can always add
a new connection to the redundancy group at any time by calling `srt_connect`,
and when a connection is dead, you'll be informed about it, but the link won't
be automatically revived.

Note: for convenience, there has been introduced a new API function,
`srt_connect_bind`, which does the same as calling first `srt_bind` on the
source address, then `srt_connect` on the destination address, when it's
called for a single socket. When it's called for a group, then the binding
procedure is done on the newly created socket for that connection (and that's
the only way how you can define the outgoing port for a socket that belongs
to a managed group).


Maintaining link activity
=========================

A link can get broken, and the only thing that happens from the library point
of view is to make you aware of it. The redundancy group, as managed, will
simply delete the broken socket and that's all. Reconnecting of the broken link
is completely up to the application. Your application may also state that the
link need not be revived, so this isn't interesting for the application. If
you want to revive the link and you believe that the connection can be still
made, or it's only broken temporarily, or the link should work, simply connect
to this address again using `srt_connect`.

The simplest way to maintain the status of the sockets in the group is to call:

	srt_group_sockets(grpid, &sockdata, &sockdata_size);

You have to prepare an array of `SRT_SOCKGROUPDATA` type by yourself, and the size
must be properly set to `sockdata_size` before calling to at least the number of 
sockets in the group. The latter is input-output, that is, it will return the
actual size of the array, possibly less than given size. If you pass too small
size, then the required size will be returned in `sockdata_size`, but the array
will not be modified at all.

That's why you should remember values from `srt_connect`. If you get the socket
ID from this, you should remember it as a member of the group. When you call
`srt_group_sockets` and the size of the group is less than your last remembered
one, it means that one of the sockets got broken, which one, you can check by
seeing which of the sockets that you remembered at the time of connection, is
lacking in the socket group data. Note that socket IDs are created using a random
number and decreasing, so dangling socket IDs will not be reassigned to correct
sockets in a "predictable time" (you'll have to create and delete sockets about
a million times to make it happen).

A recommended way is, however, to use `srt_sendmsg2` and `srt_recvmsg2`
functions, which require `SRT_MSGCTRL` structure. You should place a
`SRT_SOCKGROUPDATA` array into `SRT_MSGCTRL::grpdata` field together with its
size in `SRT_MSGCTRL::grpdata_size`, and the socket information for every
socket will be placed there, including (just once) sockets that were lately
broken and have been deleted. This last information is not present in the
result returned by `srt_group_sockets` and no sockets with result -1, that is,
last time seen as broken, will be present in this case.


Writing data to a redundant link
================================

This is very simple. Call the sending function (recommended is `srt_sendmsg2`)
to send the data, passing group ID in the place of socket ID. By recognizing
the ID as group ID, this will be resolved internally as sending the payload
over all connected sockets in the group.

The sequence number management is specifically done for that purpose. The
first working link gets its sequence numbers maintained. Every next link
will be first created as "idle", and it will be activated at the next
opportunity of sending - at this very time will be the sequence number
adjusted to match the master sequence number in the group. Effectively
the same payload will have the same sequence number in all sockets in
the redundancy group, which allows then the payload to be retrieved in order.


Reading data from a redundant link
==================================

This is also simple from the user's perspective. Simply call the reading
function, such as `srt_recvmsg2`, passing the group ID instead of socket
ID.

Also the dillema of blocking and nonblocking is the same thing. With blocking
mode (`SRTO_RCVSYN`), simply wait until your payload is retrieved. The internal
group reading facility will take care that you get your payload in the right
order and at the time to play, and the redundant payloads retrieved over
different links simultaneously will be discarded.


Checking the status
===================

If you call `srt_sendmsg2` or `srt_recvmsg2`, you'll get the status of every
socket in the group in a part of the `SRT_MSGCTRL` structure, where you should
set the pointer to your array of `SRT_SOCKGROUPDATA` type, and its size, so
that the status can be filled. The size of the array should simply correspond
to the number of redundant links that you use. If the passed size is too small,
then the `grpdata` field will be set to `NULL` by the call, whereas `grpdata_size`
will be always set to the required size.

In this structure you have:

- `id`: socket ID
- `status`: the `SRT_SOCKSTATUS` value (same as obtained by `srt_getsockstate`)
- `result`: result of the operation; if you can see -1 there, it means that you
can see this socket for the last time here, and it is **already deleted**.
- `peeraddr`: the address to which the socket was connected

The whole idea of redundancy is that a link may get broken at any time and
despite this, your application should be still capable of sending or receiving
data, when at least one link is still alive. It means then that when reading
or writing, the "sub-operation" on one of the redundant sockets might have
failed due to that the link got broken. But if the operation on another link
succeeded, the overall operation is still considered succeeded, though you
might be interested of what happened on the link. Only if **all links** get
broken is the operation considered failed.

On the listener side, the situation is similar. When you read as listener, you
still read if at least one link is alive, when you send - sending succeeds when
at least one link is alive. When the link gets broken, though, you can't do
anything anyway, so the listener doesn't have to worry about anything, except
the situation that all links are gone, but this is then reported just like in
a situation of one socket - the reading or writing operation fails. Only then
is the connection lost completely and is sending or receiving impossible.

Most important is what the caller side should do. When your link gets broken,
it's up to you to restore it, so you should do `srt_connect` for that link
again and count on that it will be established, while simultaneously the
transmission continues over existing links.

A single call to `srt_connect` may also break, like any other operation. When
it happens while another link is running, this link will simply never reach
the state of "idle", and will be deleted before it could be used.

And finally, a group can be closed. In this case, it internally closes first
all sockets that are members of this group, then the group itself is deleted.



