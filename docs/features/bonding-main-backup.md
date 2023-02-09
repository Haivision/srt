# SRT Connection Bonding: Main/Backup

I. [Introduction](#i-introduction)  
II. [Mode Overview](#ii-mode-overview)  
III. [Sender Logic](#iii-sender-logic)  
IV. [Sending Algorithm](#iV-sending-algorithm)  

## I. Introduction

*SRT Main/Backup Switching* mode saves contribution bandwidth by using only one (main) link at a time, while keeping a stream alive if the main link gets broken.

This feature is useful for broadcasting where the traffic costs for a main link are relatively low, and the main link is reasonably reliable. To make sure a transmission doesn’t fail, one or several backup connections are on stand-by. These backup connections ensure reliability, but the traffic costs may be high. To save costs, the backup links are only activated when the main link fails or doesn’t achieve the required throughput.

The goal of SRT's main/backup bonding mode is to identify a potential link break before it happens, thus providing a time window within which to seamlessly switch to one of the backup links.

## II. Mode Overview

### Mode Constraints

The **main constraints** of the main/backup switching mode are:

- **only one active link at a time**, except when switching to one of the backup links or to a link with a higher weight;
- **seamless switching** to one of the backup links ideally happens without packet drops, but dropping packets for a certain period of time is acceptable during the switch under certain circumstances (e.g. severe conditions).

Transmission happens over the main link until it is considered broken or is presumably about to become broken.

### Sensitivity Levels

The only sensitivity level implemented at the moment is a **pre-emptive switch** to a backup path from an unstable main path before it breaks. The goal is to predict an upcoming link breakage before it happens, and to be ready to switch to an activated backup link while losing as few packets as possible.

An additional sensitivity level (**handover switch**) may be added in the future for cases where low latency and packet loss is not critical. The switch would take place once the link is actually broken, without trying to predict it thereby reducing processing overhead. Since the main path in this case is already broken, there would be a delay associated with activating the backup path resulting in a discontinuity in streaming (data loss during the switch).

### Mode Limitations

Detecting a potential link break and switching to a backup link requires time. If the activation of the backup link happens within the `SRTO_PEERLATENCY` interval, there is a good chance that not a single packet will be lost. Therefore, one of the limitations (or usage guidelines) is to set `SRTO_PEERLATENCY ≥ 3×RTT`.

The logic of the main/backup sending algorithm is triggered each time an application submits new payload (calls `srt_sendmsg2(..)`). This imposes two limitations:

- it is better to submit new packets no later than every 50 or 60 ms to trigger the logic with enough frequency (the bitrate should be > 180 kbps);
- file transmission logic does not fit well with this algorithm, and is not supported.

## III. Sender Logic

###  Member Link State

In addition to an individual SRT socket state (e.g. `SRTS_CONNECTED`, `SRTS_BROKEN`, etc.; see [srt_getsockstate(..)](./../API/API-functions.md#srt_getsockstate)), a socket member of a group has a member status (see [SRT_MEMBERSTATUS](./../API/API-functions.md#SRT_MEMBERSTATUS)). The top level member status [SRT_MEMBERSTATUS](./../API/API-functions.md#SRT_MEMBERSTATUS) is visible from the SRT API. However, `SRT_GST_RUNNING` member status can have sub-statuses used by the group sending algorithm.

Member link status can be:

1. **SRT_GST_PENDING**. The link is visible, but the connection hasn't been established yet. The link cannot be used for data transmission yet.
2. **SRT_GST_IDLE** (**stand by**). The link is connected and ready for data transmission, but it is not activated for actual payload transmission. KEEPALIVE control packets are being exchanged once per second.
3. **SRT_GST_RUNNING** (**active**). The link is being used for payload transmission.
   1. **Fresh-Activated**. A link was freshly (newly) activated, and it is too early to qualify it as either stable or unstable.
   2. **Stable**. Active link is considered stable.
   3. **Unstable**. Active link is considered unstable, e.g. response time has exceeded some threshold.
   4. **Unstable-Wary**. A link was identified as unstable (e.g. no response longer than some threshold) until a new response makes it potentially stable again.
4. **SRT_GST_BROKEN**. The link has just been detected to be broken. It is about to be closed and removed from the group.

### Member State Transition

The initial state for group sender logic to operate on a member is once the member socket is connected and transitions to **SRT_GST_IDLE**.

| Event \ State                                                    | => q0 (GST_IDLE)                   | q1 (Fresh-Activated)             | q2 (Stable)                      | q3 (Unstable)                  | q4 (Wary)  | q5 =>(Broken) |
| ---------------------------------------------------------------- | ---------------------------------- | -------------------------------- | -------------------------------- | ------------------------------ | ---------- | ------------- |
| Last response < LST*                                             | x                                  | q1                               | q2                               | **q4** (`m_tsWarySince` = now) | x          | x             |
| Last response >= LST*                                            | x                                  | **q3** (`tsUnstableSince` = now) | **q3** (`tsUnstableSince` = now) | x                              | **q3**     | x             |
| `tsUnstableSince`!= 0 and (now - `tsUnstableSince`> PEERIDLETMO) | x                                  | x                                | x                                | **q5**                         | **q5**     | x             |
| Probing period is over                                           | x                                  | q2 / `tsFreshActivation` = 0     | x                                | x                              | **q2**     | x             |
| Activate (group decision)                                        | **q1** (`tsFreshActivation` = now) | x                                | x                                | x                              | x          | x             |
| Silence (group decision)                                         | x                                  | :question:                       | **q0**                           | x                              | :question: | x             |
| Close/break (external event)                                     | **q5**                             | **q5**                           | **q5**                           | **q5**                         | **q5**     | x             |

Member activation happens according to conditions described in the [Member Activation](#member-activation) section. Upon activation a member transitions to the **q1** (**SRT_GST_RUNNING**: **Fresh-Activated**) state.

A member in the **q1** (**SRT_GST_RUNNING**: **Fresh-Activated**) state can either become  **q2** (**SRT_GST_RUNNING**: **Stable**) or **q3** (**SRT_GST_RUNNING**: **Unstable**).

Conditions to qualify a member as unstable are described in the [Qualifying Member Unstable](#qualifying-member-unstable) section. If the conditions are met, the member is transitioned into the **q3** (**SRT_GST_RUNNING**: **Unstable**) state.

An **Unstable** member (**q3** (**SRT_GST_RUNNING**: **Unstable**) or **q4** (**SRT_GST_RUNNING**: **Unstable-Wary**)) **either**:

- transition back to the **q2** (**SRT_GST_RUNNING**: **Stable**) state through the **q4** (**SRT_GST_RUNNING**: **Unstable-Wary**) state;
- transition to the **q0** (**SRT_GST_IDLE**) state (be silenced); or
- transition to the **q5** (**SRT_GST_BROKEN**) state (be closed and removed from the group).

A member in the **q5** (**SRT_GST_BROKEN**) state will eventually be closed and removed from the group.

### Member Ordering by Priority <a href="#send-member-ordering"></a>

When comparing two members, one is ordered before the other depending on which of the following ordering conditions applies (in the order of priority).

1. Higher weight (highest priority)
2. By backup state (if equal weight):
   1. **SRT_GST_RUNNING**: **Stable**
   2. **SRT_GST_RUNNING**: **Fresh-Activated**
   3. **SRT_GST_RUNNING**: **Unstable-Wary**
   4. **SRT_GST_RUNNING**: **Unstable**
   5. **SRT_GST_BROKEN**
   6. **SRT_GST_IDLE** (**stand by**)
   7. **SRT_GST_PENDING**
3. By the Socket ID (lower value first).

For example, an unstable member with a higher weight is ordered before a stable member with lower weight.

**Potential Improvement**: Order by connection start time (older connection comes first) before comparing socket IDs.

### Member Activation

*Member activation* means transitioning an idle (stand by) member with status **SRT_GST_IDLE** to a **SRT_GST_RUNNING**: **Fresh-Activated** state. The time it takes to activate a member is saved as `tsFreshActivation = CurrentTime`.

Activation is needed if one of the following is true:

1. There are no **SRT_GST_RUNNING**: **Stable** or **SRT_GST_RUNNING**: **Fresh-Activated** members.
2. The weight of one of the idle members is higher than the maximum weight of **SRT_GST_RUNNING** links.

An idle link to be activated is taken from the top of the list of idle links, sorted according to [member ordering priority](#send-member-ordering).

A member remains in the  **SRT_GST_RUNNING**: **Fresh-Activated** state while `CurrentTime - tsFreshActivation > ProbingPeriod`, i.e. for the whole probing period:

 `ProbingPeriod = ILST + 50ms` 

Here **Initial Link Stability Timeout** `ILST = max(LSTmin; SRTO_PEERLATENCY)`, 

- `LSTmin = 60ms` ;
- `SRTO_PEERLATENCY` is the corresponding socket option value on a connected socket.

### Qualifying a Member as Unstable

A member in the active (**SRT_GST_RUNNING**) state can be transitioned to the **q3** (**SRT_GST_RUNNING**: **Unstable**) state (become unstable) for any of the reasons described below.

#### Unstable due to response timeout

A member link is considered unstable if the time elapsed since the last response from a peer (`LastRspTime`) exceeds the link stability timeout:

`CurrentTime - LastRspTime > LST`

where

- `CurrentTime` is the time when the next data packet **is submitted to a group** for sending;
- `LastRspTime` is the time when the latest response (*ACK, loss report (NAK), periodic NAK report, KEEP_ALIVE message, or DATA packet in case of bidirectional transmission*) was received from the SRT receiver by the SRT sender for a member in the **SRT_GST_RUNNING**: **Fresh-Activated** state  `LastRspTime ≥ tsFreshActivation`;
- `LST`  (Link Stability Timeout) is a dynamic value for stability timeout calculated based on the group `SRT Latency` and RTT estimate on a link. This value is calculated individually for each active (**SRT_GST_RUNNING**) link.

The link stability timeout for an active (**SRT_GST_RUNNING**)  member (**except** for **SRT_GST_RUNNING**: **Fresh-Activated**) is calculated with each data packet submission (on `srt_sendmsg2(..)`).

For a member in  **SRT_GST_RUNNING**: **Fresh-Activated** state `LST = ILST` (see [Member Activation](#member-activation)).

For active  (**SRT_GST_RUNNING**) members in states **different** from **SRT_GST_RUNNING**: **Fresh-Activated** `LST` is calculated as follows:

`LST = 2 * SRTT + 4 * RTTVar,` and `LSTmin ≤ LST ≤ SRTO_PEERLATENCY`,

where

- `LSTmin = 60ms`;
- `SRTO_PEERLATENCY` is the corresponding socket option value on a connected socket;
- `SRTT` and `RTTVar` are smoothed RTT and RTT variances calculated on an individual socket member in runtime as described in the SRT RFT [Round Trip Time Estimation](https://tools.ietf.org/html/draft-sharabayko-srt-00#section-4.10) section.

#### Unstable due to sending failure

If sending a packet (`srt_sendmsg2(..)`) over a member SRT socket has failed with error `SRT_EASYNCSND`, the member is qualified as **q3** (**SRT_GST_RUNNING**: **Unstable**).

This error indicates that there was not enough free space in the sender's buffer to accept this data packet for sending. It should not happen under normal conditions and if buffers are configured correctly this kind of error indicates some possible congestion on a path.

#### Unstable due to sender-side packet drops

If a member has dropped a packet (see [Too-Late Packet Drop](https://tools.ietf.org/html/draft-sharabayko-srt-01#section-4.6) section of the Internet Draft) **since the previous submission of a data packet** for sending (previous call to `srt_sendmsg2(..)`), it transitions to the **q3** (**SRT_GST_RUNNING**: **Unstable**) state.

#### Unstable due to receiver-side packet drops (TBD)

**IMPORTANT: For the time being, the main backup algorithm does not react to lost packets or packets dropped by the receiver.** Note that an SRT sender does not know the drop rate on the receiver's side. A receiver acknowledges packets it drops. PR [#1889](https://github.com/Haivision/srt/pull/1889) extends ACK packets to include the total number of packets dropped by the receiver.

### Qualifying a Member as Broken

#### Broken due to peer idle timeout

Similar to a single SRT connection, a member SRT socket is considered broken if there has been no response from a peer for a certain time, defined by the [SRTO_PEERIDLETIMEO](./../APISocketOptions.md#SRTO_PEERIDLETIMEO) socket option (5 seconds by default). A broken socket will be closed by the SRT library. It is also removed from a group.

#### Broken due to remaining unstable for too long

The only additional condition to break an SRT connection from a group is if a member remains **unstable** for too long. A group can request a socket member to break its connection if the time elapsed since the socket has become unstable (`tsUnstableSince`) exceeds the timeout defined by the [SRTO_PEERIDLETIMEO](./../APISocketOptions.md#SRTO_PEERIDLETIMEO) socket option:

`CurrentTime - tsUnstableSince > SRTO_PEERIDLETIMEO`.

### Qualifying a Member as Stable

Only a member SRT socket in the active (**SRT_GST_RUNNING**) state can be qualified as stable.

#### Freshly-activated becomes stable

A freshly activated member SRT socket (**SRT_GST_RUNNING**: **Fresh-Activated**) is qualified as stable (**SRT_GST_RUNNING**: **Stable**) once the probing period `ProbingPeriod` is over (unless it has already been qualified as unstable). The probing period is defined in the [Member Activation](#member-activation) section.

#### Unstable becomes stable

If there is no longer a reason to qualify a member as unstable (see [Qualifying a Member as Unstable](#qualifying-a-member-as-unstable)) it can transition to the stable state. A member in the **q3** (**SRT_GST_RUNNING**: **Unstable**) state transitions immediately to the **q4** (**SRT_GST_RUNNING**: **Unstable-Wary**) state. The time of the transition event is saved as `tsWarySince = CurrentTime`.

If a member remains in the **q4** (**SRT_GST_RUNNING**: **Unstable-Wary**) state for `4 × SRTO_PEERLATENCY`, it can transition to the **SRT_GST_RUNNING**: **Stable** state.

*Note that if a member becomes unstable **q3** (**SRT_GST_RUNNING**: **Unstable**) again, the `tsWarySince` time will be reset on the next transition to the **Unstable-Wary** state.*

### Silencing an Active Member

There must be only one stable (**SRT_GST_RUNNING**: **Stable**) member SRT socket active in a group. There may be several active unstable or fresh activated sockets in a group. However, if more than one member is qualified as stable, only one must remain active.

In order to select a stable member to remain active the [Member Ordering by Priority](#send-member-ordering) is applied. All active members ordered after the first stable member are silenced. All active members ordered before the first stable member in the list, including the stable member, remain active.

## IV. Sending Algorithm

The group sending workflow is triggered by a submission of the following data packet via the `srt_sendmsg(..)` SRT API function. The following steps apply in order.

### 1. Qualify Member States

Before sending a packet, all member links are qualified according to the states described above.

### 2. Sending the Same Packet over Active Links

The same data packet is sent (duplicated) over all members qualified as active (**SRT_GST_RUNNING**).

**If sending fails, the link is re-qualified as unstable** as described in the [Unstable by Sending Failure](#unstable-by-sending-failure) section.

### 3. Save the Packet Being Sent to the Group SND Buffer

The group has a separate buffer for packets being sent and to be acknowledged. If a member socket gets broken, those packets can be resent over a freshly-activated backup member.

### 4. Check if Backup Link Activation is Needed

See the [Member Activation](#member-activation) section.

### 5. [IF] Activate Idle Link

If a member is activated, all buffered packets (see step 3) are submitted to this SRT member socket. If sending fails (see the [Unstable by Sending Failure](#unstable-by-sending-failure) section), another member is activated by following the logic described in the [Member Activation](#member-activation) section.

### 6. Check Pending and Broken Sockets

Check if there are pending sockets that failed to connect, and should be removed from the group.

Check if there are broken sockets to be removed from the group.

Check if there are member socket unstable for too long that should be requested to transition to the `broken` state.

### 7. Wait for Sending Success

There may be a situation where no sending has succeeded, but there are active members.

If the group is in non-blocking operation mode, the group returns `SRT_EASYNCSND` error.

If the group is in blocking operation mode, sending over active members is retried until the sending timeout [`SRTO_SNDTIMEO`](./../API/API-socket-options.md#SRTO_SNDTIMEO) is reached (`SRT_EASYNCSND` error is returned), or at least one member successfully accepts a data packet for sending.

### 8. Silence Redundant Members

The rules described in the [Silencing an Active Member](#silencing-an-active-member) section are applied in this step.
