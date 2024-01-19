# Low Level Information for the SRT project

## Introduction

This document contains information on various topics related to
the SRT source code, including descriptions of some cross-source analysis that would
not be obvious for a source code reviewer. It's not a complete documentation of
anything, rather a collection of various kind of information retrieved during
development and even reverse engineering.

## Mutex locking

This analysis is a result of detected lots of cascade mutex locking in the
SRT code. A more detailed analysis would be required as to which mutex is
going to protect what kind of data later.

Here is the info collected so far:

### Data structures

The overall structure of the object database, involving sockets and groups
is as follows (pseudo-language):

```
CUDTUnited (singleton) {
     CONTAINER<CUDTSocket> m_Sockets;
     CONTAINER<CUDTSocket> m_ClosedSockets;
     CONTAINER<CUDTGroup> m_Groups;
     CONTAINER<CUDTGroup> m_ClosedGroups;
}

CUDTGroup {
     type SocketData { CUDTSocket* ps; SRTSOCKET id; int state; ... }
     CONTAINER<SocketData> m_Group;
}
```

Dead sockets (either closed manually or broken after connection) are
moved first from `m_Sockets` to `m_ClosedSockets`. The GC thread will take
care to delete them physically after making sure all inside facilities
do not contain any remaining data of interest.

Groups may only be manually closed, however a closed group is moved
to `m_ClosedGroups`. The GC thread will take care to delete them, as long
as their usage counter is 0. Every call to an API function (as well as
TSBPD thread) increases the usage counter in the beginning and decreases
upon exit. A group may be closed in one thread and still being used in
another. The group will persist up to the time when the current API function
using it exits and decreases the usage counter back to 0.

Containers and contents guarded by mutex:

`CUDTUnited::m_GlobControlLock` - guards all containers in CUDTUnited.

`CUDTSocket::m_ControlLock` - guards internal operation performed on particular
socket, with its existence assumed (this is because a socket will always exist
until it's deleted while being in `m_ClosedSockets`, and when the socket is in
`m_ClosedSockets` it will not be deleted until it's free from any operation,
while the socket is assumed nonexistent for any newly called API function even
if it exists physically, but is moved to `m_ClosedSockets`).

`CUDTGroup::m_GroupLock` - guards the `m_Group` container inside a group that
collects member sockets.

There are unfortunately many situations when multiple locks have to be applied
at a time. This is then the hierarchy of the mutexes that must be preserved
everywhere in the code.

As mutexes cannot be really ordered unanimously, below are two trees, with also
some possible branches inside. The mutex marked with (T) is terminal, that is,
no other locks shall be allowed in the section where this mutex is locked.

### Mutex ordering information

Note that the list isn't exactly complete, but it should contain all
mutexes for which the locking order must be preserved.

```

 - CUDTSocket::m_ControlLock

 - CUDT::m_ConnectionLock

        - CRendezvousQueue::m_RIDVectorLock

 - CUDTUnited::m_GlobControlLock

 - CUDTGroup::m_GroupLock

    - CUDT::m_RecvAckLock  || CEPoll::m_EPollLock(T)

----------------
 - CUDTUnited::m_GlobControlLock

        - CUDTGroup::m_GroupLock  || CSndUList::m_ListLock(T)

     - CUDT::m_ConnectionLock

              - CRendezvousQueue::m_RIDVectorLock

 - CUDT::m_SendLock

    - CUDT::m_RecvLock

       - CUDT::m_RecvBufferLock

 - CUDT::m_RecvAckLock || CUDT::m_SendBlockLock
------------------
```

ANALYSIS ON: `m_ConnectionLock`

**NOTE**: There is a potential of reporting a deadlock due to lock
inversion between `CRcvQueue::m_LSLock` and `CUDT::m_ConnectionLock`.
This is false.

According to the below description, you need to know which objects
exactly are taken into account. The CRcvQueue object is an underlying
object of CMultiplexer, which is shared between sockets, and also it
is definitely shared between the listener socket and the accepted
socket spawn out of it. We have then two scenarios here that seem
to be in conflict:

1. Setting a new listener on the multiplexer by calling `srt_listen()`
or removing the listener from the multiplexer by calling `srt_close()`
on the listener socket. This calls `CUDT::setListenState`, which is done under
a lock for `m_ConnectionLock` of the socket that is being set up as listener.
Then inside the CRcvQueue::setListener it locks `m_LSLock`.


2. Processing the new connection from the incoming connection request
processed in `CUDT::processConnectRequest`. It is called under the lock
of `m_LSLock`. Then inside it calls `CUDT::open` and `CUDT::acceptAndRespond`
that lock `m_ConnectionLock`.

However, the methods that potentially lock `m_ConnectionLock` are
called for the sake of the **newly created socket** that is about to
be reported as the accepted one from `srt_accept()`, **not the listener
socket**.

That said, a potential situation when these two things could be in collision, as
theoretically these mutexes are in two different objects and the accepted
socket shares always the listener with the listening socket, then just as well
if `srt_listen()` is called on an accepted socket (which will always fail and
makes no sense, but still it must be somehow recognized and rejected by the
internals), it will go through the sequence of first `m_ConnectionLock` and
then `m_LSLock`, while for the same object the order was that the `m_LSLock`
was locked before its creation in response to the connection handler and
then `m_ConnectionLock` was applied for it.

These two situations, however, cannot occur simultaneously. This is because
those activities that occur during the connection processing that apply
`m_ConnectionLock` under a lock of `m_LSLock` are performed during the time
when the socket is just created and is still under processing for connection.
A potential of calling `srt_listen()`, just as well as `srt_close()` on it,
or even breaking it, will be only possible when this activity is finished
and no more locks are applied on `m_ConnectionLock`.

A theoretical potential exists if you try to call `srt_listen()` on a
potentially accepted socket in the listener callback. The result will
only be such that this function call will hang up due to inability to
lock `m_LSLock`.


```

-- CUDT::startConnect flow

CUDTUnited::connectIn -- > [LOCKED s->m_ControlLock]
    CUDT::open -- > [MAYBE_LOCKED m_ConnectionLock, if bind() not called]
        CUDT::clearData --> [LOCKED m_StatsLock]
    CUDTUnited::updateMux  -- > [LOCKED m_GlobControlLock]
    {
       [SCOPE UNLOCK s->m_ControlLock, if blocking mode]
       CUDT::startConnect  -- > [LOCKED m_ConnectionLock]
           CRcvQueue::registerConnector
                CRendezvousQueue::insert --> [LOCKED CRendezvousQueue::m_RIDVectorLock]
    }
END.

-- CUDT::groupConnect flow

CUDT::groupConnect (no locks)
   CUDT::setOpt [LOCKS m_ConnectionLock, m_SendLock, m_RecvLock]
   { [LOCKS m_GlobControlLock]
        CUDTGroup::add [LOCKS m_GroupLock]
   }
CUDT::connectIn --> continue with startConnect flow

-- CUDTUnited::listen (API function)

CUDTUnited::listen
    CUDTUnited::locateSocket [LOCKS m_GlobControlLock]
    {
        [SCOPE LOCK s->m_ControlLock]
        CUDT::setListenState -- > [LOCKED m_ConnectionLock]
             CRcvQueue::setListener -- > [LOCKED m_LSLock]
    }

-- CUDT::processAsyncConnectRequest

CRcvQueue::worker ->
...
CRcvQueue::worker_TryAsyncRend_OrStore
     CUDT::processAsyncConnectResponse -- > [LOCKED m_ConnectionLock]
         CUDT::processConnectResponse
             CUDT::postConnect
                 CUDT::interpretSrtHandshake ->
                 [IF group extension found]
                 CUDT::interpretGroup
                 {
                     [SCOPE LOCK m_GlobControlLock]
                     [IF Responder]
                     {
                         CUDT::makeMePeerOf
                             [LOCKS m_GroupLock]
                                 CUDTGroup::syncWithSocket
                             CUDTGroup::find --> [LOCKED m_GroupLock]
                     }
                     debugGroup -- > [LOCKED m_GroupLock]
                 }


-- CUDT::acceptAndRespond

CRcvQueue::worker_ProcessConnectionRequest
{
     [SCOPE LOCK m_LSLock]
     CUDT::processConnectRequest
         CUDTUnited::newConnection
             locateSocket -- > [LOCKED m_GlobControlLock]
             locatePeer -- > [LOCKED m_GlobControlLock]
             [IF failure, LOCK m_AcceptLock]
             generateSocketID --> [LOCKED m_IDLock]
             CUDT::open  -- > [LOCKED m_ConnectionLock]
             CUDT::updateListenerMux  -- > [LOCKED m_GlobControlLock]
             CUDT::acceptAndRespond --> [LOCKED m_ConnectionLock]
                 CUDT::interpretSrtHandshake ->
                 [IF group extension found]
                 CUDT::interpretGroup
                 {
                     [SCOPE LOCK m_GlobControlLock]
                     [IF Responder]
                     {
                         CUDT::makeMePeerOf
                             [LOCKS m_GroupLock]
                                 CUDTGroup::syncWithSocket
                             CUDTGroup::find --> [LOCKED m_GroupLock]
                     }
                     debugGroup -- > [LOCKED m_GroupLock]
                 }
                 {
                     [SCOPE LOCK m_GlobControlLock]
                     CUDT::synchronizeWithGroup -- > [LOCKED m_GroupLock]
                 }
                 CRcvQueue::setNewEntry -- > [LOCKED CRcvQueue::m_IDLock]
                 {
                     [SCOPE LOCK m_GlobControlLock]
                     {
                         [SCOPE LOCK m_GroupLock]
                     }
                 }
             {
                 [SCOPE LOCK m_AcceptLock]
                     CEPoll::update_events
             }
             [IF Rollback]
             CUDT::closeInternal
                 [LOCKING m_EPollLock]
                 {
                     [SCOPE LOCK m_ConnectionLock]
                         [SCOPE LOCK m_SendLock]
                             [SCOPE LOCK m_RecvLock]
                                 [LOCKING m_RcvBufferLock]
                 }
             {
                 [SCOPE LOCK m_GlobControlLock]
                 CUDT::removeFromGroup --> [LOCKED m_GroupLock]
             }

         CEPoll::update_events
}

-- CUDT::bstats: TRT-LOCKED m_ConnectionLock

-- CUDT::packData

CSndQueue::worker
 CSndUList::pop -- > [LOCKED m_ListLock]
     CUDT::packData

```
