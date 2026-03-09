# Unit pool

This is the facility for managing memory for incoming packets and receiver
buffer. This solution is using passing ownership, but done a bit shadowy
way due to required compatibility with C++03 and inaccessibility of move
semantics.

## History

The original facility for memory management for incoming packets and
receiver buffer, as provided for the UDT library was using the
`CUnitQueue` class that was working through leasing the memory blocks
to the receiver buffer. That was working on the following premises:

* The multiplexer is the owner of the queue and all blocks allocated
by it.

* The receiver buffer is propertied to the socket, while one multiplexer
can be used by multiple sockets. This means that once the packet buffer
is placed into the socket's receiver buffer, it's leased to this buffer;
once the unit is decommissioned (after the contents have been delivered
to the application) this unit was changing the status to free and could
be reused.

* The memory was allocated through big single blocks that were split
into single-packet buffer units. That was possible because the multiplexer
was the owner of this memory all the time and the current use status
was changed through leasing.

This solution had however limitations:

1. Once the socket is bound (that is, assigned to a multiplexer), it could
since that moment potentially contain packets in its receiver buffer. This
means that the multiplexer is the higher object in the hierarchy and the socket
is bound to it forever. At the same time, the last socket bound to a
multiplexer must drag the multiplexer with itself when deleted, however you
can't "unbind" the socket because the receiver buffer must be deleted
completely (and this way return the leased units) before you delete the
contents of the multiplexer's unit queue. This puts additional limitations on
the socket closing procedure and caused extra troubles with the fix to make
the UDP socket closed as fast as possible.

2. This solution is not possible to be used together with the a group that
would have its own receiver buffer (earlier versions of SRT were simply reusing
the sockets' buffers and deliver or discard packets directly from the sockets'
buffers). This introduces a nonexistent earlier dependence between the group
and the multiplexer, while a single connection could be closed while the group
still exists and is still capable of delivering packets to the application. 
If there is a unit leased to this buffer by the multiplexer being closed at
the moment, then such a multiplexer would have to have its lifetime extended
up to the time when the receiver buffer entry keeping it will decommission it,
otherwise the receiver buffer would contain a dangling pointer to the unit.

Because of this there was a need for a new receiver buffer memory management
facility.

## The packet unit pool concept

The new packet unit pool is based on the following premises:

* Units are no longer leased, but the ownership can be passed between objects
* Reuse of the decommissioned units is treated as optimization, not limitation

The following features were then not implemented:

1. No more leasing: the unit is owned by the container where it is currently
stored. You delete the container - you delete the unit. If the unit gets lost
anywhere in the processing and no one bothered with returning it - the memory
will be simply reclaimed by the system, as usual.

2. No large block allocation. That was possible with unchanged ownership, but
ownership passing rule requires that every unit is a single object on its own.
The impact it makes on often and repeating unit requests are smoothened by the
use of cache containers.

The advantage is, however, that the multiplexer and the receiver buffer can be
now decoupled from one another. And the group receiver buffer could be finally
implemented properly.

Note that theoretically you may think that the old solution still works for the
single sockets and problems are only with the group. That's not true simply
because as a multiplexer can be shared between sockets, it can be just as well
shared between member sockets, or even shared equally between single sockets
and group-member sockets - and this cannot be anyhow limited simply because
this happens on the listener sockets that can accept single connections as well
as group connections, and this socket's multiplexer will be then shared the
described way. In short: the multiplexer reads the packet and doesn't know
where it would have to be passed - this will be known after reading the header
information (after the whole packet, together with the payload, has been read)
and dispatching it to the right socket, begin a member socket or not.

## Reading and dispatching packets

Dispatching of the incoming packets happens in the multiplexer's receiver
thread, hence all control information will be executed immediately. So the
packet unit is taken by the multiplexer from its private container, filled
by the `recvmsg` call, then dispatched, but without removal yet - this is
not necessary if the packet turns out to be a control packet. In this case
the dispatching procedure will do its job and then this same unit can be
reused for the next reading.

If the packet turns out to be a data packet however, it is removed from the
container immediately and kept temporarily in a local variable for the time
calling the dispatching function, which **may** take ownership od that unit.
If it didn't, the unit is then returned to the container. This step is
unfortunately necessary by one reason: during the dispatching there's
potentially a packet filter to be executed, which may result in various
combinations of results: may take over the packet, may not need the packet
or it may also return it directly to the receiver queue's private container.
What the packet filter requires, however, is the source of fresh units that
it may need to use to store filter-provided packets, so at this moment this
private multiplexer's container must not contain temporarily used packets.

If the packet filter is used, the following things may happen:

1. The incoming packet was a packet-filter control packet, so this packet
needs to be interpreted, but not stored any further and should be able to
be immediately reused.

2. The incoming packet was a valid data packet and should be then potentially
stored in the receiver buffer.

3. Regardless of these above, a packet fed into the packet filter may result
in producing extra packets by this packet filter. These packets will have
to be stored in the newly requested packet units.

And regardless of these above, some packets may be stored in the receiver
buffer, others can be rejected. Packet units stored in the receiver buffer
get ownership transferred to the receiver buffer, others will be returned
to the multiplexer's private container. That includes a possibility that
multiple packets produced by the packet filter will be returned.

Packets stored in the receiver buffer are since this moment owned by the
buffer and they can be at best only passed ownership back to the multiplexer.

## Series separation

The decommissioned unit is not stored directly in the multiplexer's container.
The reason is that there would be two threads fighting for access to it, so
locking common mutexes for that purpose should be minimized.

Because of that there's a mechanism known as "condensation". There are two
container layers in the pool (lower and upper) and a separate container is kept
by the multiplexer. A container keeping directly the units is called "series".

The packet unit pool's upper container is a "container of series" and it
keeps series ready for pickup for the multiplexer. If the multiplexer requires
a unit, it checks first in its own container. Only if this container is empty
will it proceed to "refill" it. This is simply a procedure done once per a
time (when the local storage is depleted) and this means that one series is
picked up from the upper container and moved to the multiplexer's storage.
If, however, the upper container is also empty, then the units are allocated
anew from the system. The number of units in the single series is defined by
the `SRT_RCV_BUFFER_POOL_SERIES_SIZE` macro in `queue.h`. The maximum number
of series stored in the upper container is `SRT_RCV_BUFFER_POOL_MAX_SERIES`.

The packet unit pool's lower container is a container of units ("condenser").
Decommissioned units are being stored there until the size of a single series
is collected; if that happens, the whole series is "uplifted" to the upper
container.

Both upper and lower containers use separate mutexes. It is possible to lock
them together and the order is: lower, upper. Locking both happens only in case
when the lower container gets "full" (the series number of units is collected)
so the series is moved to the upper container so that it can be picked up by
the multiplexer.


## Single socket approach

The version for a single socket, that is, where a receiver buffer may only
contain units provided by exactly one multiplexer, is simpler: the receiver
buffer keeps the pointer to the unit pool object. This pointer is valid as
long as the socket is bound (unbinding is not implemented at this moment,
but it is possible). After reading a packet by the application, the unit
is decommissioned, so the unit's ownership is passed to the multiplexer's
condenser. Then the unit may be potentially uplifted to the upper container,
then refilled from it by the multiplexer.


## Group approach

Once we have the single buffer for the group - that is, member sockets do not
have their receiver buffers, and they only operate with the link and the
multiplexer, while data packets are stored in the group's buffer and they are
only extracted by the application from there - there can be units potentially
provided by multiple different multiplexers.

In general this isn't a problem because the receiver buffer owns the packets
anyway, and it can delete them directly if need be. However we still want to
have the memory allocation optimized, so it is desired that the buffer return
these units to the very multiplexer, from which they have been extracted, at
least if this is possible.

However returning them directly could be challenging for the object
persistence rules - regarding the fact that the group must be prepared to
outlive the multiplexer, while keeping the units received from it. Therefore
the very first rule is that every unit stored in the buffer has also an
information about the multiplexer ID from which it has come. Keeping the
pointer to the multiplexer is not possible; at least not for single units,
and even keeping a private dispatching map leading to pointers would be
another point to update when deleting a multiplexer. So dispatching by
multiplexer ID is inevitable.

This however is another challenge: doing global mutex locking and dispatching
a multiplexer for every packet to be "condensed" would put too much of a
burden. Therefore the groups are using the so-called "water container".

That is, the group doesn't really "condense" decommissioned packets, instead
it condenses them locally and the number of condensed units is kept
independently, while the local condenser keeps a map so that every multiplexer
has its own condensation pool. Once the number of condensed units exceed the
number of series (regardless of the fact that this number doesn't mean in
this case anything else than to simply group the flushing events in bigger
packs), the "flushing" action is undertaken: the global lock is applied
and for every multiplexer ID collected in the "water" map the multiplexer
is found and the whole container of units under this key in the map is
moved to the lower container of that multiplexer. Of course, there is no
guarantee that a multiplexer can be found by ID, but if this happens,
units are simply not returned. At the end of the flushing action the map
is cleared - whatever units haven't been returned to their assigned
multiplexer, get simply deleted.

Of course, this means that at once sometimes there's about "half of the
series" returned to the lower container, so only at any next time will
the single series be collected back and uplifted to the upper container.
Still, one of the containers will eventually uplift enough packets so
that the whole series can be reused. Keeping unused units in the pool's
condenser and the group's condenser is the matter of balance between
the usage of memory and CPU; possibly further optimizations for that
process may be needed.

