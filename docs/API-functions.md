# SRT API Functions

<h3 id="Library Initialization">Library Initialization</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [srt_startup](#srt_startup)                       | Called at the start of an application that uses the SRT library                                                |
| [srt_cleanup](#srt_cleanup)                       | Cleans up global SRT resources before exiting an application                                                   |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |


<h3 id="creating-and-configuring-sockets">Creating and Configuring Sockets</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [srt_socket](#srt_socket)                         | Deprecated                                                                                                     |
| [srt_create_socket](#srt_create_socket)           | Creates an SRT socket                                                                                          |
| [srt_bind](#srt_bind)                             | Binds a socket to a local address and port                                                                     |
| [srt_bind_acquire](#srt_bind_acquire)             | Acquires a given UDP socket instead of creating one                                                            |
| [srt_getsockstate](#srt_getsockstate)             | Gets the current status of the socket                                                                          |
| [srt_getsndbuffer](#srt_getsndbuffer)             | Retrieves information about the sender buffer                                                                  |
| [srt_close](#srt_close)                           | Closes the socket or group and frees all used resources                                                        |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h3 id="connecting">Connecting</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [srt_listen](#srt_listen)                         | Sets up the listening state on a socket                                                                        |
| [srt_accept](#srt_accept)                         | Accepts a connection; creates/returns a new socket or group ID                                                 |
| [srt_accept_bond](#srt_accept_bond)               | Accepts a connection pending on any sockets passed in the `listeners` array <br/> of `nlisteners` size               |
| [srt_listen_callback](#srt_listen_callback)       | Installs/executes a callback hook on a socket created to handle the incoming connection <br/> on a listening socket  |
| [srt_connect](#srt_connect)                       | Connects a socket or a group to a remote party with a specified address and port                               |
| [srt_connect_bind](#srt_connect_bind)             | Same as [`srt_bind`](#srt_bind) then [`srt_connect`](#srt_connect) if called with socket [`u`](#u)             |
| [srt_connect_debug](#srt_connect_debug)           | Same as [`srt_connect`](#srt_connect)but allows specifying ISN (developers only)                               |
| [srt_rendezvous](#srt_rendezvous)                 | Performs a rendezvous connection                                                                               |
| [srt_connect_callback](#srt_connect_callback)     | Installs/executes a callback hook on socket/group [`u`](#u) after connection resolution/failure                |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h3 id="socket-group-management">Socket Group Management</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [SRT_GROUP_TYPE](#SRT_GROUP_TYPE)                 | Group types collected in an [`SRT_GROUP_TYPE`](#SRT_GROUP_TYPE) enum                                           | 
| [SRT_SOCKGROUPCONFIG](#SRT_SOCKGROUPCONFIG)       | Structure used to define entry points for connections for [`srt_connect_group`](#srt_connect_group)            |
| [SRT_SOCKGROUPDATA](#SRT_SOCKGROUPDATA)           | Most important structure for group member status                                                               |
| [SRT_MEMBERSTATUS](#SRT_MEMBERSTATUS)             | Enumeration type that defines the state of a member connection in the group                                    |
| [srt_create_group](#srt_create_group)             | Creates a new group of type `type`                                                                             |
| [srt_include](#srt_include)                       | Adds a socket to a group                                                                                       |
| [srt_exclude](#srt_exclude)                       | Removes a socket from a group to which it currently belongs                                                    |
| [srt_groupof](#srt_groupof)                       | Returns the group ID of a socket, or `SRT_INVALID_SOCK`                                                        |
| [srt_group_data](#srt_group_data)                 | Obtains the current member state of the group specified in `socketgroup`                                       |
| [srt_connect_group](#srt_connect_group)           | Similar to calling [`srt_connect`](#srt_connect) or [`srt_connect_bind`](#srt_connect_bind) <br/> in a loop for every item in an array. |
| [srt_prepare_endpoint](#srt_prepare_endpoint)     | Prepares a default [`SRT_SOCKGROUPCONFIG`](#SRT_SOCKGROUPCONFIG) object as an element of <br/> an array for [`srt_connect_group`](#srt_connect_group)   |
| [srt_create_config](#srt_create_config)           | Creates a dynamic object for specifying socket options                                                         |
| [srt_delete_config](#srt_delete_config)           | Deletes the configuration object                                                                               |
| [srt_config_add](#srt_config_add)                 | Adds a configuration option to the configuration object                                                        |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h3 id="options-and-properties">Options and Properties</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [srt_getpeername](#srt_getpeername)               | Retrieves the remote address to which the socket is connected                                                  |
| [srt_getsockname](#srt_getsockname)               | Extracts the address to which the socket was bound                                                             |
| [srt_getsockopt](#srt_getsockopt)                 | Gets the value of the given socket option (from a socket or a group)                                           |
| [srt_getsockflag](#srt_getsockflag)               | Gets the value of the given socket option (from a socket or a group)                                           |
| [srt_setsockopt](#srt_setsockopt)                 | Sets a value for a socket option in the socket or group                                                        |
| [srt_setsockflag](#srt_setsockflag)               | Sets a value for a socket option in the socket or group                                                        |
| [srt_getversion](#srt_getversion)                 | Get SRT version value                                                                                          |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h3 id="helper-data-types-for-transmission">Helper Data Types for Transmission</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [SRT_MSGCTRL](#SRT_MSGCTRL)                       | Used in [`srt_sendmsg2`](#srt_sendmsg) and [`srt_recvmsg2`](#srt_recvmsg2) calls; <br/> specifies some extra parameters  |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h3 id="transmission">Transmission</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [srt_send](#srt_send)                             | Sends a payload to a remote party over a given socket                                                          |
| [srt_sendmsg](#srt_sendmsg)                       | Sends a payload to a remote party over a given socket                                                          |
| [srt_sendmsg2](#srt_sendmsg2)                     | Sends a payload to a remote party over a given socket                                                          |
| [srt_recv](#srt_recv)                             | Extracts the payload waiting to be received                                                                    |
| [srt_recvmsg](#srt_recvmsg)                       | Extracts the payload waiting to be received                                                                    |
| [srt_recvmsg2](#srt_recvmsg2)                     | Extracts the payload waiting to be received                                                                    |
| [srt_sendfile](#srt_sendfile)                     | Function dedicated to sending a file                                                                           |
| [srt_recvfile](#srt_recvfile)                     | Function dedicated to receiving a file                                                                         |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h3 id="performance-tracking">Performance Tracking</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [srt_bstats](#srt_bstats)                         | Reports the current statistics                                                                                 |
| [srt_bistats](#srt_bistats)                       | Reports the current statistics                                                                                 |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h3 id="asynchronous-operations-epoll">Asynchronous Operations (epoll)</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [srt_epoll_create](#srt_epoll_create)             | Creates a new epoll container                                                                                  |
| [srt_epoll_add_usock](#srt_epoll_add_usock)       | Adds a user socket to a container, or updates an existing socket subscription                                  |
| [srt_epoll_add_ssock](#srt_epoll_add_ssock)       | Adds a system socket to a container, or updates an existing socket subscription                                |
| [srt_epoll_update_usock](#srt_epoll_update_usock) | Adds a user socket to a container, or updates an existing socket subscription                                  |
| [srt_epoll_update_ssock](#srt_epoll_update_ssock) | Adds a system socket to a container, or updates an existing socket subscription                                |
| [srt_epoll_remove_usock](#srt_epoll_remove_usock) | Removes a specified user socket from an epoll container; clears all readiness states for that socket           |
| [srt_epoll_remove_ssock](#srt_epoll_remove_ssock) | Removes a specified system socket from an epoll container; clears all readiness states for that socket         |
| [srt_epoll_wait](#srt_epoll_wait)                 | Blocks the call until any readiness state occurs in the epoll container                                        |
| [srt_epoll_uwait](#srt_epoll_uwait)               | Blocks a call until any readiness state occurs in the epoll container                                          |
| [srt_epoll_clear_usocks](#srt_epoll_clear_usocks) | removes all SRT ("user") socket subscriptions from the epoll container identified by [`eid`](#eid)             |
| [srt_epoll_set](#srt_epoll_set)                   | Allows setting or retrieving flags that change the default behavior of the epoll functions                     |
| [srt_epoll_release](#srt_epoll_release)           | Deletes the epoll container                                                                                    |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h3 id="logging-control">Logging Control</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [srt_setloglevel](#srt_setloglevel)               | Sets the minimum severity for logging                                                                          |
| [srt_addlogfa](#srt_addlogfa)                     | Add a functional area (FA), which is an additional filtering mechanism for logging                             |
| [srt_dellogfa](#srt_dellogfa)                     | Delete a functional area (FA), which is an additional filtering mechanism for logging                          |
| [srt_resetlogfa](#srt_resetlogfa)                 | Reset a functional area (FA), which is an additional filtering mechanism for logging                           |
| [srt_setloghandler](#srt_setloghandler)           | Replaces default standard stream for error logging                                                             |
| [srt_setlogflags](#srt_setlogflags)               | Allows configuring parts of log information that are not to be passed                                          |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h3 id="time-access">Time Access</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [srt_time_now](#srt_time_now)                     | Get time in microseconds elapsed since epoch using SRT internal clock <br/> (steady or monotonic clock)              |
| [srt_connection_time](#srt_connection_time)       | Get connection time in microseconds elapsed since epoch using SRT internal clock <br/> (steady or monotonic clock)   |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h3 id="diagnostics">Diagnostics</h3>
  
| *Function / Structure*                            | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [srt_getlasterror](#srt_getlasterror)             | Get the numeric code of the last error                                                                         |
| [srt_strerror](#srt_strerror)                     | Returns a string message that represents a given SRT error code and possibly <br/> the `errno` value, if not 0 |
| [srt_getlasterror_str](#srt_getlasterror_str)     | Gets the text message for the last error                                                                       |
| [srt_clearlasterror](#srt_clearlasterror)         | Clears the last error                                                                                          |
| [srt_rejectreason_str](#srt_rejectreason_str)     | Returns a constant string for the reason of the connection rejected, as per given code ID                      |
| [srt_setrejectreason](#srt_setrejectreason)       | Sets the rejection code on the socket                                                                          |
| [srt_getrejectreason](#srt_getrejectreason)       | Provides a detailed reason for a failed connection attempt                                                     |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h4 id="rejection-reasons">Rejection Reasons</h4>

| *Rejection Reason*                                | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
| [SRT_REJ_UNKNOWN](#SRT_REJ_UNKNOWN)               | A fallback value for cases when there was no connection rejected                                               |
| [SRT_REJ_SYSTEM](#SRT_REJ_SYSTEM)                 | A system function reported a failure                                                                           |
| [SRT_REJ_PEER](#SRT_REJ_PEER)                     | The connection has been rejected by peer, but no further details are available                                 |
| [SRT_REJ_RESOURCE](#SRT_REJ_RESOURCE)             | A problem with resource allocation (usually memory)                                                            |
| [SRT_REJ_ROGUE](#SRT_REJ_ROGUE)                   | The data sent by one party to another cannot be properly interpreted                                           |
| [SRT_REJ_BACKLOG](#SRT_REJ_BACKLOG)               | The listener's backlog has exceeded                                                                            |
| [SRT_REJ_IPE](#SRT_REJ_IPE)                       | Internal Program Error                                                                                         |
| [SRT_REJ_CLOSE](#SRT_REJ_CLOSE)                   | The listener socket received a request as it is being closed                                                   |
| [SRT_REJ_VERSION](#SRT_REJ_VERSION)               | A party did not satisfy the minimum version requirement that had been set up for a connection                  |
| [SRT_REJ_RDVCOOKIE](#SRT_REJ_RDVCOOKIE)           | Rendezvous cookie collision                                                                                    |
| [SRT_REJ_BADSECRET](#SRT_REJ_BADSECRET)           | Both parties have defined a passprhase for connection and they differ                                          |
| [SRT_REJ_UNSECURE](#SRT_REJ_UNSECURE)             | Only one connection party has set up a password                                                                |
| [SRT_REJ_MESSAGEAPI](#SRT_REJ_MESSAGEAPI)         | The value for [`SRTO_MESSAGEAPI`](../docs/APISocketOptions.md#SRTO_MESSAGEAPI) flag is different on both connection parties  |
| [SRT_REJ_FILTER](#SRT_REJ_FILTER)                 | The [`SRTO_PACKETFILTER`](../docs/APISocketOptions.md#SRTO_PACKETFILTER) option has been set differently on both connection parties  |
| [SRT_REJ_GROUP](#SRT_REJ_GROUP)                   | The group type or some group settings are incompatible for both connection parties                             |
| [SRT_REJ_TIMEOUT](#SRT_REJ_TIMEOUT)               | The connection wasn't rejected, but it timed out                                                               |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |

<h4 id="error-codes">Error Codes</h4>
  
| *Error Code*                                      | *Description*                                                                                                  |
|:------------------------------------------------- |:-------------------------------------------------------------------------------------------------------------- |
[`SRT_EUNKNOWN`](#srt_eunknown)                     | Internal error when setting the right error code                                                               |
[`SRT_SUCCESS`](#srt_success)                       | The value set when the last error was cleared and no error has occurred since then                             |
[`SRT_ECONNSETUP`](#srt_econnsetup)                 | General setup error resulting from internal system state                                                       |
[`SRT_ENOSERVER`](#srt_enoserver)                   | Connection timed out while attempting to connect to the remote address                                         |
[`SRT_ECONNREJ`](#srt_econnrej)                     | Connection has been rejected                                                                                   |
[`SRT_ESOCKFAIL`](#srt_esockfail)                   | An error occurred when trying to call a system function on an internally used UDP socket                       |
[`SRT_ESECFAIL`](#srt_esecfail)                     | A possible tampering with the handshake packets was detected, or encryption request <br/> wasn't properly fulfilled. |
[`SRT_ESCLOSED`](#srt_esclosed)                     | A socket that was vital for an operation called in blocking mode has been closed <br/> during the operation          |
[`SRT_ECONNFAIL`](#srt_econnfail)                   | General connection failure of unknown details                                                                  |
[`SRT_ECONNLOST`](#srt_econnlost)                   | The socket was properly connected, but the connection has been broken                                          |
[`SRT_ENOCONN`](#srt_enoconn)                       | The socket is not connected                                                                                    |
[`SRT_ERESOURCE`](#srt_eresource)                   | System or standard library error reported unexpectedly for unknown purpose                                     |
[`SRT_ETHREAD`](#srt_ethread)                       | System was unable to spawn a new thread when requried                                                          |
[`SRT_ENOBUF`](#srt_enobuf)                         | System was unable to allocate memory for buffers                                                               |
[`SRT_ESYSOBJ`](#srt_esysobj)                       | System was unable to allocate system specific objects                                                          |
[`SRT_EFILE`](#srt_efile)                           | General filesystem error (for functions operating with file transmission)                                      |
[`SRT_EINVRDOFF`](#srt_einvrdoff)                   | Failure when trying to read from a given position in the file                                                  |
[`SRT_ERDPERM`](#srt_erdperm)                       | Read permission was denied when trying to read from file                                                       |
[`SRT_EINVWROFF`](#srt_einvwroff)                   | Failed to set position in the written file                                                                     |
[`SRT_EWRPERM`](#srt_ewrperm)                       | Write permission was denied when trying to write to a file                                                     |
[`SRT_EINVOP`](#srt_einvop)                         | Invalid operation performed for the current state of a socket                                                  |
[`SRT_EBOUNDSOCK`](#srt_eboundsock)                 | The socket is currently bound and the required operation cannot be performed in this state                     |
[`SRT_ECONNSOCK`](#srt_econnsock)                   | The socket is currently connected and therefore performing the required operation is not possible              |
[`SRT_EINVPARAM`](#srt_einvparam)                   | Call parameters for API functions have some requirements that were not satisfied                               |
[`SRT_EINVSOCK`](#srt_einvsock)                     | The API function required an ID of an entity (socket or group) and it was invalid                              |
[`SRT_EUNBOUNDSOCK`](#srt_eunboundsock)             | The operation to be performed on a socket requires that it first be explicitly bound                           |
[`SRT_ENOLISTEN`](#srt_enolisten)                   | The socket passed for the operation is required to be in the listen state                                      |
[`SRT_ERDVNOSERV`](#srt_erdvnoserv)                 | The required operation cannot be performed when the socket is set to rendezvous mode                           |
[`SRT_ERDVUNBOUND`](#srt_erdvunbound)               | An attempt was made to connect to a socket set to rendezvous mode that was not first bound                     |
[`SRT_EINVALMSGAPI`](#srt_einvalmsgapi)             | The function was used incorrectly in the message API                                                           |
[`SRT_EINVALBUFFERAPI`](#srt_einvalbufferapi)       | The function was used incorrectly in the stream (buffer) API                                                   |
[`SRT_EDUPLISTEN`](#srt_eduplisten)                 | The port tried to be bound for listening is already busy                                                       |
[`SRT_ELARGEMSG`](#srt_elargemsg)                   | Size exceeded                                                                                                  |
[`SRT_EINVPOLLID`](#srt_einvpollid)                 | The epoll ID passed to an epoll function is invalid                                                            |
[`SRT_EPOLLEMPTY`](#srt_epollempty)                 | The epoll container currently has no subscribed sockets                                                        |
[`SRT_EASYNCFAIL`](#srt_easyncfail)                 | General asynchronous failure (not in use currently)                                                            |
[`SRT_EASYNCSND`](#srt_easyncsnd)                   | Sending operation is not ready to perform                                                                      |
[`SRT_EASYNCRCV`](#srt_easyncrcv)                   | Receiving operation is not ready to perform                                                                    |
[`SRT_ETIMEOUT`](#srt_etimeout)                     | The operation timed out                                                                                        |
[`SRT_ECONGEST`](#srt_econgest)                     | With [`SRTO_TSBPDMODE`](../docs/APISocketOptions.md#SRTO_TSBPDMODE) and [`SRTO_TLPKTDROP`](../docs/APISocketOptions.md#SRTO_TLPKTDROP) set to true, <br/> some packets were dropped by sender                    |
[`SRT_EPEERERR`](#srt_epeererr)                     | Receiver peer is writing to a file that the agent is sending                                                   |
| <img width=290px height=1px/>                     | <img width=720px height=1px/>                                                                                  |



## Library initialization

* [srt_startup](#srt_startup)
* [srt_cleanup](#srt_cleanup)


### srt_startup
```
int srt_startup(void);
```

This function shall be called at the start of an application that uses the SRT
library. It provides all necessary platform-specific initializations, sets up
global data, and starts the SRT GC thread. If this function isn't explicitly 
called, it will be called automatically when creating the first socket. However, 
relying on this behavior is strongly discouraged.

|      Returns                  |                                                                 |
|:----------------------------- |:--------------------------------------------------------------- |
|         0                     | Successfully run, or already started                            |
|         1                     | This is the first startup, but the GC thread is already running |
|        -1                     | Failed                                                          |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                  |                                                                 |
|:----------------------------- |:--------------------------------------------------------------- |
| [`SRT_ECONNSETUP`](#srt_econnsetup) | With error code set, reported when required system resource(s) failed to initialize. <br/> This is currently used only on Windows to report a failure from `WSAStartup`. |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_cleanup
```
int srt_cleanup(void);
```

This function cleans up all global SRT resources and shall be called just before 
exiting the application that uses the SRT library. This cleanup function will still
be called from the C++ global destructor, if not called by the application, although
relying on this behavior is strongly discouraged.

|      Returns                  |                                                                 |
|:----------------------------- |:--------------------------------------------------------------- |
|         0                     | A possibility to return other values is reserved for future use |
| <img width=240px height=1px/>       | <img width=710px height=1px/>                      |

**IMPORTANT**: Note that the startup/cleanup calls have an instance counter.
This means that if you call [`srt_startup`](#srt_startup) multiple times, you need to call the 
`srt_cleanup` function exactly the same number of times.


## Creating and configuring sockets

* [srt_socket](#srt_socket)
* [srt_create_socket](#srt_create_socket)
* [srt_bind](#srt_bind)
* [srt_bind_acquire](#srt_bind_acquire)
* [srt_getsockstate](#srt_getsockstate)
* [srt_getsndbuffer](#srt_getsndbuffer)
* [srt_close](#srt_close)


### srt_socket
```
SRTSOCKET srt_socket(int af, int type, int protocol);
```

Old and deprecated version of [`srt_create_socket`](#srt_create_socket). All arguments are ignored.

**NOTE** changes with respect to UDT version:

* In UDT (and SRT versions before 1.4.2) the `af` parameter was specifying the
socket family (`AF_INET` or `AF_INET6`). This is now not required; this parameter
is decided at the call of [`srt_connect`](#srt_connect) or [`srt_bind`](#srt_bind).

* In UDT the `type` parameter was used to specify the file or message mode
using `SOCK_STREAM` or `SOCK_DGRAM` symbols (with the latter being misleading,
as the message mode has nothing to do with UDP datagrams and it's rather
similar to the SCTP protocol). In SRT these two modes are available by setting
[`SRTO_TRANSTYPE`](../docs/APISocketOptions.md#SRTO_TRANSTYPE). The default is `SRTT_LIVE`. If, however, you set
[`SRTO_TRANSTYPE`](../docs/APISocketOptions.md#SRTO_TRANSTYPE) to `SRTT_FILE` for file mode, you can then leave the
[`SRTO_MESSAGEAPI`](../docs/APISocketOptions.md#SRTO_MESSAGEAPI) option as false (default), which corresponds to "stream" mode
(TCP-like), or set it to true, which corresponds to "message" mode (SCTP-like).


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_create_socket
```
SRTSOCKET srt_create_socket();
```

Creates an SRT socket.

Note that socket IDs always have the `SRTGROUP_MASK` bit clear.

|       Returns                 |                                                         |
|:----------------------------- |:------------------------------------------------------- |
|      Socket ID                | A valid socket ID on success                            |
| `SRT_INVALID_SOCK`            | (`-1`) on error                                         |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|     Errors                    |                                                              |
|:----------------------------- |:------------------------------------------------------------ |
| [`SRT_ENOBUF`](#srt_enobuf)   |  Not enough memory to allocate required resources          . |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

**NOTE:** This is probably a design flaw (:warning: &nbsp; **BUG?**). Usually underlying system 
errors are reported by [`SRT_ECONNSETUP`](#srt_econnsetup).


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)


---  
  
### srt_bind
```
int srt_bind(SRTSOCKET u, const struct sockaddr* name, int namelen);
```

Binds a socket to a local address and port. Binding specifies the local network
interface and the UDP port number to be used for the socket. When the local 
address is a form of `INADDR_ANY`, then it's bound to all interfaces. When the 
port number is 0, then the port number will be system-allocated if necessary.

This call is obligatory for a listening socket before calling [`srt_listen`](#srt_listen)
and for rendezvous mode before calling [`srt_connect`](#srt_connect); otherwise it's 
optional. For a listening socket it defines the network interface and the port where 
the listener should expect a call request. In the case of rendezvous mode (when the
socket has set [`SRTO_RENDEZVOUS`](../docs/APISocketOptions.md#SRTO_RENDEZVOUS) to 
true both parties connect to one another) it defines the network interface and port 
from which packets will be sent to the peer, and the port to which the peer is 
expected to send packets.

For a connecting socket this call can set up the outgoing port to be used in the 
communication. It is allowed that multiple SRT sockets share one local outgoing 
port, as long as [`SRTO_REUSEADDR`](../docs/APISocketOptions.md#SRTO_REUSEADDRS) 
is set to *true* (default). Without this call the port will be automatically 
selected by the system.

**NOTE**: This function cannot be called on a socket group. If you need to
have the group-member socket bound to the specified source address before
connecting, use [`srt_connect_bind`](#srt_connect_bind) for that purpose.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
| `SRT_ERROR`                   | (-1) on error, otherwise 0                                |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                        |                                                                      |
|:----------------------------------- |:-------------------------------------------------------------------- |
| [`SRT_EINVSOCK`](#srt_einvsock)     | Socket passed as [`u`](#u) designates no valid socket                |
| [`SRT_EINVOP`](#srt_einvop)         | Socket already bound                                                 |
| [`SRT_ECONNSETUP`](#srt_econnsetup) | Internal creation of a UDP socket failed                             |
| [`SRT_ESOCKFAIL`](#srt_esockfail)   | Internal configuration of a UDP socket (`bind`, `setsockopt`) failed |
| <img width=240px height=1px/>       | <img width=710px height=1px/>                      |




[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_bind_acquire

```
int srt_bind_acquire(SRTSOCKET u, UDPSOCKET udpsock);
```

A version of [`srt_bind`](#srt_bind) that acquires a given UDP socket instead of creating one.



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_getsockstate

```
SRT_SOCKSTATUS srt_getsockstate(SRTSOCKET u);
```

Gets the current status of the socket. Possible states are:

|       State                                     | Description                                                       |
|:----------------------------------------------- |:----------------------------------------------------------------- |
| <a name="SRTS_INIT">`SRTS_INIT`</a>             | Created, but not bound.                                           |
| <a name="SRTS_OPENED">`SRTS_OPENED`</a>         | Created and bound, but not in use yet.                            |
| <a name="SRTS_LISTENING">`SRTS_LISTENING`</a>   | Socket is in listening state.                                     |
| <a name="SRTS_CONNECTING">`SRTS_CONNECTING`</a> | The connect operation was initiated, but not yet finished. This may also mean that it has timed out; <br/> you can only know that after getting a socket error report from [`srt_epoll_wait`](#srt_epoll_wait). In blocking mode <br/> it's not possible because [`srt_connect`](#srt_connect) does not return until the socket is connected or failed due <br/> to timeout or interrupted call.  |
| <a name="SRTS_CONNECTED">`SRTS_CONNECTED`</a>   | The socket is connected and ready for transmission.               |
| <a name="SRTS_BROKEN">`SRTS_BROKEN`</a>         | The socket was connected, but the connection was broken.          |
| <a name="SRTS_CLOSING">`SRTS_CLOSING`</a>       | The socket may still be open and active, but closing is requested, so no further operations will <br/> be accepted (active operations will be completed before closing) |
| <a name="SRTS_CLOSED">`SRTS_CLOSED`</a>         | The socket has been closed, but not yet removed by the GC thread. |
| <a name="SRTS_NONEXIST">`SRTS_NONEXIST`</a>     | The specified number does not correspond to a valid socket.       |
| <img width=240px height=1px/>       | <img width=710px height=1px/>                      |



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_getsndbuffer

```
int srt_getsndbuffer(SRTSOCKET sock, size_t* blocks, size_t* bytes);
```

Retrieves information about the sender buffer.

**Arguments**:

* `sock`: Socket to test
* `blocks`: Written information about buffer blocks in use
* `bytes`: Written information about bytes in use

This function can be used for diagnostics. It is especially useful when the 
socket needs to be closed asynchronously.



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_close

```
int srt_close(SRTSOCKET u);
```

Closes the socket or group and frees all used resources. Note that underlying
UDP sockets may be shared between sockets, so these are freed only with the
last user closed.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
| `SRT_ERROR`                   | (-1) in case of error, otherwise 0                        |
| <img width=240px height=1px/> | <img width=710px height=1px/>                             |

|       Errors                    |                                                 |
|:------------------------------- |:----------------------------------------------- |
| [`SRT_EINVSOCK`](#srt_einvsock) | Socket [`u`](#u) indicates no valid socket ID   |
| <img width=240px height=1px/>   | <img width=710px height=1px/>                   |



## Connecting

* [srt_listen](#srt_listen)
* [srt_accept](#srt_accept)
* [srt_accept_bond](#srt_accept_bond)
* [srt_listen_callback](#srt_listen_callback)
* [srt_connect](#srt_connect)
* [srt_connect_bind](#srt_connect_bind)
* [srt_connect_debug](#srt_connect_debug)
* [srt_rendezvous](#srt_rendezvous)
* [srt_connect_callback](#srt_connect_callback)

### srt_listen
```
int srt_listen(SRTSOCKET u, int backlog);
```

This sets up the listening state on a socket with a backlog setting that 
defines how many sockets may be allowed to wait until they are accepted 
(excessive connection requests are rejected in advance).

The following important options may change the behavior of the listener
socket and the [`srt_accept`](#srt_accept) function:

* [`srt_listen_callback`](#srt_listen_callback) installs a user function that will 
be called before [`srt_accept`](#srt_accept) can happen
* [`SRTO_GROUPCONNECT`](../docs/APISocketOptions.md#SRTO_GROUPCONNECT) option allows 
the listener socket to accept group connections

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
| `SRT_ERROR`                   | (-1) in case of error, otherwise 0.                       |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                            |                                                                                              |
|:--------------------------------------- |:-------------------------------------------------------------------------------------------- |
| [`SRT_EINVPARAM`](#srt_einvparam)       | Value of `backlog` is 0 or negative.                                                         |
| [`SRT_EINVSOCK`](#srt_einvsock)         | Socket [`u`](#u) indicates no valid SRT socket.                                              |
| [`SRT_EUNBOUNDSOCK`](#srt_eunboundsock) | [`srt_bind`](#srt_bind) has not yet been called on that socket.                              |
| [`SRT_ERDVNOSERV`](#srt_erdvnoserv)     | [`SRTO_RENDEZVOUS`](../docs/APISocketOptions.md#SRTO_RENDEZVOUS) flag is set to true on specified socket. |
| [`SRT_EINVOP`](#srt_einvop)             | Internal error (should not happen when [`SRT_EUNBOUNDSOCK`](#srt_eunboundsock) is reported). |
| [`SRT_ECONNSOCK`](#srt_econnsock)       | The socket is already connected.                                                             |
| [`SRT_EDUPLISTEN`](#srt_eduplisten)     | The address used in [`srt_bind`](#srt_bind) by this socket is already occupied by another listening socket. <br/> Binding multiple sockets to one IP address and port is allowed, as long as <br/> [`SRTO_REUSEADDR`](../docs/APISocketOptions.md#SRTO_REUSEADDRS) is set to true, but only one of these sockets can be set up as a listener.  |
| <img width=240px height=1px/>           | <img width=710px height=1px/>                      |



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_accept

```
SRTSOCKET srt_accept(SRTSOCKET lsn, struct sockaddr* addr, int* addrlen);
```

Accepts a pending connection, then creates and returns a new socket or
group ID that handles this connection. The group and socket can be
distinguished by checking the `SRTGROUP_MASK` bit on the returned ID.

* `lsn`: the listener socket previously configured by [`srt_listen`](#srt_listen)
* `addr`: the IP address and port specification for the remote party
* `addrlen`: INPUT: size of `addr` pointed object. OUTPUT: real size of the
returned object

**NOTE:** `addr` is allowed to be NULL, in which case it's understood that the
application is not interested in the address from which the connection originated.
Otherwise `addr` should specify an object into which the address will be written, 
and `addrlen` must also specify a variable to contain the object size. Note also
that in the case of group connection only the initial connection that
establishes the group connection is returned, together with its address. As
member connections are added or broken within the group, you can obtain this
information through [`srt_group_data`](#srt_group_data) or the data filled by 
[`srt_sendmsg2`](#srt_sendmsg) and [`srt_recvmsg2`](#srt_recvmsg2).

If the pending connection is a group connection (initiated on the peer side by 
calling the connection function using a group ID, and permitted on the listener 
socket by the [`SRTO_GROUPCONNECT`](../docs/APISocketOptions.md#SRTO_GROUPCONNECT) 
flag), then the value returned is a group ID. This function then creates a new 
group, as well as a new socket for this connection, that will be added to the 
group. Once the group is created this way, further connections within the same 
group, as well as sockets for them, will be created in the background. The 
[`SRT_EPOLL_UPDATE`](#SRT_EPOLL_UPDATE) event is raised on the `lsn` socket when 
a new background connection is attached to the group, although it's usually for 
internal use only.

|      Returns                  |                                                                         |
|:----------------------------- |:----------------------------------------------------------------------- |
| socket/group ID               | On success, a valid SRT socket or group ID to be used for transmission. |
| `SRT_ERROR`                   | (-1) on failure                                                         |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                      |                                                                         |
|:--------------------------------- |:----------------------------------------------------------------------- |
| [`SRT_EINVPARAM`](#srt_einvparam) | NULL specified as `addrlen`, when `addr` is not NULL  |
| [`SRT_EINVSOCK`](#srt_einvsock)   | `lsn` designates no valid socket ID.                   |
| [`SRT_ENOLISTEN`](#srt_enolisten) | `lsn` is not set up as a listener ([`srt_listen`](#srt_listen) not called). |
| [`SRT_EASYNCRCV`](#srt_easyncrcv) | No connection reported so far. This error is reported only when the `lsn` listener socket was <br/> configured as non-blocking for reading ([`SRTO_RCVSYN`](../docs/APISocketOptions.md#SRTO_RCVSYN) set to false); otherwise the call blocks <br/> until a connection is reported or an error occurs    |
| [`SRT_ESCLOSED`](#srt_esclosed)   | The `lsn` socket has been closed while the function was blocking the call (if [`SRTO_RCVSYN`](../docs/APISocketOptions.md#SRTO_RCVSYN) <br/> is set to default true). This includes a situation when the socket was closed just at the <br/> moment when a connection was made and the socket got closed during processing   |
| <img width=240px height=1px/>     | <img width=710px height=1px/>                      |



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_accept_bond

```
SRTSOCKET srt_accept_bond(const SRTSOCKET listeners[], int nlisteners, int msTimeOut);
```

Accepts a pending connection, like [`srt_accept`](#srt_accept), but pending on any of the
listener sockets passed in the `listeners` array of `nlisteners` size.

**Arguments**:

* `listeners`: array of listener sockets (all must be setup by [`srt_listen`](#srt_listen))
* `nlisteners`: size of the `listeners` array
* `msTimeOut`: timeout in [ms] or -1 to block forever

This function is for blocking mode only - for non-blocking mode you should simply 
call [`srt_accept`](#srt_accept) on the first listener socket that reports readiness,
and this function is actually a friendly shortcut that uses waiting on epoll and 
[`srt_accept`](#srt_accept) internally. This function supports an important use 
case for accepting a group connection, for which every member connection is expected 
to be established over a different listener socket.

Note that there's no special set of settings required or rejected for this function. 
Group-member connections for the same group can always be established over various 
different listener sockets when all those listeners are hosted by the same application. 
The group management is global for the application, so a connection reporting in for 
an already connected group gets discovered, and the connection will be handled in the 
background. This occurs regardless of which listener socket the call was made to, 
as long as the connection is accepted according to any additional conditions.

This function has nothing to do with the groups. You can use it in any case when 
you have one service that accepts connections to multiple endpoints. Note also 
that the settings as to whether listeners should accept or reject socket or group 
connections should be applied to the listener sockets appropriately prior to 
calling this function.

|      Returns                  |                                                                        |
|:----------------------------- |:---------------------------------------------------------------------- |
| SRT socket<br/>group ID       | On success, a valid SRT socket or group ID to be used for transmission |
| `SRT_ERROR`      | (-1) on failure                                                        |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                      |                                                              |
|:--------------------------------- |:------------------------------------------------------------ |
| [`SRT_EINVPARAM`](#srt_einvparam) | NULL specified as `listeners` or `nlisteners` < 1            |
| [`SRT_EINVSOCK`](#srt_einvsock)   | Any socket in `listeners` designates no valid socket ID. Can also mean *Internal Error* when <br/> an error occurred while creating an accepted socket (:warning: &nbsp; **BUG?**) |
| [`SRT_ENOLISTEN`](#srt_enolisten) | Any socket in `listeners` is not set up as a listener ([`srt_listen`](#srt_listen) not called, or the listener socket <br/> has already been closed)  |
| [`SRT_EASYNCRCV`](#srt_easyncrcv) | No connection reported on any listener socket as the timeout has been reached. This error is only <br/> reported when `msTimeOut` is not -1  |
| <img width=240px height=1px/>     | <img width=710px height=1px/>                      |



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_listen_callback

```
int srt_listen_callback(SRTSOCKET lsn, srt_listen_callback_fn* hook_fn, void* hook_opaque);
```

This call installs a callback hook, which will be executed on a socket that is
automatically created to handle the incoming connection on the listening socket 
(and is about to be returned by [`srt_accept`](#srt_accept)), but before the 
connection has been accepted.

**Arguments**:

* `lsn`: Listening socket where you want to install the callback hook
* `hook_fn`: The callback hook function pointer
* `hook_opaque`: The pointer value that will be passed to the callback function

|      Returns                  |                                                            |
|:----------------------------- |:---------------------------------------------------------- |
|         0                     | Successful                                                 |
|        -1                     | Error                                                      |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                      |                                           |
|:--------------------------------- |:----------------------------------------- |
| [`SRT_EINVPARAM`](#srt_einvparam) | Reported when `hook_fn` is a null pointer |
| <img width=240px height=1px/>     | <img width=710px height=1px/>                      |

The callback function has the signature as per this type definition:
```
typedef int srt_listen_callback_fn(void* opaque, SRTSOCKET ns, int hs_version
             const struct sockaddr* peeraddr, const char* streamid);
```

The callback function gets the following parameters passed:

* `opaque`: The pointer passed as `hook_opaque` when registering
* `ns`: The freshly created socket to handle the incoming connection
* `hs_version`: The handshake version (usually 5, pre-1.3 versions of SRT use 4)
* `peeraddr`: The address of the incoming connection
* `streamid`: The value set to [`SRTO_STREAMID`](../docs/APISocketOptions.md#SRTO_STREAMID) option set on the peer side

Note that SRT versions that use handshake version 4 are incapable of using
any extensions, such as `streamid`. However they do support encryption.
Note also that the SRT version isn't extracted at this point. However you can
prevent connections with versions that are too old by using the
[`SRTO_MINVERSION`](../docs/APISocketOptions.md#SRTO_MINVERSION) option.

The callback function is given an opportunity to:

* use the passed information (`streamid` and peer address) to decide
  what to do with this connection
* alter any options on the socket, which could not be set properly
  beforehand on the listening socket to be derived by the accepted socket,
  and won't be allowed to be altered after the socket is returned by
  [`srt_accept`](#srt_accept)

Note that normally the returned socket has already set all derived options from 
the listener socket. The moment when this callback is called is when the conclusion 
handshake has been already received from the caller party, but not yet interpreted 
(the `streamid` field is extracted from it prematurely). When, for example, you set 
a passphrase on the socket at this point, the Key Material processing will happen 
against this passphrase, after the callback function is finished.

The callback function shall return 0, if the connection is to be accepted.
If you return -1, **or** if the function throws an exception, this will be
understood as a request to reject the incoming connection. In this case the
about-to-be-accepted socket will be silently deleted and [`srt_accept`](#srt_accept) 
will not report it. Note that in case of non-blocking mode the epoll bits for 
read-ready on the listener socket will not be set if the connection is rejected, 
including when rejected from this user function.

**IMPORTANT**: This function is called in the receiver worker thread, which
means that it must do its checks and operations as quickly as possible. Every 
delay you create in this function will burden the processing of the incoming data 
on the associated UDP socket. In the case of a listener socket this means the 
listener socket itself and every socket accepted off this listener socket. 
Avoid any extensive search operations. It is best to cache in memory whatever 
database you have to check against the data received in `streamid` or `peeraddr`.



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_connect

```
int srt_connect(SRTSOCKET u, const struct sockaddr* name, int namelen);
```

Connects a socket or a group to a remote party with a specified address and port.

**Arguments**:

* [`u`](#u): can be an SRT socket or SRT group, both freshly created and not yet
  used for any connection, except possibly [`srt_bind`](#srt_bind) on the socket
* `name`: specification of the remote address and port
* `namelen`: size of the object passed by `name`

**NOTES:**  
  
1. The socket used here may be bound from upside (or binding and connection can
be done in one function, [`srt_connect_bind`](#srt_connect_bind)) so that it uses 
a predefined network interface or local outgoing port. If not, it behaves as if 
it was bound to `INADDR_ANY` (which binds on all interfaces) and port 0 (which
makes the system assign the port automatically).
2. When [`u`](#u) is a group, then this call can be done multiple times, each time
for another member connection, and a new member SRT socket will be created
automatically for every call of this function.
3. If you want to connect a group to multiple links at once and use blocking
mode, you might want to use [`srt_connect_group`](#srt_connect_group) instead.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|    `SRT_ERROR`                | (-1) in case of error                                     |
|         0                     | In case when used for [`u`](#u) socket                    |
|     Socket ID                 | Created for connection for [`u`](#u) group                |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                          |                                                             |
|:------------------------------------- |:----------------------------------------------------------- |
| [`SRT_EINVSOCK`](#srt_einvsock)       | Socket [`u`](#u) indicates no valid socket ID               |
| [`SRT_ERDVUNBOUND`](#srt_erdvunbound) | Socket [`u`](#u) has set [`SRTO_RENDEZVOUS`](../docs/APISocketOptions.md#SRTO_RENDEZVOUS) to true, but [`srt_bind`](#srt_bind) hasn't yet been called on it. <br/> The [`srt_connect`](#srt_connect) function is also used to connect a rendezvous socket, but rendezvous sockets <br/> must be explicitly bound to a local interface prior to connecting. Non-rendezvous sockets (caller <br/> sockets) can be left without binding - the call to [`srt_connect`](#srt_connect) will bind them automatically. |
| [`SRT_ECONNSOCK`](#srt_econnsock)     | Socket [`u`](#u) is already connected                       |
| [`SRT_ECONNREJ`](#srt_econnrej)       | Connection has been rejected                                |
| [`SRT_ENOSERVER`](#srt_enoserver)     | Connection has been timed out (see [`SRTO_CONNTIMEO`](../docs/APISocketOptions.md#SRTO_CONNTIMEO)) |
| [`SRT_ESCLOSED`](#srt_esclosed)       | The socket [`u`](#u) has been closed while the function was blocking the call (if [`SRTO_RCVSYN`](../docs/APISocketOptions.md#SRTO_RCVSYN) is set to <br/> default true)   |
| <img width=240px height=1px/>         | <img width=710px height=1px/>                      |


When the [`SRT_ECONNREJ`](#srt_econnrej) error is reported, you can get the reason 
for a rejected connection from [`srt_getrejectreason`](#srt_getrejectreason). In 
non-blocking mode (when [`SRTO_RCVSYN`](../docs/APISocketOptions.md#SRTO_RCVSYN) 
is set to false), only [`SRT_EINVSOCK`](#srt_einvsock), [`SRT_ERDVUNBOUND`](#srt_erdvunbound) 
and [`SRT_ECONNSOCK`](#srt_econnsock) can be reported. In all other cases the function 
returns immediately with a success, and the only way to obtain the connecting status 
is through the epoll flag with [`SRT_EPOLL_ERR`](#SRT_EPOLL_ERR). In this case you can 
also call [`srt_getrejectreason`](#srt_getrejectreason) to get the detailed reason for 
the error, including connection timeout ([`SRT_REJ_TIMEOUT`](#SRT_REJ_TIMEOUT)).

Note that in case of failure the socket is in `SRTS_CONNECTING`, not in
`SRTS_BROKEN` state. After the failure was reported and you read any extra
information from the socket, the socket should be manually closed using
`srt_close` function.



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_connect_bind

```
int srt_connect_bind(SRTSOCKET u, const struct sockaddr* source,
                     const struct sockaddr* target, int len);
```

This function does the same as first [`srt_bind`](#srt_bind) then 
[`srt_connect`](#srt_connect), if called with [`u`](#u) being a socket. 
If [`u`](#u) is a group, then it will execute [`srt_bind`](#srt_bind)
first on the automatically created socket for the connection.

**Arguments**:

* [`u`](#u): Socket or group to connect
* `source`: Address to bind [`u`](#u) to
* `target`: Address to connect
* `len`: size of the original structure of `source` and `target`

|      Returns                  |                                                          |
|:----------------------------- |:-------------------------------------------------------- |
|    `SRT_ERROR`                | (-1) in case of error                                    |
|         0                     | In case when used for [`u`](#u) socket                   |
|    Socket ID                  | Created for connection for [`u`](#u) group               |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                          |                                                          |
|:------------------------------------- |:-------------------------------------------------------- |
| [`SRT_EINVSOCK`](#srt_einvsock)       | Socket passed as [`u`](#u) designates no valid socket    |
| [`SRT_EINVOP`](#srt_einvop)           | Socket already bound                                     |
| [`SRT_ECONNSETUP`](#srt_econnsetup)   | Internal creation of a UDP socket failed                 |
| [`SRT_ESOCKFAIL`](#srt_esockfail)     | Internal configuration of a UDP socket (`bind`, `setsockopt`) failed   |
| [`SRT_ERDVUNBOUND`](#srt_erdvunbound) | Internal error ([`srt_connect`](#srt_connect) should not report it after [`srt_bind`](#srt_bind) was called)   |
| [`SRT_ECONNSOCK`](#srt_econnsock)     | Socket [`u`](#u) is already connected                    |
| [`SRT_ECONNREJ`](#srt_econnrej)       | Connection has been rejected                             |
| <img width=240px height=1px/>         | <img width=710px height=1px/>                      |


**IMPORTANT**: It's not allowed to bind and connect the same socket to two
different families (that is, both `source` and `target` must be `AF_INET` or
`AF_INET6`), although you may mix links over IPv4 and IPv6 in one group.



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_connect_debug

```
int srt_connect_debug(SRTSOCKET u, const struct sockaddr* name, int namelen, int forced_isn);
```

This function is for developers only and can be used for testing. It does the 
same thing as [`srt_connect`](#srt_connect), with the exception that it allows 
specifying the Initial Sequence Number for data transmission. Normally this value 
is generated randomly.



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_rendezvous
```
int srt_rendezvous(SRTSOCKET u, const struct sockaddr* local_name, int local_namelen,
        const struct sockaddr* remote_name, int remote_namelen);
```
Performs a rendezvous connection. This is a shortcut for doing bind locally,
setting the [`SRTO_RENDEZVOUS`](../docs/APISocketOptions.md#SRTO_RENDEZVOUS) option 
to true, and doing [`srt_connect`](#srt_connect). 

**Arguments**:

* [`u`](#u): socket to connect
* `local_name`: specifies the local network interface and port to bind
* `remote_name`: specifies the remote party's IP address and port

|      Returns                  |                                                          |
|:----------------------------- |:-------------------------------------------------------- |
| `SRT_ERROR`                   | (-1) in case of error, otherwise 0                       |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                          |                                                          |
|:------------------------------------- |:-------------------------------------------------------- |
| [`SRT_EINVSOCK`](#srt_einvsock)       | Socket passed as [`u`](#u) designates no valid socket    |
| [`SRT_EINVOP`](#srt_einvop)           | Socket already bound                                     |
| [`SRT_ECONNSETUP`](#srt_econnsetup)   | Internal creation of a UDP socket failed                 |
| [`SRT_ESOCKFAIL`](#srt_esockfail)     | Internal configuration of a UDP socket (`bind`, `setsockopt`) failed    |
| [`SRT_ERDVUNBOUND`](#srt_erdvunbound) | Internal error ([`srt_connect`](#srt_connect) should not report it after [`srt_bind`](#srt_bind) was called)    |
| [`SRT_ECONNSOCK`](#srt_econnsock)     | Socket [`u`](#u) is already connected                    |
| [`SRT_ECONNREJ`](#srt_econnrej)       | Connection has been rejected                             |
| <img width=240px height=1px/>         | <img width=710px height=1px/>                      |

**IMPORTANT**: Establishing a rendezvous connection to two different families is not 
allowed (that is, both `local_name` and `remote_name` must be `AF_INET` or `AF_INET6`).



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_connect_callback
```
int srt_connect_callback(SRTSOCKET u, srt_connect_callback_fn* hook_fn, void* hook_opaque);
```

This call installs a callback hook, which will be executed on a given [`u`](#u)
socket or all member sockets of a [`u`](#u) group, just after a pending connection 
in the background has been resolved and the connection has failed. Note that this 
function is not guaranteed to be called if the [`u`](#u) socket is set to blocking 
mode ([`SRTO_RCVSYN`](../docs/APISocketOptions.md#SRTO_RCVSYN) option set to true). 
It is guaranteed to be called when a socket is in non-blocking mode, or when you 
use a group.

This function is mainly intended to be used with group connections. Note that even 
if you use a group connection in blocking mode, after the group is considered
connected the member connections still continue in background. Also, when some 
connections are still pending and others have failed, the blocking call for 
[`srt_connect_group`](#srt_connect_group) will not exit until at least one of 
them succeeds or all fail - in such a case those failures also happen only in 
the background, while the connecting function blocks until all connections are 
resolved. When all links fail, you will only get a general error code for the 
group. This mechanism allows you to get individual errors for particular member
connection failures.

You can also use this mechanism as an alternative method for a single-socket
connection in non-blocking mode to trigger an action when the connection
process is finished. It is recommended, however, that you use this callback
only to collect failure information, as the call will happen in one of the
internal SRT threads.

**Arguments**:

* [`u`](#u): Socket or group that will be used for connecting and for which the hook is installed
* `hook_fn`: The callback hook function pointer
* `hook_opaque`: The pointer value that will be passed to the callback function


|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|         0                     | Successful                                                |
|        -1                     | Error                                                     |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                       |                                           |
|:---------------------------------- |:------------------------------------------|
| [`SRT_EINVPARAM`](#srt_einvparam)  | Reported when `hook_fn` is a null pointer |
| <img width=240px height=1px/>      | <img width=710px height=1px/>                      |


The callback function signature has the following type definition:

```
typedef void srt_connect_callback_fn(void* opaq, SRTSOCKET ns, int errorcode, const struct sockaddr* peeraddr, int token);
```

**Arguments**:

* `opaq`: The pointer passed as `hook_opaque` when registering
* `ns`: The socket for which the connection process was resolved
* [`errorcode`](#error-codes): The error code, same as for [`srt_connect`](#srt_connect) for blocking mode
* `peeraddr`: The target address passed to [`srt_connect`](#srt_connect) call
* `token`: The token value, if it was used for group connection, otherwise -1


## Socket group management

  * [SRT_GROUP_TYPE](#SRT_GROUP_TYPE)
  * [SRT_SOCKGROUPCONFIG](#SRT_SOCKGROUPCONFIG)
  * [SRT_SOCKGROUPDATA](#SRT_SOCKGROUPDATA)
  * [SRT_MEMBERSTATUS](#SRT_MEMBERSTATUS)
  
[Functions to be used on groups](#functions-to-be-used-on-groups):

  * [srt_create_group](#srt_create_group)
  * [srt_include](#srt_include)
  * [srt_exclude](#srt_exclude)
  * [srt_groupof](#srt_groupof)
  * [srt_group_data](#srt_group_data)
  * [srt_connect_group](#srt_connect_group)
  * [srt_prepare_endpoint](#srt_prepare_endpoint)
  * [srt_create_config](#srt_create_config)
  * [srt_delete_config](#srt_delete_config)
  * [srt_config_add](#srt_config_add)


  
### SRT_GROUP_TYPE

The following group types are collected in an [`SRT_GROUP_TYPE`](#SRT_GROUP_TYPE) enum:

* `SRT_GTYPE_BROADCAST`: broadcast type, all links are actively used at once
* `SRT_GTYPE_BACKUP`: backup type, idle links take over connection on disturbance
* `SRT_GTYPE_BALANCING`: balancing type, share bandwidth usage between links


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### SRT_SOCKGROUPCONFIG

This structure is used to define entry points for connections for the
[`srt_connect_group`](#srt_connect_group) function:

```
typedef struct SRT_GroupMemberConfig_
{
    SRTSOCKET id;
    struct sockaddr_storage srcaddr;
    struct sockaddr_storage peeraddr;
    int weight;
    SRT_SOCKOPT_CONFIG* config;
    int errorcode;
    int token;
} SRT_SOCKGROUPCONFIG;
```

where:

* `id`: member socket ID (filled back as output)
* `srcaddr`: address to which `id` should be bound
* `peeraddr`: address to which `id` should be connected
* `weight`: the weight parameter for the link (group-type dependent)
* `config`: the configuration object, if used (see [`srt_create_config()`](#srt_create_config))
* [`errorcode`](#error-codes): status of the connecting operation
* `token`: An integer value unique for every connection, or -1 if unused

The `srt_prepare_endpoint` sets these fields to default values. After that
you can change the value of `weight` and `config` and `token` fields. The
`weight` parameter's meaning is dependent on the group type:

* BROADCAST: not used
* BACKUP: positive value of link priority (the greater, the more preferred)
* BALANCING: relative expected load on this link for fixed algorithm

In any case, the allowed value for `weight` is between 0 and 32767.

The `config` parameter is used to provide options to be set separately on a socket 
for a particular connection (see [`srt_create_config()`](#srt_create_config)).

The `token` value is intended to allow the application to more easily identify
a particular connection. If you don't use it and leave the default value of -1,
the library will set a unique value for the next connection (a 32-bit unsigned
number that will overflow by itself; the default value will be skipped).
The application can also set a unique value by itself and keep the same
value for the same connection.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### SRT_SOCKGROUPDATA

The most important structure for the group member status is [`SRT_SOCKGROUPDATA`](#SRT_SOCKGROUPDATA):

```c++
typedef struct SRT_SocketGroupData_
{
    SRTSOCKET id;
    struct sockaddr_storage peeraddr;
    SRT_SOCKSTATUS sockstate;
    uint16_t weight;
    SRT_MEMBERSTATUS memberstate;
    int result;
    int token;
} SRT_SOCKGROUPDATA;
```

where:

* `id`: member socket ID
* `peeraddr`: address to which `id` should be connected
* `sockstate`: current connection status (see [`srt_getsockstate`](#srt_getsockstate)
* `weight`: current weight value set on the link
* `memberstate`: current state of the member (see below)
* `result`: result of the operation (if this operation recently updated this structure)
* `token`: A token value set for that connection (see [`SRT_SOCKGROUPCONFIG`](#SRT_SOCKGROUPCONFIG))


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### SRT_MEMBERSTATUS

The enumeration type that defines the state of a member
connection in a group:

* `SRT_GST_PENDING`: The connection is in progress, so the socket
is not currently being used for transmission, even potentially,
and still has a chance to fail and transit into `SRT_GST_BROKEN`
without turning into `SRT_GST_IDLE`

* `SRT_GST_IDLE`: The connection is established and ready to
take over transmission, but it's not used for transmission at
the moment. This state may last for a short moment in the case of
broadcast or balancing groups. In backup groups this state
defines a backup link that is ready to take over when the
currently active (running) link becomes unstable.

* `SRT_GST_RUNNING`: The connection is established and at least
one packet has already been sent or received over it.

* `SRT_GST_BROKEN`: The connection was broken. Broken connections are not to be 
revived. Note also that it is only possible to see this state if it is read by 
[`srt_sendmsg2`](#srt_sendmsg) or [`srt_recvmsg2`](#srt_recvmsg2) just after
the link failure has been detected. Otherwise, the broken link simply 
disappears from the member list.

Note that internally the member state is separate for sending and receiving. If 
the `memberstate` field of [`SRT_SOCKGROUPDATA`](#SRT_SOCKGROUPDATA) is
`SRT_GST_RUNNING`, it means that this is the state in at least one
direction, while in the other direction it may be `SRT_GST_IDLE`. In all
other cases the states should be the same in both directions.

States should normally start with `SRT_GST_PENDING` and then turn into 
`SRT_GST_IDLE`. Once a new link is used for sending data, the state becomes 
`SRT_GST_RUNNING`. In the case of the `SRT_GTYPE_BACKUP` type group, if a link 
is in the `SRT_GST_RUNNING` state, but another link is chosen to remain
as the only active one, this link will be "silenced" (its state will
become `SRT_GST_IDLE`).




## Functions to be used on groups:

### srt_create_group

```
SRTSOCKET srt_create_group(SRT_GROUP_TYPE type);
```

Creates a new group of type `type`. This is typically called on the
caller side to be next used for connecting to the listener peer side.
The group ID is of the same domain as the socket ID, with the exception that
the `SRTGROUP_MASK` bit is set on it, unlike for socket ID.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_include

```
int srt_include(SRTSOCKET socket, SRTSOCKET group);
```

This function adds a socket to a group. This is only allowed for unmanaged
groups. No such group type is currently implemented.



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_exclude

```
int srt_exclude(SRTSOCKET socket);
```
This function removes a socket from a group to which it currently belongs.
This is only allowed for unmanaged groups. No such group type is currently
implemented.



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_groupof

```
SRTSOCKET srt_groupof(SRTSOCKET socket);
```

Returns the group ID of the socket, or `SRT_INVALID_SOCK` if the socket
doesn't exist or it's not a member of any group.



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_group_data 

```
int srt_group_data(SRTSOCKET socketgroup, SRT_SOCKGROUPDATA output[], size_t* inoutlen);
```

**Arguments**:

* `socketgroup` an existing socket group ID
* `output` points to an output array
* `inoutlen` points to a variable that stores the size of the `output` array,
  and is set to the filled array's size

This function obtains the current member state of the group specified in
`socketgroup`. The `output` should point to an array large enough to hold all 
the elements. The `inoutlen` should point to a variable initially set to the size 
of the `output` array. The current number of members will be written back to `inoutlen`.

If the size of the `output` array is enough for the current number of members,
the `output` array will be filled with group data and the function will return
the number of elements filled. Otherwise the array will not be filled and 
`SRT_ERROR` will be returned.

This function can be used to get the group size by setting `output` to `NULL`,
and providing `socketgroup` and `inoutlen`.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|   # of elements               | The number of data elements filled, on success            |
|         -1                    | Error                                                     |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|      Errors                        |                                                           |
|:---------------------------------- |:--------------------------------------------------------- |
| [`SRT_EINVPARAM`](#srt_einvparam)  | Reported if `socketgroup` is not an existing group ID     |
| [`SRT_ELARGEMSG`](#srt_elargemsg)  | Reported if `inoutlen` if less than the size of the group |
| <img width=240px height=1px/>      | <img width=710px height=1px/>                      |

   
   

| in:output | in:inoutlen    | returns      | out:output | out:inoutlen | Error                             |
|:---------:|:--------------:|:------------:|:----------:|:------------:|:---------------------------------:|
| NULL      | NULL           | -1           | NULL       | NULL         | [`SRT_EINVPARAM`](#srt_einvparam) |
| NULL      | ptr            | 0            | NULL       | group.size() | ✖️                                |
| ptr       | NULL           | -1           | ✖️         | NULL         | [`SRT_EINVPARAM`](#srt_einvparam) |
| ptr       | ≥ group.size   | group.size() | group.data | group.size   | ✖️                                |
| ptr       | < group.size   | -1           | ✖️         | group.size   | [`SRT_ELARGEMSG`](#srt_elargemsg) |



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_connect_group

```
int srt_connect_group(SRTSOCKET group,
                      SRT_SOCKGROUPCONFIG name [], int arraysize);
```

This function does almost the same as calling [`srt_connect`](#srt_connect) or 
[`srt_connect_bind`](#srt_connect_bind) (when the source was specified for 
[`srt_prepare_endpoint`](#srt_prepare_endpoint)) in a loop for every item specified 
in `name` array. However if you did this in blocking mode, the first call to 
[`srt_connect`](#srt_connect) would block until the connection is established, 
whereas this function blocks until any of the specified connections is established.

If you set the group nonblocking mode ([`SRTO_RCVSYN`](../docs/APISocketOptions.md#SRTO_RCVSYN) 
option), there's no difference, except that the [`SRT_SOCKGROUPCONFIG`](#SRT_SOCKGROUPCONFIG) 
structure allows you to add extra configuration data used by groups. Note also that 
this function accepts only groups, not sockets.

The elements of the `name` array need to be prepared with the use of the
[`srt_prepare_endpoint`](#srt_prepare_endpoint) function. Note that it is
**NOT** required that every target address you specify for it is of the same
family.

Return value and errors in this function are the same as in [`srt_connect`](#srt_connect),
although this function reports success when at least one connection has
succeeded. If none has succeeded, this function reports an [`SRT_ECONNLOST`](#srt_econnlost)
error. Particular connection states can be obtained from the `name`
array upon return from the [`errorcode`](#error-codes) field.

The fields of [`SRT_SOCKGROUPCONFIG`](#SRT_SOCKGROUPCONFIG) structure have the following meaning:

**Input**:

* `id`: unused, should be -1 (default when created by [`srt_prepare_endpoint`](#srt_prepare_endpoint))
* `srcaddr`: address to bind before connecting, if specified (see below for details)
* `peeraddr`: target address to connect
* `weight`: weight value to be set on the link
* `config`: socket options to be set on the socket before connecting
* [`errorcode`](#error-codes): unused, should be [`SRT_SUCCESS`](#srt_success) (default)
* `token`: An integer value unique for every connection, or -1 if unused

**Output**:

* `id`: The socket created for that connection (-1 if failed to create)
* `srcaddr`: unchanged
* `peeraddr`: unchanged
* `weight`: unchanged
* `config`: unchanged (the object should be manually deleted upon return)
* [`errorcode`](#error-codes): status of connection for that link ([`SRT_SUCCESS`](#srt_success) if succeeded)
* `token`: same as in input, or a newly created token value if input was -1

The procedure of connecting for every connection definition specified
in the `name` array is performed the following way:

1. The socket for this connection is first created

2. Socket options derived from the group are set on that socket.

3. If `config` is not NULL, configuration options stored there are set on that socket.

4. If source address is specified (that is `srcaddr` value is **not**
default empty, as described in [`SRT_SOCKGROUPCONFIG`](#SRT_SOCKGROUPCONFIG),
then the binding operation is done on the socket (see [`srt_bind`](#srt_bind)).

5. The socket is added to the group as a member.

6. The socket is connected to the target address, as specified
in the `peeraddr` field.

During this process there can be errors at any stage. There are two
possibilities as to what may happen in this case:

1. If creation of a new socket has failed, which may only happen due to
problems with system resources, then the whole loop is interrupted and no
further items in the array are processed. All sockets that got created until
then, and for which the connection attempt has at least successfully started,
remain group members, although the function will return immediately with an
error status (that is, without waiting for the first successful connection). If
your application wants to do any partial recovery from this situation, it can
only use the epoll mechanism to wait for readiness.

2. In any other case, if an error occurs at any stage of the above process, the
processing is interrupted for this very array item only, the socket used for it
is immediately closed, and the processing of the next elements continues. In the case
of a connection process, it also passes two stages - parameter check and the process
itself. Failure at the parameter check breaks this process, while if the check
passes, this item is considered correctly processed, even if the connection
attempt is going to fail later. If this function is called in blocking mode,
it then blocks until at least one connection reports success, or if all of them
fail. The status of connections that continue in the background after this function 
exits can then be checked by [`srt_group_data`](#srt_group_data).

As member socket connections are running in the background, for determining 
if a particular connection has succeeded or failed it is recommended
to use [`srt_connect_callback`](#srt_connect_callback). In this case the
`token` callback function parameter will be the same as the `token` value used
for the particular item in the `name` connection table.

The `token` value doesn't have any limitations except that the -1 value is
a "trap representation", that is, when set on input it will make the internals
define a unique value for the `token`. Your application can also set unique values,
in which case the `token` value will be preserved.




[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_prepare_endpoint

```
SRT_SOCKGROUPCONFIG srt_prepare_endpoint(const struct sockaddr* src /*nullable*/,
                                       const struct sockaddr* adr, int namelen);
```

This function prepares a default [`SRT_SOCKGROUPCONFIG`](#SRT_SOCKGROUPCONFIG) object as an element
of the array you can prepare for [`srt_connect_group`](#srt_connect_group) function, filled with
additional data:

**Arguments**:

* `src`: address to which the newly created socket should be bound
* `adr`: address to which the newly created socket should connect
* `namelen`: size of both `src` and `adr`

The following fields are set by this function:

* `id`: -1 (unused for input)
* `srcaddr`: default empty (see below) or copied from `src`
* `peeraddr`: copied from `adr`
* `weight`: 0
* `config`: `NULL`
* [`errorcode`](#error-codes): [`SRT_SUCCESS`](#srt_success)

The default empty `srcaddr` is set the following way:

* `ss_family` set to the same value as `adr->sa_family`
* empty address (`INADDR_ANY` for IPv4 and `in6addr_any` for IPv6)
* port number 0

If `src` is not NULL, then `srcaddr` is copied from `src`. Otherwise
it will remain as default empty.

The `adr` parameter is obligatory. If `src` parameter is not NULL,
then both `adr` and `src` must have the same value of `sa_family`.

Note though that this function has no possibility of reporting errors - these
would be reported only by [`srt_connect_group`](#srt_connect_group), separately 
for every individual connection, and the status can be obtained from
the [`errorcode`](#error-codes) field.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_create_config

```
SRT_SOCKOPT_CONFIG* srt_create_config();
```

Creates a dynamic object for specifying the socket options. You can add options 
to be set on the socket by [`srt_config_add`](#srt_config_add) and then mount this 
object into the `config` field in [`SRT_SOCKGROUPCONFIG`](#SRT_SOCKGROUPCONFIG)
object for that particular connection. After the object is no longer needed, you 
should delete it using [`srt_delete_config`](#srt_delete_config).

|      Returns                  |                                                                    |
|:----------------------------- |:------------------------------------------------------------------ |
|      Pointer                  | The pointer to the created object (memory allocation errors apply) |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_delete_config

```
void srt_delete_config(SRT_SOCKOPT_CONFIG* c);
```

Deletes the configuration object.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_config_add

```
int srt_config_add(SRT_SOCKOPT_CONFIG* c, SRT_SOCKOPT opt, void* val, int len);
```

Adds a configuration option to the configuration object.

Parameters have meanings similar to [`srt_setsockflag`](#srt_setsockflag). Note
that not every option is allowed to be set this way. However, the option (if allowed) 
isn't checked if it doesn't violate other preconditions. This will be checked when 
the option is being set on the socket, which may fail as a part of the connection 
process done by [`srt_connect_group`](#srt_connect_group).

This function should be used when this option must be set individually on a socket 
and differently for a particular link. If you need to set some option the same way 
on every socket, you should instead set this option on the whole group.

The following options are allowed to be set on the member socket:

* [`SRTO_SNDBUF`](../docs/APISocketOptions.md#SRTO_SNDBUF): Allows for larger sender buffer for slower links
* [`SRTO_RCVBUF`](../docs/APISocketOptions.md#SRTO_RCVBUF): Allows for larger receiver buffer for longer recovery
* [`SRTO_UDP_RCVBUF`](../docs/APISocketOptions.md#SRTO_UDP_RCVBUF): UDP receiver buffer, if this link has a big flight window
* [`SRTO_UDP_SNDBUF`](../docs/APISocketOptions.md#SRTO_UDP_SNDBUF): UDP sender buffer, if this link has a big flight window
* [`SRTO_SNDDROPDELAY`](../docs/APISocketOptions.md#SRTO_SNDDROPDELAY): When particular link tends to drop too eagerly
* [`SRTO_NAKREPORT`](../docs/APISocketOptions.md#SRTO_NAKREPORT): If you don't want NAKREPORT to work for this link
* [`SRTO_CONNTIMEO`](../docs/APISocketOptions.md#SRTO_CONNTIMEO): If you want to give more time to connect on this link
* [`SRTO_LOSSMAXTTL`](../docs/APISocketOptions.md#SRTO_LOSSMAXTTL): If this link tends to suffer from UDP reordering
* [`SRTO_PEERIDLETIMEO`](../docs/APISocketOptions.md#SRTO_PEERIDLETIMEO): If you want to be more tolerant for temporary outages
* [`SRTO_GROUPSTABTIMEO`](../docs/APISocketOptions.md#SRTO_GROUPSTABTIMEO): To set ACK jitter tolerance per individual link


|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|         0                     | Success                                                   |
|        -1                     | Failure                                                   |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                       |                                                                       |
|:---------------------------------- |:--------------------------------------------------------------------- |
| [`SRT_EINVPARAM`](#srt_einvparam)  | This option is not allowed to be set on a socket being a group member |
| <img width=240px height=1px/>      | <img width=710px height=1px/>                      |


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)



## Options and properties

* [srt_getpeername](#srt_getpeername)
* [srt_getsockname](#srt_getsockname)
* [srt_getsockopt, srt_getsockflag](#srt_getsockopt-srt_getsockflag)
* [srt_setsockopt, srt_setsockflag](#srt_setsockopt-srt_setsockflag)
* [srt_getversion](#srt_getversion)


**NOTE**: For more information, see [Getting and Setting Options](../docs/APISocketOptions.md#getting-and-setting-options)

  
### srt_getpeername
```
int srt_getpeername(SRTSOCKET u, struct sockaddr* name, int* namelen);
```

Retrieves the remote address to which the socket is connected.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
| `SRT_ERROR`                   | (-1) in case of error, otherwise 0                        |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                    |                                                                          |
|:------------------------------- |:------------------------------------------------------------------------ |
| [`SRT_EINVSOCK`](#srt_einvsock) | Socket [`u`](#u) indicates no valid socket ID                            |
| [`SRT_ENOCONN`](#srt_enoconn)   | Socket [`u`](#u) isn't connected, so there's no remote address to return |
| <img width=240px height=1px/>   | <img width=710px height=1px/>                      |


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_getsockname
```
int srt_getsockname(SRTSOCKET u, struct sockaddr* name, int* namelen);
```

Extracts the address to which the socket was bound. Although you should know 
the address(es) that you have used for binding yourself, this function can be 
useful for extracting the local outgoing port number when it was specified as 0 
with binding for system autoselection. With this function you can extract the 
port number after it has been autoselected.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
| `SRT_ERROR`                   | (-1) in case of error, otherwise 0                        |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                    |                                                |
|:------------------------------- |:---------------------------------------------- |
| [`SRT_EINVSOCK`](#srt_einvsock) | Socket [`u`](#u) indicates no valid socket ID  |
| [`SRT_ENOCONN`](#srt_enoconn)   | Socket [`u`](#u) isn't bound, so there's no local address to return <br/>(:warning: &nbsp; **BUG?** It should rather be [`SRT_EUNBOUNDSOCK`](#srt_eunboundsock))        |
| <img width=240px height=1px/>   | <img width=710px height=1px/>                      |


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_getsockopt
### srt_getsockflag
```
int srt_getsockopt(SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT opt, void* optval, int* optlen);
int srt_getsockflag(SRTSOCKET u, SRT_SOCKOPT opt, void* optval, int* optlen);
```

Gets the value of the given socket option (from a socket or a group). 

The first version ([`srt_getsockopt`](#srt_getsockopt)) respects the BSD socket 
API convention, although the "level" parameter is ignored. The second version 
([`srt_getsockflag`](#srt_getsockflag)) omits the "level" parameter completely.

Options correspond to various data types, so you need to know what data type is 
assigned to a particular option, and to pass a variable of the appropriate data 
type. Specifications are provided in the `apps/socketoptions.hpp` file at the 
`srt_options` object declaration.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
| `SRT_ERROR`                   | (-1) in case of error, otherwise 0                        |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                     |                                                |
|:-------------------------------- |:---------------------------------------------- |
| [`SRT_EINVSOCK`](#srt_einvsock)  | Socket [`u`](#u) indicates no valid socket ID  |
| [`SRT_EINVOP`](#srt_einvop)      | Option `opt` indicates no valid option         |
| <img width=240px height=1px/>    | <img width=710px height=1px/>                      |


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---
### srt_setsockopt
### srt_setsockflag

```
int srt_setsockopt(SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT opt, const void* optval, int optlen);
int srt_setsockflag(SRTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen);
```

Sets a value for a socket option in the socket or group. 

The first version ([`srt_setsockopt`](#srt_setsockopt)) respects the BSD socket 
API convention, although the "level" parameter is ignored. The second version 
([`srt_setsockflag`](#srt_setsockflag)) omits the "level" parameter completely.

Options correspond to various data types, so you need to know what data type is 
assigned to a particular option, and to pass a variable of the appropriate data 
type with the option value to be set.

Please note that some of the options can only be set on sockets or only on
groups, although most of the options can be set on the groups so that they
are then derived by the member sockets.

|      Returns                  |                                                 |
|:----------------------------- |:----------------------------------------------- |
| `SRT_ERROR`                   | (-1) in case of error, otherwise 0              |
| <img width=240px height=1px/> | <img width=710px height=1px/>                   |

|       Errors                    |                                               |
|:------------------------------- |:--------------------------------------------- |
| [`SRT_EINVSOCK`](#srt_einvsock) | Socket [`u`](#u) indicates no valid socket ID |
| [`SRT_EINVOP`](#srt_einvop)     | Option `opt` indicates no valid option        |
| <img width=240px height=1px/>   | <img width=710px height=1px/>                 |

**NOTE*: Various other errors may result from problems when setting a 
specific option (see option description for details).



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_getversion

```
uint32_t srt_getversion();
```

Get SRT version value. The version format in hex is 0xXXYYZZ for x.y.z in human 
readable form, where x = ("%d", (version>>16) & 0xff), etc.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
| SRT Version                   | Unsigned 32-bit integer                                   |
| <img width=240px height=1px/> | <img width=710px height=1px/>                             |




## Helper data types for transmission

  
### SRT_MSGCTRL

The [`SRT_MSGCTRL`](#SRT_MSGCTRL) structure:

```c++
typedef struct SRT_MsgCtrl_
{
   int flags;            // Left for future
   int msgttl;           // TTL for a message, default -1 (no TTL limitation)
   int inorder;          // Whether a message is allowed to supersede a partially lost one. Unused in stream and live mode.
   int boundary;         // 0:mid pkt, 1(01b):end of frame, 2(11b):complete frame, 3(10b): start of frame
   int64_t srctime;      // source time (microseconds since SRT internal clock epoch)
   int32_t pktseq;       // sequence number of the first packet in received message (unused for sending)
   int32_t msgno;        // message number (output value for both sending and receiving)
   SRT_SOCKGROUPDATA* grpdata; // pointer to group data array
   size_t grpdata_size;  // size of the group array
} SRT_MSGCTRL;
```

The [`SRT_MSGCTRL`](#SRT_MSGCTRL) structure is used in [`srt_sendmsg2`](#srt_sendmsg) 
and [`srt_recvmsg2`](#srt_recvmsg2) calls and specifies some special extra parameters:

- `flags`: [IN, OUT]. RESERVED FOR FUTURE USE (should be 0). This is
intended to specify some special options controlling the details of how the
called function should work.

- `msgttl`: [IN]. In **message** and **live mode** only, specifies the TTL for 
sending messages (in `[ms]`). Not used for receiving messages. If this value
is not negative, it defines the maximum time up to which this message should
stay scheduled for sending for the sake of later retransmission. A message
is always sent for the first time, but the UDP packet carrying it may be
(also partially) lost, and if so, lacking packets will be retransmitted. If
the message is not successfully resent before TTL expires, further retransmission
is given up and the message is discarded.

- `inorder`: [IN]. In **message mode** only, specifies that sent messages should 
be extracted by the receiver in the order of sending. This can be meaningful if 
a packet loss has happened, and a particular message must wait for retransmission 
so that it can be reassembled and then delivered. When this flag is false, the 
message can be delivered even if there are any previous messages still waiting 
for completion.

- `boundary`: RESERVED FOR FUTURE USE. Intended to be used in a special mode 
when you are allowed to send or retrieve a part of the message.

- `srctime`:
  - [OUT] Receiver only. Specifies the time when the packet was intended to be
delivered to the receiving application (in microseconds since SRT clock epoch).
  - [IN] Sender only. Specifies the application-provided timestamp to be associated
with the packet. If not provided (specified as 0), the current time of 
SRT internal clock is used.
  - For details on how to use `srctime` please refer to the (Time Access)[#time-access] section.

- `pktseq`: Receiver only. Reports the sequence number for the packet carrying
out the payload being returned. If the payload is carried out by more than one
UDP packet, only the sequence of the first one is reported. Note that in
**live mode** there's always one UDP packet per message.

- `msgno`: Message number that can be sent by both sender and receiver,
although it is required that this value remain monotonic in subsequent send calls. 
Normally message numbers start with 1 and increase with every message sent.

- `grpdata` and `grpdata_size`: Pointer and size of the group array. For single
socket connections these values should remain NULL and 0 respectively. When you
call [`srt_sendmsg2`](#srt_sendmsg) or [`srt_recvmsg2`](#srt_recvmsg2) function 
for a group, you should pass an array here so that you can retrieve the status of 
particular member sockets. If you pass an array that is too small, your `grpdata_size` 
field will be rewritten with the current number of members, but without filling in 
the array. For details, see the (Bonding introduction)[bonding-intro.md] and 
(Socket Groups)[socket-groups.md] documents.

**Helpers for [`SRT_MSGCTRL`](#SRT_MSGCTRL):**

```
void srt_msgctrl_init(SRT_MSGCTRL* mctrl);
const SRT_MSGCTRL srt_msgctrl_default;
```

Helpers for getting an object of [`SRT_MSGCTRL`](#SRT_MSGCTRL) type ready to use. 
The first is a function that fills the object with default values. The second is 
a constant object and can be used as a source for assignment. Note that you cannot 
pass this constant object into any of the API functions because they require it 
to be mutable, as they use some fields to output values.




## Transmission

* [srt_send, srt_sendmsg, srt_sendmsg2](#srt_send-srt_sendmsg-srt_sendmsg2)
* [srt_recv, srt_recvmsg, srt_recvmsg2](#srt_recv-srt_recvmsg-srt_recvmsg2)
* [srt_sendfile, srt_recvfile](#srt_sendfile-srt_recvfile)

  
### srt_send
### srt_sendmsg
### srt_sendmsg2

```
int srt_send(SRTSOCKET u, const char* buf, int len);
int srt_sendmsg(SRTSOCKET u, const char* buf, int len, int ttl/* = -1*/, int inorder/* = false*/);
int srt_sendmsg2(SRTSOCKET u, const char* buf, int len, SRT_MSGCTRL *mctrl);
```

Sends a payload to a remote party over a given socket.


**Arguments**:

* [`u`](#u): Socket used to send. The socket must be connected for this operation.
* `buf`: Points to the buffer containing the payload to send.
* `len`: Size of the payload specified in `buf`.
* `ttl`: Time (in `[ms]`) to wait for a successful delivery. See description of 
the [`SRT_MSGCTRL::msgttl`](#SRT_MSGCTRL) field.
* `inorder`: Required to be received in the order of sending. See 
[`SRT_MSGCTRL::inorder`](#SRT_MSGCTRL).
* `mctrl`: An object of [`SRT_MSGCTRL`](#SRT_MSGCTRL) type that contains extra 
parameters, including `ttl` and `inorder`.

The way this function works is determined by the mode set in options, and it has
specific requirements:

1. In **file/stream mode**, the payload is byte-based. You are not required to
know the size of the data, although they are only guaranteed to be received
in the same byte order.

2. In **file/message mode**, the payload that you send using this function is 
a single message that you intend to be received as a whole. In other words, a 
single call to this function determines a message's boundaries.

3. In **live mode**, you are only allowed to send up to the length of
`SRTO_PAYLOADSIZE`, which can't be larger than 1456 bytes (1316 default).

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|       Size                    | Size of the data sent, if successful                      |
|    `SRT_ERROR`                | In case of error (-1)                                     |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

**NOTE**: Note that in **file/stream mode** the returned size may be less than `len`, 
which means that it didn't send the whole contents of the buffer. You would need to 
call this function again with the rest of the buffer next time to send it completely. 
In both **file/message** and **live mode** the successful return is always equal to `len`.

|       Errors                                  |                                                                                                                     |
|:--------------------------------------------- |:------------------------------------------------------------------------------------------------------------------- |
| [`SRT_ENOCONN`](#srt_enoconn)                 | Socket [`u`](#u) used when the operation is not connected.                                                          |
| [`SRT_ECONNLOST`](#srt_econnlost)             | Socket [`u`](#u) used for the operation has lost its connection.                                                    |
| [`SRT_EINVALMSGAPI`](#srt_einvalmsgapi)       | Incorrect API usage in **message mode**:<br/>**live mode**: trying to send more bytes at once than `SRTO_PAYLOADSIZE` or wrong source time <br/>was provided. |
| [`SRT_EINVALBUFFERAPI`](#srt_einvalbufferapi) | Incorrect API usage in **stream mode** (reserved for future use):<br/>The congestion controller object used for this mode doesn't use any restrictions on this call, <br/>but this may change. |
| [`SRT_ELARGEMSG`](#srt_elargemsg)             | Message to be sent can't fit in the sending buffer (that is, it exceeds the current total space in the <br/>sending buffer in bytes). This means that the sender buffer is too small, or the application is <br/>trying to send a larger message than initially predicted.  |
| [`SRT_EASYNCSND`](#srt_easyncsnd)             | There's no free space currently in the buffer to schedule the payload. This is only reported in <br/>non-blocking mode ([`SRTO_SNDSYN`](../docs/APISocketOptions.md#SRTO_SNDSYN) set to false); in blocking mode the call is blocked until <br/>enough free space in the sending buffer becomes available.  |
| [`SRT_ETIMEOUT`](#srt_etimeout)               | The condition described above still persists and the timeout has passed. This is only reported in <br/>blocking mode when [`SRTO_SNDTIMEO`](../docs/APISocketOptions.md#SRTO_SNDTIMEO) is set to a value other than -1. |
| [`SRT_EPEERERR`](#srt_epeererr)               | This is reported only in the case where, as a stream is being received by a peer, the <br/>[`srt_recvfile`](#srt_recvfile) function encounters an error during a write operation on a file. This is reported by <br/>a `UMSG_PEERERROR` message from the peer, and the agent sets the appropriate flag internally. <br/>This flag persists up to the moment when the connection is broken or closed. |
| <img width=240px height=1px/>                 | <img width=710px height=1px/>                      |




[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_recv
### srt_recvmsg
### srt_recvmsg2


```
int srt_recv(SRTSOCKET u, char* buf, int len);
int srt_recvmsg(SRTSOCKET u, char* buf, int len);
int srt_recvmsg2(SRTSOCKET u, char *buf, int len, SRT_MSGCTRL *mctrl);
```

Extracts the payload waiting to be received. Note that [`srt_recv`](#srt_recv) and 
[`srt_recvmsg`](#srt_recvmsg) are identical functions, two different names being 
kept for historical reasons. In the UDT predecessor the application was required 
to use either the `UDT::recv` version for **stream mode** and `UDT::recvmsg` for 
**message mode**. In SRT this distinction is resolved internally by the 
[`SRTO_MESSAGEAPI`](../docs/APISocketOptions.md#SRTO_MESSAGEAPI) flag.

**Arguments**:

* [`u`](#u): Socket used to send. The socket must be connected for this operation.
* `buf`: Points to the buffer to which the payload is copied.
* `len`: Size of the payload specified in `buf`.
* `mctrl`: An object of [`SRT_MSGCTRL`](#SRT_MSGCTRL) type that contains extra 
parameters.

The way this function works is determined by the mode set in options, and it has 
specific requirements:

1. In **file/stream mode**, as many bytes as possible are retrieved, that is,
only so many bytes that fit in the buffer and are currently available. Any
data that is available but not extracted this time will be available next time.

2. In **file/message mode**, exactly one message is retrieved, with the 
boundaries defined at the moment of sending. If some parts of the messages are 
already retrieved, but not the whole message, nothing will be received (the 
function blocks or returns [`SRT_EASYNCRCV`](#srt_easyncrcv)). If the message 
to be returned does not fit in the buffer, nothing will be received and 
the error is reported.

3. In **live mode**, the function behaves as in **file/message mode**, although the 
number of bytes retrieved will be at most the size of `SRTO_PAYLOADSIZE`. In this mode, 
however, with default settings of [`SRTO_TSBPDMODE`](../docs/APISocketOptions.md#SRTO_TSBPDMODE) 
and [`SRTO_TLPKTDROP`](../docs/APISocketOptions.md#SRTO_TLPKTDROP), the message will be 
received only when its time to play has come, and until then it will be kept in the 
receiver buffer. Also, when the time to play has come for a message that is next to 
the currently lost one, it will be delivered and the lost one dropped.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|       Size                    | Size (\>0) of the data received, if successful.           |
|         0                     | If the connection has been closed                         |
|   `SRT_ERROR`                 | (-1) when an error occurs                                 |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                                  |                                                           |
|:--------------------------------------------- |:--------------------------------------------------------- |
| [`SRT_ENOCONN`](#srt_enoconn)                 | Socket [`u`](#u) used for the operation is not connected. |
| [`SRT_ECONNLOST`](#srt_econnlost)             | Socket [`u`](#u) used for the operation has lost connection (this is reported only if the connection <br/> was unexpectedly broken, not when it was closed by the foreign host). |
| [`SRT_EINVALMSGAPI`](#srt_einvalmsgapi)       | Incorrect API usage in **message mode**:<br/>-- **live mode**: size of the buffer is less than [`SRTO_PAYLOADSIZE`](../docs/APISocketOptions.md#SRTO_PAYLOADSIZE) |
| [`SRT_EINVALBUFFERAPI`](#srt_einvalbufferapi) | Incorrect API usage in **stream mode**:<br/>• Currently not in use. File congestion control used for **stream mode** does not restrict <br/> the parameters. :warning: &nbsp; **???**   |
| [`SRT_ELARGEMSG`](#srt_elargemsg)             | Message to be sent can't fit in the sending buffer (that is, it exceeds the current total space in <br/> the sending buffer in bytes). This means that the sender buffer is too small, or the application <br/> is trying to send a larger message than initially intended.  |
| [`SRT_EASYNCRCV`](#srt_easyncrcv)             | There are no data currently waiting for delivery. This happens only in non-blocking mode <br/> (when [`SRTO_RCVSYN`](../docs/APISocketOptions.md#SRTO_RCVSYN) is set to false). In blocking mode the call is blocked until the data are ready. <br/> How this is defined, depends on the mode:<br/>• In **live mode** (with [`SRTO_TSBPDMODE`](../docs/APISocketOptions.md#SRTO_TSBPDMODE) on), at least one packet must be present in the receiver <br/> buffer and its time to play be in the past<br/>• In **file/message mode**, one full message must be available, the next one waiting if there are no <br/> messages with `inorder` = false, or possibly the first message ready with `inorder` = false<br/>• In **file/stream mode**, it is expected to have at least one byte of data still not extracted  |
| [`SRT_ETIMEOUT`](#srt_etimeout)               | The readiness condition described above is still not achieved and the timeout has passed. <br/> This is only reported in blocking mode when[`SRTO_RCVTIMEO`](../docs/APISocketOptions.md#SRTO_RCVTIMEO) is set to a value other than -1. |
| <img width=240px height=1px/>                 | <img width=710px height=1px/>                      |



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_sendfile
### srt_recvfile

```
int64_t srt_sendfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block);
int64_t srt_recvfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block);
```

These are functions dedicated to sending and receiving a file. You need to call
this function just once for the whole file, although you need to know the size of
the file prior to sending, and also define the size of a single block that should
be internally retrieved and written into a file in a single step. This influences
only the performance of the internal operations; from the application perspective
you just have one call that exits only when the transmission is complete.

**Arguments**:

* [`u`](#u): Socket used for transmission. The socket must be connected.
* `path`: Path to the file that should be read or written.
* `offset`: Needed to pass or retrieve the offset used to read or write to a file
* `size`: Size of transfer (file size, if offset is at 0)
* `block`: Size of the single block to read at once before writing it to a file

The following values are recommended for the `block` parameter:

```
#define SRT_DEFAULT_SENDFILE_BLOCK 364000
#define SRT_DEFAULT_RECVFILE_BLOCK 7280000
```

You need to pass them to the [`srt_sendfile`](#srt_sendfile) or 
[`srt_recvfile`](#srt_recvfile) function if you don't know what value to chose.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|       Size                    | The size (\>0) of the transmitted data of a file. It may be less than `size`, if the size was greater <br/> than the free space in the buffer, in which case you have to send rest of the file next time.  |
|        -1                     | in case of error                                          |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                                  |                                                                               |
|:--------------------------------------------- |:----------------------------------------------------------------------------- |
| [`SRT_ENOCONN`](#srt_enoconn)                 | Socket [`u`](#u) used for the operation is not connected.                     |
| [`SRT_ECONNLOST`](#srt_econnlost)             | Socket [`u`](#u) used for the operation has lost its connection.              |
| [`SRT_EINVALBUFFERAPI`](#srt_einvalbufferapi) | When socket has [`SRTO_MESSAGEAPI`](../docs/APISocketOptions.md#SRTO_MESSAGEAPI) = true or [`SRTO_TSBPDMODE`](../docs/APISocketOptions.md#SRTO_TSBPDMODE) = true. <br/> (:warning: &nbsp; **BUG?**: Looxlike MESSAGEAPI isn't checked)   |
| [`SRT_EINVRDOFF`](#srt_einvrdoff)             | There is a mistake in `offset` or `size` parameters, which should match the index availability <br/> and size of the bytes available since `offset` index. This is actually reported for [`srt_sendfile`](#srt_sendfile) <br/> when the `seekg` or `tellg` operations resulted in error.  |
| [`SRT_EINVWROFF`](#srt_einvwroff)             | Like above, reported for [`srt_recvfile`](#srt_recvfile) and `seekp`/`tellp`. |
| [`SRT_ERDPERM`](#srt_erdperm)                 | The read from file operation has failed ([`srt_sendfile`](#srt_sendfile)).    |
| [`SRT_EWRPERM`](#srt_ewrperm)                 | The write to file operation has failed ([`srt_recvfile`](#srt_recvfile)).     |
| <img width=240px height=1px/>                 | <img width=710px height=1px/>                      |



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)



## Performance tracking

**Sequence Numbers** 
The sequence numbers used in SRT are 32-bit "circular numbers" with the most significant 
bit not included. For example 0x7FFFFFFF shifted forward by 3 becomes 2. As far as 
any comparison is concerned, it can be thought of as a "distance" which is an integer
value expressing an offset to be added to one sequence number in order to get the
second one. This distance is only valid as long as the threshold value isn't exceeded, 
so it's understood that all sequence numbers that are anywhere taken into account are 
systematically updated and kept in the range between 0 and half of the maximum 0x7FFFFFFF. 
Hence, the distance counting procedure always assumes that the sequence number are in 
the required range already, so for a numbers like 0x7FFFFFF0 and 0x10, for which the 
"numeric difference" would be 0x7FFFFFE0, the "distance" is 0x20.


* [srt_bstats, srt_bistats](#srt_bstats-srt_bistats)

  
### srt_bstats
### srt_bistats
```
// Performance monitor with Byte counters for better bitrate estimation.
int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear);

// Performance monitor with Byte counters and instantaneous stats instead of moving averages for Snd/Rcvbuffer sizes.
int srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous);
```

Reports the current statistics

**Arguments**:

* [`u`](#u): Socket from which to get statistics
* `perf`: Pointer to an object to be written with the statistics
* `clear`: 1 if the statistics should be cleared after retrieval
* `instantaneous`: 1 if the statistics should use instant data, not moving averages

`SRT_TRACEBSTATS` is an alias to `struct CBytePerfMon`. For a complete description
of the fields please refer to the document [statistics.md](statistics.md).



## Asynchronous operations (epoll)

* [srt_epoll_create](#srt_epoll_create)
* [srt_epoll_add_usock, srt_epoll_add_ssock, srt_epoll_update_usock, srt_epoll_update_ssock](#srt_epoll_add_usock-srt_epoll_add_ssock-srt_epoll_update_usock-srt_epoll_update_ssock)
* [srt_epoll_remove_usock, srt_epoll_remove_ssock](#srt_epoll_remove_usock-srt_epoll_remove_ssock)
* [srt_epoll_wait](#srt_epoll_wait)
* [srt_epoll_uwait](#srt_epoll_uwait)
* [srt_epoll_clear_usocks](#srt_epoll_clear_usocks)
* [srt_epoll_set](#srt_epoll_set)
* [srt_epoll_release](#srt_epoll_release)

The epoll system is currently the only method for using multiple sockets in one
thread with having the blocking operation moved to epoll waiting so that it can
block on multiple sockets at once. That is, instead of blocking a single reading
or writing operation, as it's in blocking mode, it blocks until at least one of
the sockets subscribed for a single waiting call in given operation mode is ready
to do this operation without blocking. It's usually combined with setting the
nonblocking mode on a socket. In SRT this is set separately for reading and
writing ([`SRTO_RCVSYN`](../docs/APISocketOptions.md#SRTO_RCVSYN) and 
[`SRTO_SNDSYN`](../docs/APISocketOptions.md#SRTO_SNDSYN) respectively). This is 
to ensure that if there is internal error in the application (or even possibly 
a bug in SRT that has reported a spurious readiness report) the operation will end 
up with an error rather than cause blocking, which would be more dangerous for the 
application ([`SRT_EASYNCRCV`](#srt_easyncrcv) and [`SRT_EASYNCSND`](#srt_easyncsnd) 
respectively).

The epoll system, similar to the one on Linux, relies on [`eid`](#eid) objects 
managed internally in SRT, which can be subscribed to particular sockets and the 
readiness status of particular operations. The [`srt_epoll_wait`](#srt_epoll_wait) 
function can then be used to block until any readiness status in the whole 
[`eid`](#eid) is set.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
### srt_epoll_create
```
int srt_epoll_create(void);
```

Creates a new epoll container.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|     valid EID                 | Success                                                   |
|        -1                     | Failure                                                   |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                        |                                                                       |
|:----------------------------------- |:--------------------------------------------------------------------- |
| [`SRT_ECONNSETUP`](#srt_econnsetup) | System operation failed or not enough space to create a new epoll. System error might happen on <br/> systems that use a special method for the system part of epoll (`epoll_create()`, `kqueue()`), <br/> and therefore associated resources, like epoll on Linux.   |
| <img width=240px height=1px/>       | <img width=710px height=1px/>                      |


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_epoll_add_usock
### srt_epoll_add_ssock
### srt_epoll_update_usock
### srt_epoll_update_ssock

```
int srt_epoll_add_usock(int eid, SRTSOCKET u, const int* events);
int srt_epoll_add_ssock(int eid, SYSSOCKET s, const int* events);
int srt_epoll_update_usock(int eid, SRTSOCKET u, const int* events);
int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int* events);
```

Adds a socket to a container, or updates an existing socket subscription.

The `_usock` suffix refers to a user socket (SRT socket). 
The `_ssock` suffix refers to a system socket.

The `_add_` functions add new sockets. The `_update_` functions act on a socket that 
is in the container already and just allow changes in the subscription details. For 
example, if you have already subscribed a socket with [`SRT_EPOLL_OUT`](#SRT_EPOLL_OUT) 
to wait until it's connected, to change it into poll for read-readiness, you use this 
function on that same socket with a variable set to [`SRT_EPOLL_IN`](#SRT_EPOLL_IN). 
This will not only change the event type which is polled on the socket, but also 
remove any readiness status for flags that are no longer set. It is discouraged
to perform socket removal and adding back (instead of using `_update_`) because
this way you may miss an event that could happen in the brief moment between
these two calls.

**Arguments**:

* <a name="eid">`eid`</a>: epoll container id
* <a name="u">`u`</a>: SRT socket
* <a name="s">`s`(#s)</a>: system socket
* <a name="eid">`events`</a>: points to
  * a variable set to epoll flags (see below) to use only selected events
  * NULL if you want to subscribe a socket for all events in level-triggered mode

Possible epoll flags are the following:

   * <a name="SRT_EPOLL_IN">`SRT_EPOLL_IN`</a>: report readiness for reading or incoming connection on a listener socket
   * <a name="SRT_EPOLL_OUT">`SRT_EPOLL_OUT`</a>: report readiness for writing or a successful connection
   * <a name="SRT_EPOLL_ERR">`SRT_EPOLL_ERR`</a>: report errors on the socket
   * <a name="SRT_EPOLL_UPDATE">`SRT_EPOLL_UPDATE`</a>: an important event has happened that requires attention
   * <a name="SRT_EPOLL_ET">`SRT_EPOLL_ET`</a>: the event will be edge-triggered

All flags except [`SRT_EPOLL_ET`](#SRT_EPOLL_ET) are event type flags (important for functions
that expect only event types and not other flags).

The [`SRT_EPOLL_IN`](#SRT_EPOLL_IN), [`SRT_EPOLL_OUT`](#SRT_EPOLL_OUT) and 
[`SRT_EPOLL_ERR`](#SRT_EPOLL_ERR) events are by default **level-triggered**. 
With [`SRT_EPOLL_ET`](#SRT_EPOLL_ET) flag they become **edge-triggered**.

The [`SRT_EPOLL_UPDATE`](#SRT_EPOLL_UPDATE) flag is always edge-triggered. It 
designates a special event that happens on a group, or on a listener socket that 
has the [`SRTO_GROUPCONNECT`](../docs/APISocketOptions.md#SRTO_GROUPCONNECT) flag 
set to allow group connections. This flag is triggered in the following situations:

* for group connections, when a new link has been established for a group that 
is already connected (that is, has at least one connection established), 
[`SRT_EPOLL_UPDATE`](#SRT_EPOLL_UPDATE) is reported for the listener socket 
accepting the connection. This is intended for internal use only. An initial 
connection results in reporting the group connection on that listener. But 
when the group is already connected, [`SRT_EPOLL_UPDATE`](#SRT_EPOLL_UPDATE) 
is reported instead.

* when one of a group's member connection has been broken

Note that at this time the edge-triggered mode is supported only for SRT
sockets, not for system sockets.

In the **edge-triggered** mode the function will only return socket states that
have changed since the last call of the waiting function. All events reported
in a particular call of the waiting function will be cleared in the internal
flags and will not be reported until the internal signalling logic clears this
state and raises it again.

In the **level-triggered** mode the function will always return the readiness
state as long as it lasts, until the internal signalling logic clears it.

Note that when you use [`SRT_EPOLL_ET`](#SRT_EPOLL_ET) flag in one subscription 
call, it defines edge-triggered mode for all events passed together with it. 
However, if you want to have some events reported as edge-triggered and others 
as level-triggered, you can do two separate subscriptions for the same socket.

**IMPORTANT**: The [`srt_epoll_wait`](#srt_epoll_wait) function does not report
[`SRT_EPOLL_UPDATE`](#SRT_EPOLL_UPDATE) events. If you need the ability to get 
any possible flag, you must use [`srt_epoll_wait`](#srt_epoll_wait). Note that 
this function doesn't work with system file descriptors.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|         0                     | Success                                                   |
|        -1                     | Failure                                                   |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                        |                                                                   |
|:----------------------------------- |:----------------------------------------------------------------- |
| [`SRT_EINVPOLLID`](#srt_einvpollid) | [`eid`](#eid) parameter doesn't refer to a valid epoll container  |
| <img width=240px height=1px/>       | <img width=710px height=1px/>                      |

:warning: &nbsp; **BUG?**: for `add_ssock` the system error results in an empty `CUDTException()`
call which actually results in [`SRT_SUCCESS`](#srt_success). For cases like that 
the [`SRT_ECONNSETUP`](#srt_econnsetup) code is predicted.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_epoll_remove_usock
### srt_epoll_remove_ssock

```
int srt_epoll_remove_usock(int eid, SRTSOCKET u);
int srt_epoll_remove_ssock(int eid, SYSSOCKET s);
```

Removes a specified socket from an epoll container and clears all readiness
states recorded for that socket.

The `_usock` suffix refers to a user socket (SRT socket). 
The `_ssock` suffix refers to a system socket.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|         0                     | Success                                                   |
|        -1                     | Failure                                                   |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                        |                                                                   |
|:----------------------------------- |:----------------------------------------------------------------- |
| [`SRT_EINVPOLLID`](#srt_einvpollid) | [`eid`](#eid) parameter doesn't refer to a valid epoll container  |
| <img width=240px height=1px/>       | <img width=710px height=1px/>                      |


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_epoll_wait
```
int srt_epoll_wait(int eid, SRTSOCKET* readfds, int* rnum, SRTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                        SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum);
```

Blocks the call until any readiness state occurs in the epoll container.

Readiness can be on a socket in the container for the event type as per
subscription. Note that in the case when a particular event was subscribed with
[`SRT_EPOLL_ET`](#SRT_EPOLL_ET) flag, this event, once reported in this function, 
will be cleared internally.

The first readiness state causes this function to exit, but all ready sockets
are reported. This function blocks until the timeout specified in the `msTimeOut`
parameter.  If timeout is 0, it exits immediately after checking. If timeout is
-1, it blocks indefinitely until a readiness state occurs.

**Arguments**:

* `eid`: epoll container
* `readfds` and `rnum`: A pointer and length of an array to write SRT sockets that are read-ready
* `writefds` and `wnum`: A pointer and length of an array to write SRT sockets that are write-ready
* `msTimeOut`: Timeout specified in milliseconds, or special values (0 or -1)
* `lwfds` and `lwnum`: A pointer and length of an array to write system sockets that are read-ready
* `lwfds` and `lwnum`: A pointer and length of an array to write system sockets that are write-ready

Note that the following flags are reported:

* [`SRT_EPOLL_IN`](#SRT_EPOLL_IN) as read-ready (also a listener socket ready to accept)
* [`SRT_EPOLL_OUT`](#SRT_EPOLL_OUT) as write-ready (also a connected socket)
* [`SRT_EPOLL_ERR`](#SRT_EPOLL_ERR) as both read-ready and write-ready
* [`SRT_EPOLL_UPDATE`](#SRT_EPOLL_UPDATE) is not reported

There is no space here to report sockets for which it's already known that the operation 
will end up with error (although such a state is known internally). If an error occurred 
on a socket then that socket is reported in both read-ready and write-ready arrays, 
regardless of what event types it was subscribed for. Usually then you subscribe the 
given socket for only read readiness, for example ([`SRT_EPOLL_IN`](#SRT_EPOLL_IN)), 
but pass both arrays for read and write readiness.This socket will not be reported 
in the write readiness array even if it's write ready (because this isn't what it 
was subscribed for), but it will be reported there, if the next operation on this 
socket is about to be erroneous. On such sockets you can still perform an operation, 
just you should expect that it will always report an error. On the other hand that's 
the only way to know what kind of error has occurred on the socket.

|      Returns                  |                                                              |
|:----------------------------- |:------------------------------------------------------------ |
|       Number                  | The number (\>0) of ready sockets, of whatever kind (if any) |
|         -1                    | Error                                                        |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                        |                                                                     |
|:----------------------------------- |:------------------------------------------------------------------- |
| [`SRT_EINVPOLLID`](#srt_einvpollid) | [`eid`](#eid) parameter doesn't refer to a valid epoll container    |
| [`SRT_ETIMEOUT`](#srt_etimeout)     | Up to `msTimeOut` no sockets subscribed in [`eid`](#eid) were ready. This is reported only if `msTimeOut` <br/> was \>=0, otherwise the function waits indefinitely. |
| <img width=240px height=1px/>       | <img width=710px height=1px/>                      |



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_epoll_uwait
```
int srt_epoll_uwait(int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);
```

This function blocks a call until any readiness state occurs in the epoll container. 
Unlike [`srt_epoll_wait`](#srt_epoll_wait), it can only be used with [`eid`](#eid) 
subscribed to user sockets (SRT sockets), not system sockets.

This function blocks until the timeout specified in `msTimeOut` parameter. If
timeout is 0, it exits immediately after checking. If timeout is -1, it blocks
indefinitely until a readiness state occurs.

**Arguments**:

* `eid`: epoll container
* `fdsSet` : A pointer to an array of `SRT_EPOLL_EVENT`
* `fdsSize` : The size of the fdsSet array
* `msTimeOut` : Timeout specified in milliseconds, or special values (0 or -1):
   * 0: Don't wait, return immediately (report any sockets currently ready)
   * -1: Wait indefinitely.

|      Returns                  |                                                                                                                                        |
|:----------------------------- |:-------------------------------------------------------------------------------------------------------------------------------------- |
|       Number                  | The number of user socket (SRT socket) state changes that have been reported in `fdsSet`, <br/> if this number isn't greater than `fdsSize`  |
|   `fdsSize` + 1               | This means that there was not enough space in the output array to report all events. <br/> For events subscribed with the [`SRT_EPOLL_ET`](#SRT_EPOLL_ET) flag only those will be cleared that were reported. <br/> Others will wait for the next call.  |
|         0                     | If no readiness state was found on any socket and the timeout has passed <br/> (this is not possible when waiting indefinitely)              |
|        -1                     | Error                                                                                                                                  |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                        |                                                                   |
|:----------------------------------- |:----------------------------------------------------------------- |
| [`SRT_EINVPOLLID`](#srt_einvpollid) | [`eid`](#eid) parameter doesn't refer to a valid epoll container  |
| [`SRT_EINVPARAM`](#srt_einvparam)   | One of possible usage errors:<br/>* `fdsSize` is < 0<br/>* `fdsSize` is > 0 and `fdsSet` is a null pointer<br/>* [`eid`](#eid) was subscribed to any system socket  |
| <img width=240px height=1px/>       | <img width=710px height=1px/>                      |

**IMPORTANT**: This function reports timeout by returning 0, not by [`SRT_ETIMEOUT`](#srt_etimeout) error.

The `SRT_EPOLL_EVENT` structure:

```
typedef struct SRT_EPOLL_EVENT_
{
    SRTSOCKET fd;
    int       events;
} SRT_EPOLL_EVENT;
```

* `fd`: the user socket (SRT socket)
* [`events`](#events): event flags that report readiness of this socket - a combination
of [`SRT_EPOLL_IN`](#SRT_EPOLL_IN), [`SRT_EPOLL_OUT`](#SRT_EPOLL_OUT) and [`SRT_EPOLL_ERR`](#SRT_EPOLL_ERR).
See [srt_epoll_add_usock](#srt_epoll_add_usock) for details.

Note that when [`SRT_EPOLL_ERR`](#SRT_EPOLL_ERR) is set, the underlying socket error
can't be retrieved with `srt_getlasterror()`. The socket will be automatically
closed and its state can be verified with a call to [`srt_getsockstate`](#srt_getsockstate).


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_epoll_clear_usocks
```
int srt_epoll_clear_usocks(int eid);
```

This function removes all SRT ("user") socket subscriptions from the epoll
container identified by [`eid`](#eid).

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|         0                     | Success                                                   |
|        -1                     | Failure                                                   |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                        |                                                                   |
|:----------------------------------- |:----------------------------------------------------------------- |
| [`SRT_EINVPOLLID`](#srt_einvpollid) | [`eid`](#eid) parameter doesn't refer to a valid epoll container  |
| <img width=240px height=1px/>       | <img width=710px height=1px/>                      |



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_epoll_set
```
int32_t srt_epoll_set(int eid, int32_t flags);
```

This function allows setting or retrieving flags that change the default
behavior of the epoll functions. All default values for these flags are 0.
The following flags are available:

* `SRT_EPOLL_ENABLE_EMPTY`: allows the [`srt_epoll_wait`](#srt_epoll_wait) and 
[`srt_epoll_uwait`](#srt_epoll_uwait) functions to be called with the EID not 
subscribed to any socket. The default behavior of these function is to report 
error in this case.

* `SRT_EPOLL_ENABLE_OUTPUTCHECK`: Forces the [`srt_epoll_wait`](#srt_epoll_wait) 
and [`srt_epoll_uwait`](#srt_epoll_uwait) functions to check if the output array 
is not empty. For [`srt_epoll_wait`](#srt_epoll_wait) it is still allowed that 
either system or user array is empty, as long as EID isn't subscribed to this 
type of socket/fd. [`srt_epoll_uwait`](#srt_epoll_uwait) only checks if
the general output array is not empty.

**Arguments**:

   * `eid`: the epoll container id
   * `flags`: a nonzero set of the above flags, or special values:
      * 0: clear all flags (set all defaults)
      * -1: do not modify any flags

|      Returns                  |                                                                            |
|:----------------------------- |:-------------------------------------------------------------------------- |
|                               | This function returns the state of the flags at the time before the call.  |
|        -1                     | Special value in case when an error occurred.                              |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                        |                                                                   |
|:----------------------------------- |:----------------------------------------------------------------- |
| [`SRT_EINVPOLLID`](#srt_einvpollid) | [`eid`](#eid) parameter doesn't refer to a valid epoll container  |
| <img width=240px height=1px/>       | <img width=710px height=1px/>                      |


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_epoll_release
```
int srt_epoll_release(int eid);
```

Deletes the epoll container.

|      Returns                  |                                                                |
|:----------------------------- |:-------------------------------------------------------------- |
|                               | The number (\>0) of ready sockets, of whatever kind (if any).  |
|        -1                     | Error                           .                              |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                        |                                                                   |
|:----------------------------------- |:----------------------------------------------------------------- |
| [`SRT_EINVPOLLID`](#srt_einvpollid) | [`eid`](#eid) parameter doesn't refer to a valid epoll container  |
| <img width=240px height=1px/>       | <img width=710px height=1px/>                      |




## Logging control

* [srt_setloglevel](#srt_setloglevel)
* [srt_addlogfa, srt_dellogfa, srt_resetlogfa](#srt_addlogfa-srt_dellogfa-srt_resetlogfa)
* [srt_setloghandler](#srt_setloghandler)
* [srt_setlogflags](#srt_setlogflags)

SRT has a widely used system of logs, as this is usually the only way to determine
how the internals are working without changing the rules by the act of tracing.
Logs are split into levels (5 levels out of those defined by syslog are in use)
and additional filtering is possible on an FA (functional area). By default only
entries up to the *Note* log level are displayed and from all FAs.

Logging can only be manipulated globally, with no regard to a specific 
socket. This is because lots of operations in SRT are not dedicated to any 
particular socket, and some are shared between sockets.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
### srt_setloglevel

```
void srt_setloglevel(int ll);
```

Sets the minimum severity for logging. A particular log entry is displayed only 
if it has a severity greater than or equal to the minimum. Setting this value 
to `LOG_DEBUG` turns on all levels.

The constants for this value are those from `<sys/syslog.h>`
(for Windows, refer to `common/win/syslog_defs.h`). The only meaningful ones are:

* `LOG_DEBUG`: Highly detailed and very frequent messages
* `LOG_NOTICE`: Occasionally displayed information
* `LOG_WARNING`: Unusual behavior
* `LOG_ERR`: Abnormal behavior
* `LOG_CRIT`: Error that makes the current socket unusable


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_addlogfa
### srt_dellogfa
### srt_resetlogfa

```c++
void srt_addlogfa(int fa);
void srt_dellogfa(int fa);
void srt_resetlogfa(const int* fara, size_t fara_size);
```

A functional area (FA) is an additional filtering mechanism for logging. You can 
set up logging to display logs only from selected FAs. The list of FAs is
collected in the `srt.h` file, as identified by the `SRT_LOGFA_` prefix. They are 
not enumerated here because they may be changed very often.

All FAs are turned on by default, except potentially dangerous ones
(such as `SRT_LOGFA_HAICRYPT`). The reason is that they may display either
some security information that shall remain in memory only (so, only
if strictly required for development), or some duplicated information
(so you may want to turn one FA on, while turning off the others).


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_setloghandler

```c++
void srt_setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler);
typedef void SRT_LOG_HANDLER_FN(void* opaque, int level, const char* file, int line, const char* area, const char* message);
```

By default logs are printed to standard error stream. This function replaces
the sending to a stream with a handler function that will receive them.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_setlogflags

```c++
void srt_setlogflags(int flags);
```

When you set a log handler with [`srt_setloghandler`](#srt_setloghandler), you 
may also want to configure which parts of the log information you do not wish to 
be passed in the log line (the `message` parameter). A user's logging facility may,
for example, not wish to get the current time or log level marker, as it
will provide this information on its own.

The following flags are available, as collected in the `logging_api.h` public header:

- `SRT_LOGF_DISABLE_TIME`: Do not provide the time in the header
- `SRT_LOGF_DISABLE_THREADNAME`: Do not provide the thread name in the header
- `SRT_LOGF_DISABLE_SEVERITY`: Do not provide severity information in the header
- `SRT_LOGF_DISABLE_EOL`: Do not add the end-of-line character to the log line



## Time Access

* [srt_time_now](#srt_time_now)
* [srt_connection_time](#srt_connection_time)

The following set of functions is intended to retrieve timestamps from the clock 
used by SRT.

The sender can pass the timestamp in `MSGCTRL::srctime` of the `srt_sendmsg2(..)`
function together with the packet being submitted to SRT. If the `srctime` value 
is not provided (the default value of 0 is set), SRT will use the internal clock 
and assign packet submission time as the packet timestamp. If the sender wants to 
explicitly assign a timestamp to a certain packet this timestamp MUST be taken 
from SRT Time Access functions. The time value provided MUST equal or exceed the 
connection start time (`srt_connection_time(..)`) of the SRT socket passed 
to `srt_sendmsg2(..)`.

The current time value of the SRT internal clock can be retrieved using 
the `srt_time_now()` function.

There are two known cases where you might want to use `srctime`:

1. SRT passthrough (for stream gateways).
You may wish to simply retrieve packets from an SRT source and pass them transparently
to an SRT output (possibly re-encrypting). In that case, every packet you read should 
preserve the original value of `srctime` as obtained from [`srt_recvmsg2`](#srt_recvmsg2),
and the original `srctime` for each packet should be then passed to [`srt_sendmsg2`](#srt_sendmsg).
This mechanism could be used to avoid jitter resulting from varying differences between
the time of receiving and sending the same packet.

2. Stable timing source.
In the case of a live streaming procedure, when spreading packets evenly into the stream,
you might want to predefine times for every single packet to keep time intervals perfectly equal.
Or, if you believe that your input signal delivers packets at the exact times that should be
assigned to them, you might want to preserve these times at the SRT receiving side
to avoid jitter that may result from varying time differences between the packet arrival
and the moment when sending it over SRT. In such cases you might do the following:

    - At the packet arrival time, grab the current time at that moment using `srt_time_now()`.

    - When you want a pre-calculated packet time, use a private relative time counter
    set at the moment when the connection was made. From the moment when your first packet
    is ready, start pre-calculating packet times relative to the connection start time obtained
    from `srt_connection_time()`. Although you still have to synchronize sending times with these
    predefined times, by explicitly specifying the source time you avoid the jitter
    resulting from a lost accuracy due to waiting time and unfortunate thread scheduling.

Note that `srctime` uses an internally defined clock that is intended to be monotonic 
(the definition depends on the build flags, see below). Because of that **the application 
should not define this time basing on values obtained from the system functions for getting 
the current system time** (such as `time`, `ftime` or `gettimeofday`). To avoid problems and
misunderstanding you should rely exclusively on time values provided by the
`srt_time_now()` and `srt_connection_time()` functions.

The clock used by the SRT internal clock is determined by the following build flags:
- `ENABLE_MONOTONIC` makes use of `CLOCK_MONOTONIC` with `clock_gettime` function.
- `ENABLE_STDXXX_SYNC` makes use of `std::chrono::steady_clock`.

The default is currently to use the system clock as the internal SRT clock,
although it's highly recommended to use one of the above monotonic clocks,
as system clock is vulnerable to time modifications during transmission.



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
### srt_time_now

```c++
int64_t srt_time_now();
```

Get time in microseconds elapsed since epoch using SRT internal clock (steady or monotonic clock).

|      Returns                  |                                                                          |
|:----------------------------- |:------------------------------------------------------------------------ |
|                               | Current time in microseconds elapsed since epoch of SRT internal clock.  |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_connection_time

```c++
int64_t srt_connection_time(SRTSOCKET sock);
```

Get connection time in microseconds elapsed since epoch using SRT internal clock 
(steady or monotonic clock). The connection time represents the time when SRT socket 
was open to establish a connection. Milliseconds elapsed since connection start time 
can be determined using [**Performance tracking**](#Performance-tracking) functions 
and `msTimeStamp` value of the `SRT_TRACEBSTATS` (see [statistics.md](statistics.md)).

|      Returns                  |                                                                             |
|:----------------------------- |:--------------------------------------------------------------------------- |
|                               | Connection time in microseconds elapsed since epoch of SRT internal clock.  |
|        -1                     | Error                                                                       |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                      |                                                            |
|:--------------------------------- |:---------------------------------------------------------- |
| [`SRT_EINVSOCK`](#srt_einvsock)   | Socket `sock` is not an ID of a valid SRT socket           |
| <img width=240px height=1px/>     | <img width=710px height=1px/>                      |

  
---  
  
  ## Diagnostics

General notes concerning the `getlasterror` diagnostic functions: when an API
function ends up with error, this error information is stored in a thread-local
storage. This means that you'll get the error of the operation that was last
performed as long as you call this diagnostic function just after the failed
function has returned. In any other situation the information provided by the
diagnostic function is undefined.

**NOTE**: There is a list of [Error Codes](#error-codes) at the bottom of this document.


* [srt_getlasterror_str](#srt_getlasterror_str)
* [srt_getlasterror](#srt_getlasterror)
* [srt_strerror](#srt_strerror)
* [srt_clearlasterror](#srt_clearlasterror)
* [srt_getrejectreason](#srt_getrejectreason)
* [srt_rejectreason_str](#srt_rejectreason_str)
* [srt_setrejectreason](#srt_setrejectreason)

  
### srt_getlasterror

```
int srt_getlasterror(int* errno_loc);
```

Get the numeric code of the last error. Additionally, in the variable passed as
`errno_loc` the system error value is returned, or 0 if there was no system error
associated with the last error. The system error is:

  * On POSIX systems, the value from `errno`
  * On Windows, the result from `GetLastError()` call


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_strerror
```
const char* srt_strerror(int code, int errnoval);
```

Returns a string message that represents a given SRT error code and possibly the
`errno` value, if not 0.

**NOTE:** *This function isn't thread safe. It uses a static variable to hold the
error description. There's no problem with using it in a multithreaded environment,
as long as only one thread in the whole application calls this function at the
moment*



[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_getlasterror_str
```
const char* srt_getlasterror_str(void);
```

Get the text message for the last error. It's a shortcut to calling first
`srt_getlasterror` and then passing the returned value into [`srt_strerror`](#srt_strerror).
Note that, contrary to [`srt_strerror`](#srt_strerror), this function is thread safe.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_clearlasterror

```
void srt_clearlasterror(void);
```

This function clears the last error. After this call, `srt_getlasterror` will
report a "successful" code.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)


---  
  
### srt_rejectreason_str

```
const char* srt_rejectreason_str(enum SRT_REJECT_REASON id);
```

Returns a constant string for the reason of the connection rejected, as per given 
code ID. It provides a system-defined message for values below `SRT_REJ_E_SIZE`. 
For other values below `SRT_REJC_PREDEFINED` it returns the string for 
[`SRT_REJ_UNKNOWN`](#SRT_REJ_UNKNOWN). For values since `SRT_REJC_PREDEFINED` on, 
returns "Application-defined rejection reason".

The actual messages assigned to the internal rejection codes, that is, less than 
`SRT_REJ_E_SIZE`, can be also obtained from the `srt_rejectreason_msg` array.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

---  
  
### srt_setrejectreason

```
int srt_setrejectreason(SRTSOCKET sock, int value);
```

Sets the rejection code on the socket. This call is only useful in the listener 
callback. The code from `value` set this way will be set as a rejection reason 
for the socket. After the callback rejects the connection, the code will be passed 
back to the caller peer with the handshake response.

Note that allowed values for this function begin with `SRT_REJC_PREDEFINED`
(that is, you cannot set a system rejection code). For example, your application 
can inform the calling side that the resource specified under the `r` key in the 
StreamID string (see [`SRTO_STREAMID`](../docs/APISocketOptions.md#SRTO_STREAMID))
is not available - it then sets the value to `SRT_REJC_PREDEFINED + 404`.

|      Returns                  |                                                           |
|:----------------------------- |:--------------------------------------------------------- |
|         0                     | Error                                                     |
|        -1                     | Success                                                   |
| <img width=240px height=1px/> | <img width=710px height=1px/>                      |

|       Errors                      |                                              |
|:--------------------------------- |:-------------------------------------------- |
| [`SRT_EINVSOCK`](#srt_einvsock)   | Socket `sock` is not an ID of a valid socket |
| [`SRT_EINVPARAM`](#srt_einvparam) | `value` is less than `SRT_REJC_PREDEFINED`   |
| <img width=240px height=1px/>     | <img width=710px height=1px/>                      |

[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)


---  
  
### srt_getrejectreason

```
int srt_getrejectreason(SRTSOCKET sock);
```
This function provides a more detailed reason for a failed connection attempt. It 
shall be called after a connecting function (such as [`srt_connect`](#srt_connect))
has returned an error, the code for which is [`SRT_ECONNREJ`](#srt_econnrej). If 
[`SRTO_RCVSYN`](../docs/APISocketOptions.md#SRTO_RCVSYN) has been set on the socket 
used for the connection, the function should also be called when the 
[`SRT_EPOLL_ERR`](#SRT_EPOLL_ERR) event is set for this socket. It returns 
a numeric code, which can be translated into a message by 
[`srt_rejectreason_str`](#srt_rejectreason_str).

  
## Rejection Reasons

  
#### SRT_REJ_UNKNOWN

A fallback value for cases when there was no connection rejected.


#### SRT_REJ_SYSTEM

One system function reported a failure. Usually this means some system
error or lack of system resources to complete the task.

  
#### SRT_REJ_PEER

The connection has been rejected by peer, but no further details are available.
This usually means that the peer doesn't support rejection reason reporting.

  
#### SRT_REJ_RESOURCE

A problem with resource allocation (usually memory).

  
#### SRT_REJ_ROGUE

The data sent by one party to another cannot be properly interpreted. This
should not happen during normal usage, unless it's a bug, or some weird
events are happening on the network.

[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
#### SRT_REJ_BACKLOG

The listener's backlog has exceeded its queue limit (there are many other callers 
waiting for the opportunity of being connected and wait in the queue, which has 
reached its limit).

  
#### SRT_REJ_IPE

Internal Program Error. This should not happen during normal usage and it
usually means a bug in the software (although this can be reported by both
local and foreign host).

  
#### SRT_REJ_CLOSE

The listener socket was able to receive your request, but at this moment it
is being closed. It's likely that your next attempt will result in a timeout.

  
#### SRT_REJ_VERSION

One party of the connection has set up a minimum version that is required for
that connection, but the other party didn't satisfy this requirement.

  
#### SRT_REJ_RDVCOOKIE

Rendezvous cookie collision. This normally should never happen, or the 
probability that it will happen is negligible. However this can also be 
a result of a misconfiguration in that you are trying to make a 
rendezvous connection where both parties try to bind to the same IP 
address, or both are local addresses of the same host. In such a case 
the sent handshake packets are returning to the same host as if they 
were sent by the peer (i.e. a party is sending to itself). When this happens, 
this reject reason will be reported by every attempt.

[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
#### SRT_REJ_BADSECRET

Both parties have defined a passphrase for connection, but they differ.

  
#### SRT_REJ_UNSECURE

Only one connection party has set up a password. See also the 
[`SRTO_ENFORCEDENCRYPTION`](../docs/APISocketOptions.md#SRTO_ENFORCEDENCRYPTION) flag.

  
#### SRT_REJ_MESSAGEAPI

The value of the [`SRTO_MESSAGEAPI`](../docs/APISocketOptions.md#SRTO_MESSAGEAPI) 
flag is different on both connection parties.

  
#### SRT_REJ_CONGESTION

The [`SRTO_CONGESTION`](../docs/APISocketOptions.md#SRTO_CONGESTION)option has 
been set up differently on both connection parties.

  
#### SRT_REJ_FILTER

The [`SRTO_PACKETFILTER`](../docs/APISocketOptions.md#SRTO_PACKETFILTER) option 
has been set differently on both connection parties.

[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
#### SRT_REJ_GROUP

The group type or some group settings are incompatible for both connection parties. 
While every connection within a bonding group may have different target addresses, 
they should all designate the same endpoint and the same SRT application. If this 
condition isn't satisfied, then the peer will respond with a different peer group 
ID for the connection that is trying to contact a machine/application that is 
completely different from the existing connections in the bonding group.

  
#### SRT_REJ_TIMEOUT

The connection wasn't rejected, but it timed out. This code is always set on
connection timeout, but this is the only way to get this state in non-blocking
mode (see [`SRTO_RCVSYN`](../docs/APISocketOptions.md#SRTO_RCVSYN)).

There may also be server and user rejection codes, as defined by the 
`SRT_REJC_INTERNAL`, `SRT_REJC_PREDEFINED` and `SRT_REJC_USERDEFINED`
constants. Note that the number space from the value of `SRT_REJC_PREDEFINED`
and above is reserved for "predefined codes" (`SRT_REJC_PREDEFINED` value plus
adopted HTTP codes). Values above `SRT_REJC_USERDEFINED` are freely defined by
the application.


[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
  
## Error Codes

All functions that return the status via `int` value return -1 (designated as 
`SRT_ERROR`) always when the call has failed (in case of resource creation
functions an appropriate symbol is defined, like `SRT_INVALID_SOCK` for
`SRTSOCKET`). When this happens, the error code can be obtained from the
`srt_getlasterror` function. The values for the error are collected in an
`SRT_ERRNO` enum:

  
#### `SRT_EUNKNOWN`

Internal error when setting the right error code.

  
#### `SRT_SUCCESS`

The value set when the last error was cleared and no error has occurred since then.

  
#### `SRT_ECONNSETUP`

General setup error resulting from internal system state.

  
#### `SRT_ENOSERVER`

Connection timed out while attempting to connect to the remote address. Note
that when this happens, [`srt_getrejectreason`](#srt_getrejectreason) also reports 
the timeout reason.

  
#### `SRT_ECONNREJ`

Connection has been rejected. Additional reject reason can be obtained through
[`srt_getrejectreason`](#srt_getrejectreason) (see above).

[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
#### `SRT_ESOCKFAIL`

An error occurred when trying to call a system function on an internally used
UDP socket. Note that the detailed system error is available in the extra variable
passed by pointer to `srt_getlasterror`.

  
#### `SRT_ESECFAIL`

A possible tampering with the handshake packets was detected, or an encryption
request wasn't properly fulfilled.

  
#### `SRT_ESCLOSED`

A socket that was vital for an operation called in blocking mode
has been closed during the operation. Please note that this situation is
handled differently than the system calls for `connect` and `accept`
functions for TCP, which simply block indefinitely (or until the standard
timeout) when the key socket was closed during an operation. When this 
error is reported, it usually means that the socket passed as the first 
parameter to [`srt_connect*`](#srt_connect) or [`srt_accept`](#srt_accept) 
is no longer usable.

  
#### `SRT_ECONNFAIL`

General connection failure of unknown details.

  
#### `SRT_ECONNLOST`

The socket was properly connected, but the connection has been broken.
This specialization is reported from the transmission functions.

[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
#### `SRT_ENOCONN`

The socket is not connected. This can be reported also when the connection was 
broken for a function that checks some characteristic socket data.

  
#### `SRT_ERESOURCE`

System or standard library error reported unexpectedly for unknown purpose.
Usually it means some internal error.

  
#### `SRT_ETHREAD`

System was unable to spawn a new thread when required.

  
#### `SRT_ENOBUF`

System was unable to allocate memory for buffers.

  
#### `SRT_ESYSOBJ`

System was unable to allocate system specific objects (such as
sockets, mutexes or condition variables).

[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
#### `SRT_EFILE`

General filesystem error (for functions operating with file transmission).

  
#### `SRT_EINVRDOFF`

Failure when trying to read from a given position in the file (file could
be modified while it was read from).

  
#### `SRT_ERDPERM`

Read permission was denied when trying to read from file.

  
#### `SRT_EINVWROFF`

Failed to set position in the written file.

  
#### `SRT_EWRPERM`

Write permission was denied when trying to write to a file.

[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
#### `SRT_EINVOP`

Invalid operation performed for the current state of a socket. This mainly
concerns performing `srt_bind*` operations on a socket that is already bound.  
Once a socket has been been bound, it cannot be bound again.

  
#### `SRT_EBOUNDSOCK`

The socket is currently bound and the required operation cannot be
performed in this state. Usually it's about an option that can only
be set on the socket before binding (`srt_bind*`). Note that a socket
that is currently connected is also considered bound.

  
#### `SRT_ECONNSOCK`

The socket is currently connected and therefore performing the required operation 
is not possible. Usually concerns setting an option that must be set before 
connecting (although it is allowed to be altered after binding), or when trying 
to start a connecting operation ([`srt_connect*`](#srt_connect)) while the socket 
isn't in a state that allows it (only [`SRTS_INIT`](#SRTS_INIT) or
[`SRTS_OPENED`](#SRTS_OPENED) are allowed).

  
#### `SRT_EINVPARAM`

This error is reported in a variety of situations when call parameters
for API functions have some requirements defined and these were not
satisfied. This error should be reported after an initial check of the
parameters of the call before even performing any operation. This error
can be easily avoided if you set the values correctly.

  
#### `SRT_EINVSOCK`

The API function required an ID of an entity (socket or group) and
it was invalid. Note that some API functions work only with socket or
only with group, so they would also return this error if inappropriate
type of entity was passed, even if it was valid.

[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
#### `SRT_EUNBOUNDSOCK`

The operation to be performed on a socket requires that it first be explicitly 
bound (using [`srt_bind*`](#srt_bind) functions). Currently it applies when
calling [`srt_listen`](#srt_listen), which cannot work with an implicitly 
bound socket.

  
#### `SRT_ENOLISTEN`

The socket passed for the operation is required to be in the listen
state ([`srt_listen`](#srt_listen) must be called first).

  
#### `SRT_ERDVNOSERV`

The required operation cannot be performed when the socket is set to rendezvous 
mode ([`SRTO_RENDEZVOUS`](../docs/APISocketOptions.md#SRTO_RENDEZVOUS) set to true). 
Usually applies when trying to call [`srt_listen`](#srt_listen) on such a socket.

  
#### `SRT_ERDVUNBOUND`

An attempt was made to connect to a socket set to rendezvous mode 
([`SRTO_RENDEZVOUS`](../docs/APISocketOptions.md#SRTO_RENDEZVOUS) set to true) 
that was not first bound. A rendezvous connection requires setting up two addresses 
and ports on both sides of the connection, then setting the local one with 
[`srt_bind`](#srt_bind) and using the remote one with [`srt_connect`](#srt_connect) 
(or you can simply use [`srt_rendezvous`](#srt_rendezvous)). Calling 
[`srt_connect*`](#srt_connect) on an unbound socket (in [`SRTS_INIT`](#SRTS_INIT) 
state) that is to be bound implicitly is only allowed for regular caller sockets 
(not rendezvous).

  
#### `SRT_EINVALMSGAPI`

The function was used incorrectly in the message API. This can happen if:

* The parameters specific for the message API in [`SRT_MSGCTRL`](#SRT_MSGCTRL) 
type parameter were incorrectly specified

* The extra parameter check performed by the congestion controller has failed

* The socket is a member of a self-managing group, therefore you should
perform the operation on the group, not on this socket

[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
#### `SRT_EINVALBUFFERAPI`

The function was used incorrectly in the stream (buffer) API, that is, either the 
stream-only functions were used with set message API ([`srt_sendfile`](#srt_sendfile)/[`srt_recvfile`](#srt_recvfile)) 
or TSBPD mode was used with buffer API ([`SRTO_TSBPDMODE`](../docs/APISocketOptions.md#SRTO_TSBPDMODE) set to true) 
or the congestion controller has failed to check call parameters.

  
#### `SRT_EDUPLISTEN`

The port tried to be bound for listening is already busy. Note that binding to the same port 
is allowed in general (when [`SRTO_REUSEADDR`](../docs/APISocketOptions.md#SRTO_REUSEADDRS) 
is true on every socket that has bound it), but only one such socket can be a listener.

  
#### `SRT_ELARGEMSG`

Size exceeded. This is reported in the following situations:

* Trying to receive a message, but the read-ready message is larger than
the buffer passed to the receiving function

* Trying to send a message, but the size of this message exceeds the
size of the preset sender buffer, so it cannot be stored in the sender buffer.

* When getting group data, the array to be filled is too small.

  
#### `SRT_EINVPOLLID`

The epoll ID passed to an epoll function is invalid

  
#### `SRT_EPOLLEMPTY`

The epoll container currently has no subscribed sockets. This is reported by an
epoll waiting function that would in this case block forever. This problem
might be reported both in a situation where you have created a new epoll
container and didn't subscribe any sockets to it, or you did, but these
sockets have been closed (including when closed in a separate thread while the
waiting function was blocking). Note that this situation can be prevented
by setting the `SRT_EPOLL_ENABLE_EMPTY` flag, which may be useful when
you use multiple threads and start waiting without subscribed sockets, so that
you can subscribe them later from another thread.

[:arrow_up: &nbsp; Back to List of Functions & Structures](#srt-api-functions)

  
#### `SRT_EASYNCFAIL`

General asynchronous failure (not in use currently).

  
#### `SRT_EASYNCSND`

Sending operation is not ready to perform. This error is reported when trying to 
perform a sending operation on a socket that is not ready for sending, but 
[`SRTO_SNDSYN`](../docs/APISocketOptions.md#SRTO_SNDSYN) was set to false (when 
true, the function would block the call otherwise).

  
#### `SRT_EASYNCRCV`

Receiving operation is not ready to perform. This error is reported when trying to 
perform a receiving operation or accept a new socket from the listener socket, when 
the socket is not ready for that operation, but [`SRTO_RCVSYN`](../docs/APISocketOptions.md#SRTO_RCVSYN) 
was set to false (when true, the function would block the call otherwise).

  
#### `SRT_ETIMEOUT`

The operation timed out. This can happen if you have a timeout set by an option 
([`SRTO_RCVTIMEO`](../docs/APISocketOptions.md#SRTO_RCVTIMEO) or 
[`SRTO_SNDTIMEO`](../docs/APISocketOptions.md#SRTO_SNDTIMEO)), or passed as an 
extra argument ([`srt_epoll_wait`](#srt_epoll_wait) or [`srt_accept_bond`](#srt_accept_bond)) 
and the function call was blocking, but the required timeout time has passed.

  
#### `SRT_ECONGEST`

**NOTE**: This error is used only in an experimental version that requires
setting the `SRT_ENABLE_ECN` macro at compile time. Otherwise the situation 
described below results in the usual successful report.

This error should be reported by the sending function when, with
[`SRTO_TSBPDMODE`](../docs/APISocketOptions.md#SRTO_TSBPDMODE) and 
[`SRTO_TLPKTDROP`](../docs/APISocketOptions.md#SRTO_TLPKTDROP) set to true, some 
packets were dropped at the sender side (see the description of 
[`SRTO_TLPKTDROP`](../docs/APISocketOptions.md#SRTO_TLPKTDROP) for details). This
doesn't concern the data that were passed for sending by the sending function
(these data are placed at the back of the sender buffer, while the dropped
packets are at the front). In other words, the operation done by the sending
function is successful, but the application might want to slow down the sending
rate to avoid congestion.

  
#### `SRT_EPEERERR`

This error is reported when a receiver peer is writing to a file that an agent 
is sending. When the peer encounters an error when writing the received data to 
a file, it sends the `UMSG_PEERERROR` message back to the sender, and the sender 
reports this error from the API sending function.


  
  

[RETURN TO TOP OF PAGE](#SRT-API-Functions)
