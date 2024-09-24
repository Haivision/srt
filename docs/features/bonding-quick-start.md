# SRT Connection Bonding: Quick Start

## 1. Introduction

Similar to SMPTE-2022-7 over managed networks, Connection Bonding adds seamless stream protection and hitless failover to the SRT protocol. This technology relies on more than one IP network path to prevent disruption to live video streams in the event of network congestion or outages, maintaining continuity of service.

This is accomplished using the [socket groups](./socket-groups.md) introduced in [SRT v1.5](https://github.com/Haivision/srt/releases/tag/v1.5.0). The general concept of socket groups means having a group that contains multiple sockets, where one operation for sending one data signal is applied to the group. Single sockets inside the group will take over this operation and do what is necessary to deliver the signal to the receiver.

Two modes are supported:

- [Broadcast](./socket-groups.md#1-broadcast) - In *Broadcast* mode, data is sent redundantly over all the member links in a group. If one of the links fails or experiences network jitter and/or packet loss, the missing data will be received over another link in the group. Redundant packets are simply discarded at the receiver side.

- [Main/Backup](./bonding-main-backup.md) - In *Main/Backup* mode, only one (main) link at a time is used for data transmission while other (backup) connections are on standby to ensure the transmission will continue if the main link fails. The goal of Main/Backup mode is to identify a potential link break before it happens, thus providing a time window within which to seamlessly switch to one of the backup links.

While this feature remains disabled by default, you can use the [`ENABLE_BONDING=ON`](../build/build-options.md#enable_bonding) build option to activate it.

## 2. Additional Documentation and Code Examples

- [SRT Connection Bonding: Introduction](./bonding-intro.md) - Introduction to Connection Bonding. Description of group (bonded) connections.
- [SRT Connection Bonding: Socket Groups](./socket-groups.md) - Description of socket groups in SRT. Here you will also find information regarding the `srt-test-live` application for testing Connection Bonding.
- [SRT Connection Bonding: Main/Backup](./bonding-main-backup.md) - Main/Backup mode description.
- Refer also to the [SRT API](../API/API.md) and [SRT API Functions](../API/API-functions.md#socket-group-management) documentation for updates related to bonding.
- Code examples: simple [client](../../examples/test-c-client-bonding.c) and [server](../../examples/test-c-server-bonding.c) implementation.

## 3. Quick Start Guidelines

### 3.1. Transition to a Bonded Connection

For the end-user of the SRT library, operations on a group of bonded sockets should be very much like on a single SRT socket. It is important that the application needs to check regularly for broken sockets in a group and to reconnect broken links. Refer to the [SRT API Functions](../API/API-functions.md#socket-group-management) document to learn what functions can be used with a socket group.

**On the listener side (pseudocode):**

* Create a socket: `sock_id = srt_create_socket()`,
* `srt_bind(sock_id, ..)` to an interface (as usual for a listener socket),
* `srt_setsockflag(sock_id, SRTO_GROUPCONNECT, &yes /*int yes = 1*/, sizeof yes)` (by allowing group connections your app confirms being group aware),
* Listen for incoming connections: `srt_listen(sock_id, ...)` (as usual),
* Accept a bonded connection with `accepted_group_id = srt_accept(sock_id, ...)`. This will return the ID of the accepted socket group once the first socket group member establishes a connection.

Note that the connection might be still single, so to make sure this is a group you need to check `id & SRTGROUP_MASK`.

Note also that you get the activation on `srt_accept` (epoll readiness, or unblock and return) when the first connection in the group is established. Then, as long as at least one member connection is still alive, every next connection is handled in the background.

**On the caller side (pseudocode):**

* Create a socket group: `sockgroup_id = srt_create_group(SRT_GTYPE_BROADCAST)`,
* For every single endpoint:
    * Connect to the endpoint with `srt_connect(sockgroup_id, address, ...)`

NOTE: For groups it is recommended to use the multi-connecting function `srt_connect_group`. This also allows you to configure additional data for each group member.

**Sending through a group of SRT sockets:**

* Send data through a group of sockets by calling `srt_sendmsg2(group_id, ...)`.
* Pass the group ID that was created by `srt_create_group()` on the caller side, or obtained through `srt_accept()` on the listener side.

**Receiving from the group of SRT sockets:**

Read data from a group of sockets by calling `srt_recvmsg2(group_id)`, passing the group ID instead of the socket ID.
Here `group_id = accepted_group_id` for the listener, and `group_id = sockgroup_id` for the caller.

**Extra data to be passed for sending or receiving**

The `srt_sendmsg2` and `srt_recvmsg2` functions require an object of type `SRT_MSGCTRL` to provide and retrieve extra data for the operation. In this case it should also provide an array of `SRT_SOCKGROUPDATA` type and its size in the `grpdata` and `grpdata_size` fields respectively. On the output `SRT_SOCKGROUPDATA` will be filled with the current state of every member of the group.

### 3.2. Adding Multiple Listeners to a Bonded Group

A bonded connection is only useful when each individual connection is made over a different network interface (any common route/path between the interfaces nullifies the advantages of bonding). Therefore, a listener must be available for connection over the right interface. There are two ways to achieve this:

* Make the listener listen on every network interface (use the `INADDR_ANY` wildcard).
* Make multiple listeners, each one bound to a different network interface.

In the second case, all you have to do is to configure multiple listener sockets. The one that gets the first member connection will report the group ID from the `srt_accept` call. All others will be handled in the background, regardless of which listener socket they were reported from. For a blocking mode in addition to `srt_accept`, which cannot handle multiple listeners, there's a convenience function available — `srt_accept_bond` can be provided with multiple sockets and returns the first accepted socket that reports on any of the given listener sockets. In non-blocking mode you can still use `srt_accept_bond` to identify which socket is ready.

**Use Case.** Create multiple listeners bound to their own IP and port (NIC), but capable of accepting connections within the same bonding group.

Usage example:

```c++
vector<SRTSOCKET> listeners;

for (int i = 0; i < 2; ++i)
{
    SRTSOCKET s = srt_create_socket();
    const int gcon = 1;
    srt_setsockflag(s, SRTO_GROUPCONNECT, &gcon, sizeof gcon);
    srt_bind(s, (sockaddr*)&(sa[i]), sizeof (sa[i]));
    srt_listen(s, 5);	listeners.push_back(s);
}
SRTSOCKET conngrp = srt_accept_bond(listeners.data(), listeners.size(), -1 /* msTimeOut */);
```

### 3.3. Getting Group Statistics and Status

The `srt_bistats(SRTSOCKET u, ...)` function can be used with a socket group ID as the first argument to get statistics for a group. `SRT_TRACEBSTATS` values will mostly be zeros, except for the fields listed in the [SRT Group Statistics Summary Table](../API/statistics.md#srt-group-statistics). Refer to the [SRT API Functions](../API/API-functions.md#socket-group-management) documentation for usage instructions.

The number of socket members in a group can be determined by calling `srt_group_data()`. The code below retrieves the number of sockets in a group.

```c++
SRTSOCKET groupid = srt_create_group(SRT_GTYPE_BROADCAST);
int group_size = 0;
srt_group_data(groupid, NULL, &group_size);
```

To get information (status) on every member of the group at any time, you can use this function:

```c++
SRT_SOCKGROUPDATA output[group_size];
srt_group_data(groupid, output, &group_size);
```

Now `output[i]` contains information on the `i`-th socket. For details, see the [SRT_GROUP_DATA](../API/API-functions.md#srt_sockgroupdata) structure with [`SRT_MEMBER_STATUS`](../API/API-functions.md#srt_memberstatus) per socket.

Note that broken member connections are automatically removed from the group and are no longer members. If you have your own array of endpoint definitions, you should keep it in your application and update it as necessary with the results reported by this function.

## 4. FAQ

### 4.1. _Does rendezvous mode support network bonding?_

No.

### 4.2. _What kinds of statistics for a bonded connection are available?_

Every individual connection is a group of sockets that can be enumerated with `srt_group_data(SRTSOCKET groupid, SRT_SOCKGROUPDATA*, size_t* len)`. Statistics can then be retrieved for every socket in the group. See [Section 3.3. Getting Group Statistics and Status](#33-getting-group-statistics-and-status).

### 4.3. _Is it possible to receive several streams on one listener in the bonded mode?_

Yes, the same as it is with sockets. The listener socket's role is only to handle the connections, so when it's allowed to handle group connections, it can handle single sockets and various different groups as well.

### 4.4. _How are link priorities set in backup mode?_

Priorities can be set only by the caller with the `srt_connect_group(...)` function that accepts a list of `SRT_SOCKGROUPDATA` (prepared by the `srt_prepare_endpoint` function). This structure has the `weight` value for each connection. In backup mode a higher weight corresponds to a higher priority.

### 4.5. _Is it possible to bind a specific NIC to the destination IP address for a particular bonded caller?_

Yes. When you use a single connecting call for every member, you can use `srt_connect_bind`, which allows you to specify the binding address for this connection. When you use `srt_connect_group`, you should specify this source address as a parameter of the `srt_prepare_endpoint` call.

### 4.6. _Is it possible to specify a socket option for a particular bonded caller?_

Yes – if you use the `srt_connect_group` for connecting, you can mount an option container object created by `srt_create_config()` into the `config` field of `SRT_SOCKGROUPCONFIG` structure, after being prepared by `srt_prepare_endpoint`. Options can be added by `srt_config_add()`. When you no longer need the connection table (having used it for `srt_connect_group`) it must be deleted using `srt_delete_config()`. Note that not every option is allowed to be set this way, so you should still check the status of `srt_config_add()`.
