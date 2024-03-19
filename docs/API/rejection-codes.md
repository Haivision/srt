# SRT Rejection Codes

This document provides an overview of the rejection (error) codes used by and supported within SRT and SRT-based applications. 

[:arrow_down: &nbsp; Jump to list of rejection codes](#api-function-rejection-codes)


## Summary of Rejection Codes

Rejection codes are used in the SRT API, and are transferred on the wire as a part of a Handshake packet (refer to the `Handshake Type` field of the Handshake packet).
The rejection codes are divided into several ranges:

  - SRT internal
  - Predefined application level codes
  - User defined (custom) codes

In the SRT API these ranges are marked with the following constants (preprocessor definitions):

  - `SRT_REJC_INTERNAL` = 0
  - `SRT_REJC_PREDEFINED` = 1000
  - `SRT_REJC_USERDEFINED` = 2000
  
When transferred on the wire, the API value is incremented by 1000 to become the `Handshake Type` field value. In the following sections the values of rejection reason codes are given in accordance with the API values.


### SRT Internal Rejection Codes

Defined in [**srt.h**](srtcore/srt.h), these codes provide the reason why a connection is rejected by SRT. They cover the reserved range 0 - 999 (below `SRT_REJC_PREDEFINED`).

Naming: `SRT_REJ_*`

  - `SRT_REJ_UNKNOWN` = 0
  - `SRT_REJ_SYSTEM` = 1
  - ...
  - `SRT_REJ_CRYPTO` = 17


### Extended Rejection Codes

As defined in [**access_control.h**](srtcore/access_control.h), these are standard server error codes including those adopted from HTTP. They provide the reason why an application rejects a connection. The value is expected to be set by an application via the listener callback if it wants to reject an incoming connection request. These codes cover the reserved range 1000 - 1999 (`SRT_REJC_PREDEFINED` - `SRT_REJC_USERDEFINED`).


Subranges (1000 + value):

  - **0 - 99**: Reserved for unique SRT-specific codes (unused by HTTP)
  - **100 - 399**: Info, Success, and Redirection in HTTP (unused by SRT)
  - **400 - 599**: Client and server errors in HTTP (adopted by SRT)

Naming: `SRT_REJX_*`

Example:

  - `SRT_REJX_KEY_NOTSUP` (1001): The key used in the StreamID keyed string is not supported by the service.
  - `SRT_REJX_BAD_REQUEST` (1400)
  - ...

### User Defined Rejection Codes

These codes can be freely defined by an application. They can be custom codes, not adopted by other vendors. For example, `2005: “Makito license expired”`. They cover the reserved range 2000 - 2999 (higher than `SRT_REJC_USERDEFINED`).


## API Function Rejection Codes

SRT's API function rejection codes refer to system-level error conditions caused by SRT-specific settings or operating conditions. They are uninfluenced by application-related events, and applications are not permitted to use or simulate these codes.

The table below lists the rejection codes as defined in [**srt.h**](srtcore/srt.h) (click the *Rejection Reason* link to view a complete description).

| *Code* | *Rejection Reason*                                 | *Since* | *Description*                                                                                                  |
|:------:|:-------------------------------------------------- |:-------:|:-------------------------------------------------------------------------------------------------------------- |
|    0   | [SRT_REJ_UNKNOWN](#SRT_REJ_UNKNOWN)                | 1.3.4   | Fallback value for cases where connection is not rejected.                                                     |
|    1   | [SRT_REJ_SYSTEM](#SRT_REJ_SYSTEM)                  | 1.3.4   | System function reported a failure.                                                                            |
|    2   | [SRT_REJ_PEER](#SRT_REJ_PEER)                      | 1.3.4   | Connection rejected by peer, with no additional details.                                                       |
|    3   | [SRT_REJ_RESOURCE](#SRT_REJ_RESOURCE)              | 1.3.4   | Problem with resource allocation (usually memory).                                                             |
|    4   | [SRT_REJ_ROGUE](#SRT_REJ_ROGUE)                    | 1.3.4   | Data sent by one party cannot be interpreted.                                                                  |
|    5   | [SRT_REJ_BACKLOG](#SRT_REJ_BACKLOG)                | 1.3.4   | Listener's backlog has been exceeded.                                                                          |
|    6   | [SRT_REJ_IPE](#SRT_REJ_IPE)                        | 1.3.4   | Internal Program Error.                                                                                        |
|    7   | [SRT_REJ_CLOSE](#SRT_REJ_CLOSE)                    | 1.3.4   | Listener socket received a request as it is being closed.                                                      |
|    8   | [SRT_REJ_VERSION](#SRT_REJ_VERSION)                | 1.3.4   | Minimum version requirement for a connection not satisfied by one party.                                       |
|    9   | [SRT_REJ_RDVCOOKIE](#SRT_REJ_RDVCOOKIE)            | 1.3.4   | Rendezvous cookie collision.                                                                                   |
|   10   | [SRT_REJ_BADSECRET](#SRT_REJ_BADSECRET)            | 1.3.4   | Both parties have defined connection passphrases that differ.                                                  |
|   11   | [SRT_REJ_UNSECURE](#SRT_REJ_UNSECURE)              | 1.3.4   | Only one party has set up a connection password.                                                               |
|   12   | [SRT_REJ_MESSAGEAPI](#SRT_REJ_MESSAGEAPI)          | 1.3.4   | [`SRTO_MESSAGEAPI`](API-socket-options.md#SRTO_MESSAGEAPI) flag is different on both connection parties.       |
|   13   | [SRT_REJ_CONGESTION](#SRT_REJ_CONGESTION)          | 1.3.4   | Incompatible congestion-controller type.                                                                       |
|   14   | [SRT_REJ_FILTER](#SRT_REJ_FILTER)                  | 1.3.4   | [`SRTO_PACKETFILTER`](API-socket-options.md#SRTO_PACKETFILTER) option is different on both connection parties. |
|   15   | [SRT_REJ_GROUP](#SRT_REJ_GROUP)                    | 1.4.2   | Group type or group settings are incompatible between connection parties.                                      |
|   16   | [SRT_REJ_TIMEOUT](#SRT_REJ_TIMEOUT)                | 1.4.2   | Connection not rejected, but timed out.                                                                        |
|   17   | [SRT_REJ_CRYPTO](#SRT_REJ_CRYPTO)                  | 1.5.2   | Connection rejected due to unsupported or mismatching encryption mode.                                         |
|        | <img width=250px height=1px/>                      |         |                                                                                                                |


## Access Control Rejection Codes

SRT's access control rejection codes are intended for use by applications to forcefully reject connections in SRT listener callbacks. They are intended only as a guide to promote standardization. If they are used in an application, a description of their specific implementation should be published (the descriptions in this documentation are not definitive).

The table below lists the rejection codes as defined in [**access_control.h**](srtcore/access_control.h) (click the *Rejection Reason* link to view a complete description).

| *Code* | *Rejection Reason*                                | *Since* | *Description*                                                                                                  |
|:------:|:------------------------------------------------- |:-------:|:-------------------------------------------------------------------------------------------------------------- |
| 1000   | [SRT_REJX_FALLBACK](#SRT_REJX_FALLBACK)           | 1.4.2   | Callback handler has interrupted an incoming connection.                                                       |
| 1001   | [SRT_REJX_KEY_NOTSUP](#SRT_REJX_KEY_NOTSUP)       | 1.4.2   | Key specified in StreamID string not supported by application.                                                 |
| 1002   | [SRT_REJX_FILEPATH](#SRT_REJX_FILEPATH)           | 1.4.2   | Resource type designates file where path has wrong syntax or is not found.                                     |
| 1003   | [SRT_REJX_HOSTNOTFOUND](#SRT_REJX_HOSTNOTFOUND)   | 1.4.2   | The host specified in the `h` key cannot be identified.                                                        | 
| 1400   | [SRT_REJX_BAD_REQUEST](#SRT_REJX_BAD_REQUEST)     | 1.4.2   | General syntax error.                                                                                          |
| 1401   | [SRT_REJX_UNAUTHORIZED](#SRT_REJX_UNAUTHORIZED)   | 1.4.2   | Authentication failed; client unauthorized to access the resource.                                             |
| 1402   | [SRT_REJX_OVERLOAD](#SRT_REJX_OVERLOAD)           | 1.4.2   | Server load too heavy to process request, or credit limit exceeded.                                            |
| 1403   | [SRT_REJX_FORBIDDEN](#SRT_REJX_FORBIDDEN)         | 1.4.2   | Access denied to the resource for any reason.                                                                  |
| 1404   | [SRT_REJX_NOTFOUND](#SRT_REJX_NOTFOUND)           | 1.4.2   | Resource specified by `r` and `h` keys cannot be found.                                                        |
| 1405   | [SRT_REJX_BAD_MODE](#SRT_REJX_BAD_MODE)           | 1.4.2   | Mode specified in the `m` key in StreamID is not supported for this request.                                   |
| 1406   | [SRT_REJX_UNACCEPTABLE](#SRT_REJX_UNACCEPTABLE)   | 1.4.2   | Unavailable parameters in `SocketID`, or `m=publish` data format not supported.                                |
| 1409   | [SRT_REJX_CONFLICT](#SRT_REJX_CONFLICT)           | 1.4.2   | Resource specified by `r` and `h` keys is locked for modification.                                             | 
| 1415   | [SRT_REJX_NOTSUP_MEDIA](#SRT_REJX_NOTSUP_MEDIA)   | 1.4.2   | Media type  not supported by the application.                                                                  |
| 1423   | [SRT_REJX_LOCKED](#SRT_REJX_LOCKED)               | 1.4.2   | Resource is locked against any access.                                                                         |
| 1424   | [SRT_REJX_FAILED_DEPEND](#SRT_REJX_FAILED_DEPEND) | 1.4.2   | Dependent entity for the request is not present.                                                               |
| 1500   | [SRT_REJX_ISE](#SRT_REJX_ISE)                     | 1.4.2   | Internal server error.                                                                                         |
| 1501   | [SRT_REJX_UNIMPLEMENTED](#SRT_REJX_UNIMPLEMENTED) | 1.4.2   | Request not supported by current version of the service.                                                       |
| 1502   | [SRT_REJX_GW](#SRT_REJX_GW)                       | 1.4.2   | Target endpoint rejected connection from gateway server                                                        |
| 1503   | [SRT_REJX_DOWN](#SRT_REJX_DOWN)                   | 1.4.2   | Service is down for maintenance.                                                                               |
| 1505   | [SRT_REJX_VERSION](#SRT_REJX_VERSION)             | 1.4.2   | SRT application version not supported.                                                                             |
| 1507   | [SRT_REJX_NOROOM](#SRT_REJX_NOROOM)               | 1.4.2   | Data stream cannot be archived due to lack of storage space.                                                   |
|        | <img width=250px height=1px/>                     |         |                                                                                                                |


**NOTE**: SRT rejection codes follow this prefix convention:

  - `SRT_REJ`: standard rejection codes from SRT API functions (0 - 99)
  - `SRT_REJC`: mark the border values between ranges.
  - `SRT_REJX`: extended rejection codes (400 – 599)  :warning: *@max is this the correct range (see comments above)?*


## API Function Rejection Reasons


#### SRT_REJ_UNKNOWN

A fallback value for cases when there was no connection rejected.


#### SRT_REJ_SYSTEM

One system function reported a failure. Usually this means some system
error or lack of system resources to complete the task.

  
#### SRT_REJ_PEER

The connection has been rejected by the peer, but no further details are available.
This usually means that the peer doesn't support rejection reason reporting.

  
#### SRT_REJ_RESOURCE

A problem with resource allocation (usually memory).

  
#### SRT_REJ_ROGUE

The data sent by one party to another cannot be properly interpreted. This
should not happen during normal usage, unless it's a bug, or some weird
events are happening on the network.

  
#### SRT_REJ_BACKLOG

The listener's backlog has exceeded its queue limit (there are many other callers 
waiting for the opportunity to be connected and the "wait queue" has 
reached its limit).

  
#### SRT_REJ_IPE

Internal Program Error. This should not happen during normal usage. It
usually indicates a bug in the software (although this can be reported by both
local and foreign hosts).

  
#### SRT_REJ_CLOSE

The listener socket was able to receive the request, but is currently
 being closed. It's likely that the next request will result in a timeout.

  
#### SRT_REJ_VERSION

One party in the connection has set up a minimum version that is required for
that connection, but the other party doesn't satisfy this requirement.

  
#### SRT_REJ_RDVCOOKIE

Rendezvous cookie collision. Normally, the probability that this will happen is 
negligible. However, it *can* result from a misconfiguration when, in attempting 
to make a rendezvous connection, both parties try to bind to the same IP address, 
or both are local addresses of the same host. In such a case the sent handshake 
packets are returned to the same host as if they were sent by the peer (i.e. a 
party is sending to itself). In such situations, this reject reason will be 
reported for every attempt.

  
#### SRT_REJ_BADSECRET

Both parties have defined a passphrase for a connection, but they differ.

  
#### SRT_REJ_UNSECURE

Only one connection party has set up a password. See also the 
[`SRTO_ENFORCEDENCRYPTION`](API-socket-options.md#SRTO_ENFORCEDENCRYPTION) flag.

  
#### SRT_REJ_MESSAGEAPI

The value of the [`SRTO_MESSAGEAPI`](API-socket-options.md#SRTO_MESSAGEAPI) 
flag is different on both parties in a connection.

  
#### SRT_REJ_CONGESTION

The [`SRTO_CONGESTION`](API-socket-options.md#SRTO_CONGESTION)option has 
been set up differently on both parties in a connection.

  
#### SRT_REJ_FILTER

The [`SRTO_PACKETFILTER`](API-socket-options.md#SRTO_PACKETFILTER) option 
has been set differently on both parties in a connection.

  
#### SRT_REJ_GROUP

The group type or some group settings are incompatible between connection parties. 
While every connection within a bonding group may have different target addresses, 
they should all designate the same endpoint and the same SRT application. If this 
condition isn't satisfied, then the peer will respond with a different peer group 
ID for the connection that is trying to contact a machine/application that is 
completely different from the existing connections in the bonding group.

  
#### SRT_REJ_TIMEOUT

The connection wasn't rejected, but it timed out. This code is always sent on
a connection timeout, but this is the only way to get this state in non-blocking
mode (see [`SRTO_RCVSYN`](API-socket-options.md#SRTO_RCVSYN)).

There may also be server and user rejection codes, as defined by the 
`SRT_REJC_INTERNAL`, `SRT_REJC_PREDEFINED`, and `SRT_REJC_USERDEFINED`
constants. Note that the number space from the value of `SRT_REJC_PREDEFINED`
and above is reserved for "predefined codes" (`SRT_REJC_PREDEFINED` value plus
adopted HTTP codes). Values above `SRT_REJC_USERDEFINED` are freely defined by
the application.


[:arrow_up: &nbsp; Back to top](#srt-rejection-codes)


## Access Control Rejection Reasons

An SRT listener callback handler can decide to reject an  incoming connection. 
Under normal circumstances, the rejection code is predefined as `SRT_REJ_RESOURCE`. 
The handler can, however, set its own rejection code. There are two numbered spaces 
intended for this purpose (as the range below `SRT_REJC_PREDEFINED` is reserved 
for internal codes):

- `SRT_REJC_PREDEFINED` and above: These are predefined errors. Errors from this 
range (that is, below `SRT_REJC_USERDEFINED`) have their definitions provided in
the `access_control.h` public header file. The intention is that applications
using these codes understand the situations they describe in a standard way.

- `SRT_REJC_USERDEFINED` and above: These are errors that are freely defined by 
the application. Codes from this range can be only understood if each application 
knows the code definitions of the other. These codes should be used only after 
making sure that the applications at either end of a connection understand them.

The intention here is for the predefined codes to be consistent with the HTTP
standard codes. Such code can be set by using the `srt_setrejectreason` function.

The SRT-specific codes are:

#### SRT_REJX_FALLBACK

This code should be set by the callback handler in the beginning in case
the application needs to be informed that the callback handler
actually has interpreted the incoming connection, but hasn't set a
more appropriate code describing the situation.

#### SRT_REJX_KEY_NOTSUP

Indicates there was a key specified in the StreamID string that this application
doesn't support. Note that it's not obligatory for the application to
react this way - it may chose to ignore unknown keys completely, or
to have some keys in the ignore list (which it won't interpret, but tolerate)
while rejecting any others. It is also up to the application
to decide to return this specific error, or more generally report
the syntax error with `SRT_REJX_BAD_REQUEST`.

#### SRT_REJX_FILEPATH

The resource type designates a file, and the path either has the wrong syntax
or is not found. In the case where `t=file`, the path should be specified under
the `r` key, and the file specified there must be able to be saved this way.
It's up to the application to decide how to treat this path, how to parse it,
and what this path specifically means. For the `r` key, the application should
at least handle the single filename, and have storage space available to save
it (provided a file of the same name does not already exist there).  The
application should decide whether and how to handle all other situations (like
directory path, special markers in the path to be interpreted by the
application, etc.), or to report this error.

#### SRT_REJX_HOSTNOTFOUND

The host specified in the `h` key cannot be identified. The `h` key is
generally for a situation when you have multiple DNS names for a host,
so an application may want to extract the name from the URI and set it
to the `h` key so that the application can distinguish the request also by
the target host name. The application may, however, limit the number of
recognized services by host name to some predefined names and not
handle the others, even if this is properly resolved by DNS. In this
case it should report this error.

The other error codes are HTTP codes adapted for SRT:

#### SRT_REJX_BAD_REQUEST

General syntax error. This can be reported in any case when parsing
the StreamID contents failed, or it cannot be properly interpreted.

#### SRT_REJX_UNAUTHORIZED

Authentication failed, which makes the client unauthorized to access the
resource. This error, however, confirms that the syntax is correct and
the resource has been properly identified. Note that this cannot be
reported when you use a simple user-password authentication
method because in this case the password is verified only after the
listener callback handler accepts the connection. This error is rather
intended to be reported in the case of `t=auth` when the authentication
process has generated some valid session ID, but then the session
connection has specified a resource that is not within the frames
of that authentication.

#### SRT_REJX_OVERLOAD

The server is too heavily loaded to process the request, or the credit limit 
for accessing the service and the resource has been exceeded.
In HTTP the description mentions payment for a service, but
it is also used by some services for general "credit" management
for a client. In SRT it should be used when the service is doing
any kind of credit management to limit access to selected clients
that "have" enough credit, even if the credit is something the client
can recharge itself, or that can be granted depending on available
service resources.

#### SRT_REJX_FORBIDDEN

Access denied to the resource for any reason. This error is
independent of an authorization or authentication error (as reported
by `SRT_REJX_UNAUTHORIZED`). The application can decide which
is more appropriate. This error is usually intended for
a resource that should only be accessed after a successful
authorization over a separate auth-only connection, where the query
in StreamID has correctly specified the resource identity and mode,
but the session ID (in the `s` key) is either (a) not specified, or 
(b) specifies a valid session, but the authorization region for this
session does not include the specified resource.

#### SRT_REJX_NOTFOUND

The resource specified in the `r` key (in combination with the `h` key)
is not found at this time. This error should be only reported if the
information about resource accessibility is allowed to be publicly
visible. Otherwise, the application might report authorization
errors.

#### SRT_REJX_BAD_MODE

The mode specified in the `m` key in StreamID is not supported for this request.
This may apply to read-only or write-only resources, as well when interactive
(bidirectional) access is not valid for a resource.

#### SRT_REJX_UNACCEPTABLE

Applies when the parameters specified in SocketID cannot be satisfied for the
requested resource, or when `m=publish` but the data format is not acceptable.
This is a general error reporting an unsupported format for data that appears to 
be wrong when sending, or a restriction on the data format (as specified in the 
details of the resource specification) such that it cannot be provided 
when receiving.

#### SRT_REJX_CONFLICT

The resource being accessed (as specified by `r` and `h` keys) is locked for
modification. This error should only be reported for `m=publish` when the
resource being accessed is read-only because another client (not necessarily
connected through SRT):

  - is currently publishing into this resource
  - has reserved this resource ID for publishing

Note that this error should be reported when there is no other reason for
having a problem accessing the resource.

#### SRT_REJX_NOTSUP_MEDIA

The media type is not supported by the application. The media type is
specified in the `t` key. The currently standard types are
`stream`, `file` and `auth`. An application may extend this list, and
is not obliged to support all of the standard types.

#### SRT_REJX_LOCKED

The resource being accessed is locked against any access. This is similar to
`SRT_REJX_CONFLICT`, but in this case the resource is locked for reading 
and writing. This is for when the resource should be shown as existing and 
available to the client, but access is temporarily blocked.

#### SRT_REJX_FAILED_DEPEND

The dependent entity for the request is not present. In this case the
dependent entity is the session, which should be specified in the `s`
key. This means that the specified session ID is nonexistent, or it
has already expired.

#### SRT_REJX_ISE

Internal server error. This is for a general case when a request has
been correctly verified, with no related problems found, but an
unexpected error occurs after the processing of the request has started.

#### SRT_REJX_UNIMPLEMENTED

The request was correctly recognized, but the current software version
of the service (be it SRT or any other software component) doesn't
support it. This should be reported for a case where some features to
be specified in the StreamID request are supposed to be supported in a
predictable future, but the current version of the server does not
support it, or the support for this feature in this version has been
temporarily blocked. This shouldn't be reported for existing features that are
being deprecated, or older features that are no longer supported
(for this case the general `SRT_REJX_BAD_REQUEST` is more appropriate).

#### SRT_REJX_GW

The server acts as a gateway and the target endpoint rejected the
connection. The reason the connection was rejected is unspecified.
The gateway cannot forward the original rejection code from the
target endpoint because this would suggest the error was on the
gateway itself. Use this error with some other mechanism to report
the original target error, if possible.

#### SRT_REJX_DOWN

The service is down for maintenance. This can only be reported
when the service has been temporarily replaced by a stub that is only
reporting this error, while the real service is down for maintenance.

#### SRT_REJX_VERSION

Application version not supported. This can refer to an application feature
that is unsupported (possibly from an older SRT version), or to a feature
that is no longer supported because of backward compatibility requirements.

#### SRT_REJX_NOROOM

The data stream cannot be archived due to a lack of storage space. This is
reported when a request to send a file or a live stream to be archived is
unsuccessful. Note that the length of a file transmission is usually
pre-declared, so this error can be reported early. It can also be reported when
the stream is of undefined length, and there is no more storage space
available.


[:arrow_up: &nbsp; Back to top](#srt-rejection-codes)
