# What are groups ?


A Group is an entity that binds multiple sockets, and is required to
establish a "bonded connection". Groups can be used in the same way as
sockets for performing a transmission. A group is connected as long as at
least one member-socket connection is alive. As long as a group is in the
connected state, some member connections may get broken and new member
connections can be established.

Groups are fully flexible. There's no limitation how many single connections
they can use as well as when you want to establish a new connection. On the
other hand, broken connections are not automatically reestablished. The
application should track the existing connections and reestablish broken ones
if needed. But then, the application is also free to keep as many links as it
wants, including adding new links to the group while it is being used for
transmission, or removing links from the list if they are not to be further
used.

How the links are utilized within a group depends on the group type. The
simplest type, broadcast, utilizes all links at once to send the same data.

To learn more about socket groups and their abilities, please read the
[detailed document](socket-groups.md).

# Reminder: Using sockets for establishing a connection


Before we begin, let's review first how to establish a connection for a
single socket.

## Important changes

Keep in mind these important changes to SRT:

1. Specifying family (`AF_INET/AF_INET6`) when creating a socket is no longer
required. The existing `srt_socket` function redirects to a new
`srt_create_socket` function that takes no arguments. The family is
decided at the first call to `srt_bind` or `srt_connect`, and is extracted
from the value of the `sa_family` field of the `sockaddr` structure passed to
this call.

2. There's no distinction between transmission functions bound to message
or file mode. E.g. all 3 functions: `srt_send`, `srt_sendmsg` and
`srt_sendmsg2` can be used for sending data in any mode - all depends on what
your application needs.


## Socket connection

Let's review quickly how to establish a socket connection in the
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


# Group (bonded) connection

Except for several details, most of the API used for sockets can be used for
groups. Groups also have numeric identifiers, just like sockets, which
are in the same domain as sockets, except that there is one bit reserved to
indicate that the identifier is for a group, bound to a `SRTGROUP_MASK` symbol.

IMPORTANT: Socket groups are designed to utilize specific features. The
broadcast or backup group are designed to provide link redundancy (to keep
transmission running in case when one of the links gets broken). The balancing
groups allow to share the bandwidth load between links. In order to be able to
utilize any of these features, every member link in the group must be routed
through a different network path. Some terminal parts of these links can be
common for them all - but if so, for these parts these features will not be
used: a broken network path in this part would break all links at once, and
the "balanced" traffic will go through one route path as a whole anyway. SRT
has no possibility to check if you configured your links right. This means
that on the caller side you need to use different target address for every
link, while on the listener side you should use a different network device
for every link.

For the listener side, note that groups only replace the communication socket.
Listener sockets still have to be used:

```
SRTSOCKET sock = srt_create_socket();
```

To handle group connections, you need to set `SRTO_GROUPCONNECT` option:

```
int gcon = 1;
srt_setsockflag(sock, SRTO_GROUPCONNECT, &gcon, sizeof gcon);

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

IMPORTANT: This method can be easily used in non-blocking mode, as
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
	srt_prepare_endpoint(NULL, &sa1, sizeof sa1),
	srt_prepare_endpoint(NULL, &sa2, sizeof sa2),
	srt_prepare_endpoint(NULL, &sa3, sizeof sa3)
};

srt_connect_group(conngrp, gdata, 3);
```

This does the same as `srt_connect`, but blocking rules are different:
it blocks until at least one connection from the given list is established.
Then it returns and allows the group to be used for transmission, while
continuing with the other connections in the background. Note that some group 
types may require certain conditions to be satisfied, like a minimum number of
connections.

If you use non-blocking mode, then `srt_connect_group` behaves the same as
running `srt_connect` in a loop for all required endpoints.

Once the connection is ready, you use the `conngrp` id for transmission, exactly
the same way as above for the sockets.

There's one additional thing to be covered here: just how much 
should the application be involved with socket groups?


# Controlling the member connections

The object of type `SRT_MSGCTRL` is used to exchange some extra information with 
`srt_sendmsg2` and `srt_recvmsg2`. Of particular interest in this case are two fields:

* `grpdata`
* `grpdata_size`

These fields have to be set to the pointer and size of an existing `SRT_SOCKGROUPDATA`
type array, which will be filled by this call (you can also obtain it separately
by the `srt_group_data` function). The array must have a maximum possible size
to get information about every single member link. Otherwise it will not fill and 
return the proper size in `grpdata_size`.

The application should be interested here in two types of information:

* the size of the filled array
* the `status` field in every element

From the `status` field you can track every member connection as to whether its
state is still `SRTS_CONNECTED`. If a connection is detected as broken after
the call to a transmission function (`srt_sendmsg2/srt_recvmsg2`) then the
connection will appear in these data only once, and with `status`
equal to `SRTS_BROKEN`. It will not appear anymore in later calls, and it won't 
appear at all if you check the data through `srt_group_data`.

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

In the above example the socket ID is used to identify the
item in the application's link table, at which point a decision is made. If
the connection is to be revived, this function should call `srt_connect` on it.

There might be only an attempt to establish the link, in which case
you'll get first the `SRTS_CONNECTING` status here, and then a failed socket
will simply disappear. Therefore the function should also check how many
items were returned in this array, match them with existing connections,
and filter out connections that are unexpectedly not established.
