What are groups
===============

A Group is an entity that binds multiple sockets and it is required to
establish a "bonded connection". Groups can be then used the same way as
sockets for performing a transmission. It is then in general stated that a
group is connected as long as at least one member-socket connection is alive,
and as long as this state lasts, some member connections may get broken and
new member connections can be established.

Groups are fully flexible. There's no limitation how many single connections
they can use as well as when you want to establish a new connection. On the
other hand, broken connections are not automatically reestablished. The
application should track the existing connections and reestablish broken ones
if needed. But then, the application is also free to keep as many links as it
wants, including adding new links to the group while it is being used for
transmission, or removing links from the list if they are not to be further
used.

How the links are exactly utilized within the group, it depends on the group
type. The simplest type, broadcast, utilizes all links at a time to send the
same data.


Lay-ground: using sockets for establishing a connection
=======================================================

Important changes
-----------------

Note important changes SRT underwent since the first version from UDT:

1. Specifying family (`AF_INET/AF_INET6`) when creating a socket is no longer
required. The existing `srt_socket` function redirects to a new
`srt_create_socket` function that gets no arguments. The exact family is
decided at the first call to `srt_bind` or `srt_connect` and it's extracted
from the value of `sa_family` field of the `sockaddr` structure passed to
this call.

2. There's no distinction between transmission functions bound to message
or file mode. E.g. all 3 functions: `srt_send`, `srt_sendmsg` and
`srt_sendmsg2` can be used for sending data in any mode - all depends on what
your application needs.


Socket connection
-----------------

Let's review quickly what you do to establish a socket connection in the
caller-listener arrangement.

On the listener side, you create a listening endpoint. Starting with creating
a socket:

```
SRTSOCKET sock = srt_create_socket();
```

The listener needs to bind it first (note: simplified code):

```
sockaddr_in sa = CreateAddrInet("0.0.0.0:5000");
srt_bind(sock, &sa, sizeof sa);
srt_listen(sock, 5);
sockaddr_in target;
SRTSOCKET connsock = srt_accept(sock, &target, sizeof target);
```

The caller side can use default system selected address and simply connect to
the target:

```
SRTSOCKET connsock = srt_create_socket();
sockaddr_in sa = CreateAddrInet("target.address:5000");
srt_connect(connsock, &sa, sizeof sa);
```

After the connection is established, you use the send/recv functions to
transmit the data. In this case we'll utilize the most advanced versions,
`srt_sendmsg2` and `srt_recvmsg2`.

Sender side does:

```
SRT_MSGCTRL mc = srt_msgctrl_default;
packetdata = GetPacketData();
srt_sendmsg2(connsock, packetdata.data(), packetdata.size(), &mc);
```

Receiver side does:

```
SRT_MSGCTRL mc = srt_msgctrl_default;
vector<char> packetdata(SRT_LIVE_DEF_PLSIZE);
int size = srt_recvmsg2(connsock, packetdata.data(), packetdata.size(), &mc);
packetdata.resize(size);
```


Group (bonded) connection
=========================

Except for several details, most of the API used for sockets can be used for
groups. The groups also have the numeric identifiers, just like sockets, which
are in the same domain as sockets, except that there's reserved one bit to
mark that the identifier is for a group, bound to a `SRTGROUP_MASK` symbol.

IMPORTANT: Usually you'll be establishing multiple connections between two
endpoints, just using a different network path - otherwise this simply doesn't
make sense. The simplest method to achieve it is to have multiple network
devices bound to different providers - but still, the listener must bind to
one exactly port using 0.0.0.0 IP, that is, every device in the system. The
goal is to reach this listening point through different target addresses.

Things are different on listener side, however. For listening you are still
using a listening socket:

```
SRTSOCKET sock = srt_create_socket();
```

To handle group connections, you need to set `SRTO_GROUPCONNECT` option:

```
int yes = 1;
srt_setsockflag(sock, SRTO_GROUPCONNECT, &yes, sizeof yes);

sockaddr_in sa = CreateAddrInet("0.0.0.0:5000");
srt_bind(sock, &sa, sizeof sa);
srt_listen(sock, 5);
sockaddr_in target;
SRTSOCKET conngrp = srt_accept(sock, &target, sizeof target);
```

Here the (mirror) group will be created automatically upon the first connection
and `srt_accept` will return its ID (not Socket ID). Further connections in the
same group will be then handled in the background. This `conngrp` returned
here is however the exact ID you will use for transmission.

On the caller side, you start from creating a group first. We'll use the
broadcast group type here:

```
SRTSOCKET conngrp = srt_create_group(SRT_GTYPE_BROADCAST);
```

This will need to make the first connection this way:

```
sockaddr_in sa = CreateAddrInet("target.address.link1:5000");
srt_connect(conngrp, &sa, sizeof sa);
```

Then further connections can be done by calling `srt_connect` again:

```
sockaddr_in sa2 = CreateAddrInet("target.address.link2:5000");
srt_connect(conngrp, &sa2, sizeof sa2);
```

HOWEVER, this method can be so easily used in non-blocking mode, as here
you don't have to wait for the connection to be established. If you do
this in the blocking mode, the first `srt_connect` call will block
until the connection is established. While it can be done this way,
it's usually unwanted.

So for blocking mode we use a different solution. Let's say, you have
3 addresses:

```
sockaddr_in sa1 = CreateAddrInet("target.address.link1:5000");
sockaddr_in sa2 = CreateAddrInet("target.address.link2:5000");
sockaddr_in sa3 = CreateAddrInet("target.address.link3:5000");
```

You have to prepare the array for them and then use one group-connect function:

```
SRT_SOCKGROUPDATA gdata [3] = {
	srt_prepare_endpoint(&sa1, sizeof sa1),
	srt_prepare_endpoint(&sa2, sizeof sa2),
	srt_prepare_endpoint(&sa3, sizeof sa3)
};

srt_connect_group(conngrp, 0, 0, gdata, 3);
```

This does simply the same as `srt_connect`, but blocking rules are different:
it blocks until at least one connection from the given list is established.
Then it returns and allows the group to be used for transmission, while
continuing with the other connections in background (note: some group types may
require minimum conditions to be satisfied, like a minimum number of
connections - just for the record).

If you use non-blocking mode, then `srt_connect_group` is simply the same as
running `srt_connect` in a loop for all required endpoints.

Once the connection is ready, you use the `conngrp` id for transmission, exactly
the same way as above for the sockets.

There's one additional thing to be covered here, though - how much of interest
should be in the application.


Controlling the member connections
==================================

The object of type `SRT_MSGCTRL` is used to exchange some extra information
with the `srt_sendmsg2` and `srt_recvmsg2`; in this case interesting are two
fields:

* `grpdata`
* `grpdata_size`

They have to be set to the pointer and size of an existing `SRT_SOCKGROUPDATA`
type array, which will be filled by this call (you can also obtain it separately
by the `srt_group_data` function). The array must have a maximum possible size
to get information about every single member link, otherwise it will not fill
it back and return the proper size in `grpdata_size`.

The application should be interested here in two types of information:

* The size of the filled array
* The `status` field in every element

From the `status` field you can track every member connection as to whether its
state is still `SRTS_CONNECTED`. If a connection is detected as broken after
the call to a transmission function (`srt_sendmsg2/srt_recvmsg2`) then the
connection will appear in these data only once and the last time with `status`
equal to `SRTS_BROKEN` - in further calls it will not appear anymore, as well
as it won't appear at all if you check the data through `srt_group_data`.

Example:

```
SRT_SOCKGROUPDATA gdata[3];
SRT_MSGCTRL mc = srt_msgctrl_default;
mc.grpdata = gdata;
mc.grpdata_size = 3;
...
srt_sendmsg2(conngrp, packetdata.data(), packetdata.size(), &mc);

for (int i = 0; i < 3; ++i)
    if (mc.grpdata[i].status == SRTS_BROKEN)
        ReestablishConnection(mc.grpdata[i].id);
```

In the above example it is using the socket ID in order to identify the
item in the application's link table, then decide what to do with it. If
it is decided to be revived, this function should call `srt_connect` on it.
The link, however, might be only attempted to be establish, in which case
you'll get first the `SRTS_CONNECTING` status here, and then a failed socket
will simply disappear. Therefore the function should also check how many
items were returned in this array, match them with existing connections,
and distill connections that are unexpectedly not established.
