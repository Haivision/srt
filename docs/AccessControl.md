# SRT Access Control Guidelines

When an SRT connection is being established, peers interchange the handshake packets.
Each handshake, in particular, may contain a passphrase and a field called StreamID (see `SRTO_STREAMID` in [API.md](API.md)).
This field can have a length of up to 512 bytes, and is intended to control access to a stream to be transmitted.

Since SRT version 1.3.3 a callback can be registered on the listener socket, for an application to make decisions on incoming caller connections. In particular, the application can retrieve and check the Stream ID, provided by the caller. The application can allow or deny a certain connection and select a custom passphrase depending on the user requesting the desired resource (e.g. a live stream).

The stream ID value can be used as free-form, but there is a recommended
protocol so that all SRT users speak the same language here. The intent of
the protocol is:

- maintain distinction from a "readable name" in a free form
- interpret some typical data in the key-value style

## Character Encoding

The stream ID uses UTF-8 encoding.

## Recommended Stream ID Usage

This recommended protocol starts with the characters known as executable specification
in POSIX: `#!`. The next two characters are:

- `:` - this marks the YAML format, the only one currently used
- The content format, which is either:
   - `:` - the comma-separated keys with no nesting
   - `{` - like above, but nesting is allowed and must end with `}`

The form of the key-value pair is:

- `key1=value1,key2=value2`...

Beside the general syntax, there are several top-level keys treated as
standard keys. Other keys can be used by users as they see fit.

The following keys are standard:

- `u`: User Name, or authorization name that is expected to control
which password should be used for the connection. The application should
interpret it to distinguish which user should be used by the listener
party to set up the password
- `r`: Resource Name. This identifies the name of the resource, should
the listener party be able to serve multiple resources for choice
- `h`: Host Name. This identifies the hostname of the resource. 
For example, to request a stream with the URI somehost.com/videos/querry.php?vid=366.
The hostname field should have “somehost.com”, and the resource name can have “videos/querry.php?vid=366” or simply "366".
- `s`: Session ID. This is a temporary resource identifier negotiated
with the server, used just for verification. This is a one-shot identifier,
invalidated after the first use.
- `t`: Type. This specifies the purpose of the connection. Several
standard types are defined, but users may extend the use:
   - `stream` (default, if not specified): you want to exchange the
     user-specified payload of application-defined purpose
   - `file`: You want to transmit a file, and `r` is the filename
   - `auth`: You want to exchange the data needed for further exchange
     of sensible data. The `r` value states its purpose. No specific
     possible values for that are known so far, maybe in future
- `m`: Mode expected for this connection
   - `request` (default): the caller wants to receive the stream
   - `publish`: the caller wants to send the stream data
   - `bidirectional`: the bidirectional data exchange is expected

Note that `m` is not required in case when you don't use streamid and
your caller is expected to send the data. This is only for cases when
the server at the listener party is required to get known what the
caller is attempting to do.

Examples:

```#!::u=admin,r=bluesbrothers1_hi```

This specifies the username and the resource name of the stream to be served
to the caller.

```#!::u=johnny,t=file,m=publish,r=results.csv```

This specifies that the file is expected to be transmitted from the caller
to the listener and its name is `results.csv`.


## Example

An example usage os Stream ID functionality and the listener callback can be fount under tests/test_listen_callback.cpp.

A listener can register a callback to be called in the middle of accepting a new socket connection.

```
srt_listen(server_sock, 5);
srt_listen_callback(server_sock, &SrtTestListenCallback, NULL);
```

A callback function has to be implementaed by the upstream application. Example implementation follows.

```
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

