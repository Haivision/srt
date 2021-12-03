# srt-test-multiplex

**srt-test-multiplex** (formerly called "SIPLEX") is a sample program that can
send multiple streams in one direction. This tool demonstrates two SRT features:
 - the ability to use a single UDP link (a source and destination pair 
 specified by IP address and port) for multiple SRT connections
 - the use of the `streamid` socket option to identify multiplexed resources

NOTE: To make this application compiled, you need the `-DENABLE_TESTING=1`
cmake option.

Note also that this application is intended for demonstration only. It can
simply exit with error message in case of wrong usage or broken connection.


## Usage

`srt-test-multiplex <SRT-URI> -i <INPUT-URI1>[id1] <INPUT-URI2>[id2]...`

  - Reads all given input streams and sends them each one over a separate
  SRT connection, all using the same remote address and source port (hence
  using the same UDP link).

`srt-test-multiplex <SRT-URI> -o <OUTPUT-URI1>[id] <OUTPUT-URI2>[id]...`

  - Transmits data from a multiplexed SRT stream to the specified output URI(s).

An `<SRT URI>` can be identified as input or output using the **-i** or **-o** 
options. When `-i` is specified, the URIs provided are used as input, and will 
be output over the `<SRT URI>` socket. The reverse is true for any output URIs 
specified by `-o`.

If SRT-URI is caller mode, then for every declared input or output medium a
separate connection will be made, each one using the same source port (if
specified, the same will be used for all connections, otherwise the first one
will be automatically selected and then reused for all next ones) and with the
`streamid` socket option set to the value extracted from the medium's
`id` specified parameter:
```
URI1?id=a --> s1(streamid=a).connect(remhost:2000)
URI2?id=b --> s2(streamid=b).connect(remhost:2000)
URI3?id=c --> s3(streamid=c).connect(remhost:2000)
```

If SRT-URI is listener mode, then it will extract the value from `streamid`
socket option and every accepted connection will be matched against the `id`
parameter of the specified input or output medium.
```
(remhost:2000) -> accept --> s(SRT socket) --> in URI array find such that uri.id == s.streamid
```

Note that the rendezvous mode is not supported because you cannot make
multiple connections over the same UDP link in rendezvous mode.

This `streamid` is the SRT socket option (`SRTO_STREAMID` in the API). The idea 
is that it can be set on a socket used for connecting. When a listener is 
getting an accepted socket for that connection, the `streamid` socket option 
can be read from it, with the result that it will be the same as was set on 
the caller side.


## Examples

  - **Sender:**  
    - `srt-test-multiplex srt://remhost:2000 -i udp://:5000?id=low udp://:6000?id=high`
  - **Receiver:**
    - `srt-test-multiplex srt://:2000 -o output-high.ts?id=high output-low.ts?id=low`

In this example a Sender is created which will connect to `remhost` port 2000 
using multiple SRT sockets, all of which will be using the same outgoing port. 
Here the outgoing port is automatically selected when connecting. Subsequent 
sockets will reuse that port. Alternatively you can enforce the outgoing port 
using the `port` parameter with the `<SRT URI>`.

  - **Sender:**  
    - `srt-test-multiplex srt://remhost:2000?port=5555 ...`

A separate connection is made for every input resource. An appropriate resource 
ID will be set to each socket assigned to that resource according to the `id` 
parameter.
```
           +--                                                   --+
           |   id=1                                         id=1   |
           |  ------                                      -------  |
           |         \               ----->              /         |
           |   id=2   \  ----------------------------   /   id=2   |
port=5555 -|  --------- (   multiplexed UDP stream   ) ----------  |- port=2000
           |          /  ----------------------------   \          |
           |   id=3  /                                   \  id=3   |
           |  ------                                       ------  |
           +--                                                   --+
```
When a socket is accepted on the listener side (the Receiver in this example), 
srt-test-multiplex will search for the resource ID among the registered resources 
(input/output URIs) and set an ID that matches the one on the caller side. If 
the resource is not found, the connection is closed immediately. 

The srt-test-multiplex program works the same way for connections initiated by a 
caller or a listener.
