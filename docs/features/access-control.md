# SRT Access Control (Stream ID) Guidelines

## Motivation

One type of information that an SRT caller can share with an SRT listener during a connection negotiation
is the "Stream ID". This is a string of maximum 512 characters set on the caller
side. It can be retrieved at the listener side on the newly accepted socket
through a socket option (see `SRTO_STREAMID` in [SRT API Socket Options](../API/API-socket-options.md)).
**Only caller-listener connections are supported.**

As of SRT version 1.3.3 a callback can be registered on the listener socket for
an application to make decisions on incoming caller connections. This callback,
among others, is provided with the value of Stream ID from the incoming
connection. Based on this information, the application can accept or reject the
connection, select the desired data stream, or set an appropriate passphrase for
the connection.

## Purpose

There are two target use-cases of the Stream ID:
- identify the file name of a stream that is about to be sent (simple use case);
- identify a user, the purpose of the connection (receive or send), the resources, and more (advanced use case).

The Stream ID can be provided as free-form value, especially when targeting the simple use case.
However, there is a recommended convention so that all SRT users speak the same language.
The intent of the convention is to:

- promote readability and consistency among free-form names
- interpret some typical data in the key-value style

In short,

1. `SRTO_STREAMID` is designed for a caller (client) to be able to identify itself, and to state its intent (send/receive, live/file, etc.).
2. `srt_listen_callback(...)` function is used by a listener (server) to check what a caller (client) has provided in `SRTO_STREAMID` **before** the connection is established.
For example, the listener (server) can check if it knows the user and set the corresponding passphrase for a connection to be accepted.
3. Even if `srt_listen_callback(...)` accepts the connection, SRT will still have one more step to check the PASSPHRASE, and reject on mismatch.
If a correct passphrase is not provided by the client (caller), the request from caller will be rejected by SRT library (not application or programmer).

**Note!** `srt_listen_callback(...)` can't check the passphrase directly for security reasons.
The only way to make the app check the passphrase is to set the passphrase on the socket by using the `SRTO_PASSPHRASE` option. This lets SRT to reject connection on mismatch.

## Character Encoding

The Stream ID uses UTF-8 encoding.

## General Syntax

The recommended syntax starts with the characters known as an executable specification in POSIX: `#!`.

The next character defines the format used for the following key-value pair syntax.
At the moment, there is only one supported syntax identified by `:` and described below.

Everything that comes after a syntax identifier is further referenced as the content of the Stream ID.

The content starts with a `:` or `{` character identifying its format:

- `:` : comma-separated key-value pairs with no nesting,
- `{` : a nested block with one or several key-value pairs that must end with a `}` character. Nesting means that multiple level brace-enclosed parts are allowed.

The form of the key-value pair is

~~~
key1=value1,key2=value2,...
~~~

## Standard Keys

Beside the general syntax, there are several top-level keys treated as standard
keys. All single letter key definitions, including those not listed in this section,
are reserved for future use. Users can additionally use custom key definitions
with `user_*` or `companyname_*` prefixes, where `user` and `companyname` are
to be replaced with an actual user or company name.

The existing key values must not be extended, and must not differ from those described in this section.

The following keys are standard:

- `u`: **User Name**, or authorization name, that is expected to control which
password should be used for the connection. The application should interpret
it to distinguish which user should be used by the listener party to set up the
password.
- `r`: **Resource Name** identifies the name of the resource and facilitates
selection should the listener party be able to serve multiple resources.
- `h`: **Host Name** identifies the hostname of the resource. For example,
to request a stream with the URI `somehost.com/videos/querry.php?vid=366` the
`hostname` field should have `somehost.com`, and the resource name can have
`videos/querry.php?vid=366` or simply `366`. Note that this is still a key to be
specified explicitly. Support tools that apply simplifications and URI extraction
are expected to insert only the host portion of the URI here.
- `s`: **Session ID** is a temporary resource identifier negotiated with
the server, used just for verification. This is a one-shot identifier, invalidated
after the first use. The expected usage is when details for the resource and
authorization are negotiated over a separate connection first, and then the
session ID is used here alone.
- `t`: **Type** specifies the purpose of the connection. Several standard
types are defined, but users may extend the use:
  - `stream` (default, if not specified): for exchanging the user-specified
  payload for an application-defined purpose
  - `file`: for transmitting a file, where `r` is the filename
  - `auth`: for exchanging sensible data. The `r` value states its purpose.
  No specific possible values for that are known so far (FUTURE USE]
- `m`: **Mode** expected for this connection:
  - `request` (default): the caller wants to receive the stream
  - `publish`: the caller wants to send the stream data
  - `bidirectional`:  bidirectional data exchange is expected

Note that `m` is not required in the case where you don't use `streamid` to
distinguish authorization or resources, and your caller is expected to send the
data. This is only for cases where the listener can handle various purposes of the
connection and is therefore required to know what the caller is attempting to do.

Examples:

```js
#!::u=admin,r=bluesbrothers1_hi
```

This specifies the username and the resource name of the stream to be served
to the caller.

```js
#!::u=johnny,t=file,m=publish,r=results.csv
```

This specifies that the file is expected to be transmitted from the caller to
the listener and its name is `results.csv`.

### Rejection Codes

The listener callback handler is also able to decide about rejecting the
incoming connection. In a normal situation, the rejection code is predefined
as `SRT_REJ_RESOURCE`. The handler can, however, set its own rejection
code. There are two number spaces intended for this purpose (as the range
below `SRT_REJC_PREDEFINED` is reserved for internal codes):

- `SRT_REJC_PREDEFINED` and above: predefined errors. Errors from this range
(that is, below `SRT_REJC_USERDEFINED`) have their definitions provided in
the `access_control.h` public header file. The intention is that applications
using these codes understand the situation described by these codes standard
way.

- `SRT_REJC_USERDEFINED` and above: to be freely defined by the application.
Codes from this range can be only understood if each application knows the
code definitions of the other. These codes should be used only after making
sure that both applications understood them.

The intention for the predefined codes is to be consistent with the HTTP
standard codes. Therefore the following sub-ranges are used:

- 0 - 99: Reserved for unique SRT-specific codes (unused by HTTP)
- 100 - 399: Info, Success and Redirection in HTTP, unused in SRT
- 400 - 599: Client and server errors in HTTP, adopted by SRT
- 600 - 999: unused in SRT

Such a code can be set by using the `srt_setrejectreason` function.

See the list of rejection codes in the [Rejection Codes](../API/rejection-codes.md) document.

## Example

An example of Stream ID functionality and the listener callback can be
found under `tests/test_listen_callback.cpp`.

A listener can register a callback to be called in the middle of accepting a
new socket connection:

```c++
srt_listen(server_sock, 5);
srt_listen_callback(server_sock, &SrtTestListenCallback, NULL);
```

A callback function has to be implemented by the upstream application. In the
example below, the function tries to interpret the Stream ID value first according
to the Access Control guidelines and to extract the username from the `u` key.
Otherwise it falls back to a free-form specified username. Depending on the user,
it sets the appropriate password for the expected connection so that it can be
rejected if the password isn't correct. If the user isn't found in the
database (`passwd` map) the function itself rejects the connection. Note that
this can be done by both returning -1 and by throwing an exception.

```c++
int SrtTestListenCallback(void* opaq, SRTSOCKET ns, int hsversion,
    const struct sockaddr* peeraddr, const char* streamid)
{
    using namespace std;

    // opaq is used to pass some further chained callbacks

    // To reject a connection attempt, return -1.

    static const map<string, string> passwd {
        {"admin", "thelocalmanager"},
        {"user", "verylongpassword"}
    };

    // Try the "standard interpretation" with username at key u
    string username;

    static const char stdhdr [] = "#!::";
    uint32_t* pattern = (uint32_t*)stdhdr;
    bool found = -1;

    // Extract a username from the StreamID:
    if (strlen(streamid) > 4 && *(uint32_t*)streamid == *pattern)
    {
        vector<string> items;
        Split(streamid+4, ',', back_inserter(items));
        for (auto& i: items)
        {
            vector<string> kv;
            Split(i, '=', back_inserter(kv));
            if (kv.size() == 2 && kv[0] == "u")
            {
                username = kv[1];
                found = true;
            }
        }

        if (!found)
        {
            cerr << "TEST: USER NOT FOUND, returning false.\n";
            return -1;
        }
    }
    else
    {
        // By default the whole streamid is username
        username = streamid;
    }

    // When the username of the client is known, the passphrase can be set
    // on the socket being accepted (SRTSOCKET ns).
    // The remaining part of the SRT handshaking process will check the
    // passphrase of the client and accept or reject the connection.

    // When not found, it will throw an exception
    cerr << "TEST: Accessing user '" << username << "', might throw if not found\n";
    string exp_pw = passwd.at(username);

    cerr << "TEST: Setting password '" << exp_pw << "' as per user '" << username << "'\n";
    srt_setsockflag(ns, SRTO_PASSPHRASE, exp_pw.c_str(), exp_pw.size());
    return 0;
}
```
