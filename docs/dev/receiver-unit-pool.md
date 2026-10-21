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

## Pool cache layers

The units are cached in the following 3 containers:

1. Hand (direct)
2. Solid (upper)
3. Condenser (lower)

Each of them uses the "series" size, which is `SRT_RCV_BUFFER_POOL_MAX_SERIES`
(at this moment, 128). Hand and Condenser are simply 1-level containers of
units, while Solid is a container of containers of units.

Hand is the container that is for the private use of the object and its worker
thread that contains it; as the only one it's not mutex-protected - the reason
is that the intention is to be affined to exclusively one thread - the one
that will be picking up units from it. If the Hand container is empty, the
refilling procedure is used, which takes one series from the Solid container,
and if this is also empty, simply allocates the memory from the system; even
if this should happen, it always allocates one series of units.

Solid is a container of containers; each member container contains one series
of units. From this container the whole single container is extracted and
moved (swapped) with the Hand container. Solid is mutex-protected and only
its own mutex is locked for this operation.

Condenser is a unit container, which should collect units that are returned
to the pool after being decommissioned. Until the whole series of units is
collected, nothing else happens. Otherwise the full-series condenser is
moved (swapped) into the Solid container ("uplifting"). This way the whole
series of units will be available when the Hand container needs it. This
uplifting operation requires locking both lower and upper mutexes.

It is believed that this layout allows for separation for the unit pickup
for the multiplexer most of the time (it just uses Hand as its private
container without locking anything), while once per 128 pickups it will
have to do reallocation, or locking the upper (Solid) container only.
Locking the upper container will still rarely collide with returning the
units through the condenser, as the upper container will be only locked
when uplifting should happen.

## Reading and dispatching packets

Dispatching of the incoming packets happens in the multiplexer's receiver
thread, hence all control information will be executed immediately. So the
packet unit is taken by the multiplexer from the Hand container, filled
by the `recvmsg` call, then dispatched, but without removal yet - this is
not necessary if the packet turns out to be a control packet. In this case
the dispatching procedure will do its job and then this same unit can be
reused for the next reading.

If the packet turns out to be a data packet however, it is removed from the
container immediately and kept temporarily in a local variable for the time
calling the dispatching function, which **may** take ownership od that unit.
If it didn't, the unit is then returned to the Hand container. This step is
unfortunately necessary by one reason: during the dispatching there's
potentially a packet filter to be executed. One of possible things it may do in
this case is to pick up additional units from the Hand container, therefore
the currently processed data packet must be first removed. The provision to
the filter may ship various combinations of results:

1. If the incoming packet was a packet-filter control packet, this packet
needs to be interpreted, but not stored any further and should be able to
be immediately reused (returned directly to the Hand container).

2. If this packet was a valid data packet, the dispatched will attempt to
store it into the receiver buffer.

3. Regardless of these above, the packet filter, in response to provision
of this packet, may produce additional packets, which should be also
attempted to be inserted into the receiver buffer. The filter will need
to pick up units from the Hand container to store them.

The result can be zero or more packets to be potentially inserted into
the buffer, of which some may be rejected. Packet units stored in the receiver
buffer get ownership transferred to the receiver buffer, others will be
returned to the Hand container.

Packets stored in the receiver buffer are since this moment owned by the
buffer and they can be at best only passed ownership back to the multiplexer.

Units then remain in the receiver buffer until the application's call results
in delivering this data; after that the unit is decommissioned and can be
returned. This process differs between a single socket and a group.

## Single socket approach

The version for a single socket, that is, where a receiver buffer may only
contain units provided by exactly one multiplexer, is simpler: the receiver
buffer keeps the pointer to the unit pool object. This pointer is valid as
long as the socket is bound (unbinding is not implemented at this moment,
but it is possible). After reading a packet by the application, the unit
is decommissioned, so the unit's ownership is passed to the multiplexer's
Condenser container. Then the unit may be potentially uplifted to the upper
container, and then picked up to the Hand container.

## Group approach

Once we have the single buffer for the group - that is, member sockets do not
have their receiver buffers, and they only operate with the link and the
multiplexer, while data packets are stored in the group's buffer and they are
only extracted by the application from there - there can be units potentially
provided by multiple different multiplexers.

In general this isn't a problem because the receiver buffer owns the packets
anyway, and it can delete them directly if need be. However we still want to
have the memory allocation process optimized, so it is desired that the buffer
return these units to the very multiplexer, from which they have been
extracted, at least if this is possible.

However returning them directly could be challenging for the object
persistence rules - regarding the fact that the group must be prepared to
outlive the multiplexer, while keeping the units received from it. Therefore
the very first rule is that every unit stored in the buffer has also an
information about the multiplexer ID from which it has come. Keeping the
pointer to the multiplexer (or be it even the pool) is not possible; this
would require persistence synchronization (such as clearing the pointer in
case when the multiplexer is deleted). So dispatching by multiplexer ID is
inevitable.

This however is another challenge: doing global mutex locking and dispatching
a multiplexer for every packet to be "condensed" would put too much of a
burden. Therefore the groups are using the so-called "water container" - the
group's private condenser.

As packets might come from different multiplexers, the water container is
a map with keys being the multiplexer ID and the value being unit containers.
Once a single number of series has been collected (regardless how many
units are stored at particular key), a series condensation is being done,
that is, under a single global lock every multiplexer is dispatched and
if this is succeeded, all units from the container assigned to its ID are
condensed. If the multiplexer is not found by ID, units simply remain
in the water container. Then, after all keys are looped over, the water
container is completely purged (units that were not returned to any
multiplexer simply get deleted here).

Of course, this means that at once sometimes there's about "half of the
series" returned to the lower container, so only at any next time will
the single series be collected back and uplifted to the upper container.
Still, one of the containers will eventually uplift enough packets so
that the whole series can be reused. Keeping unused units in the pool's
condenser and the group's condenser is the matter of balance between
the usage of memory and CPU; possibly further optimizations for that
process may be needed.

