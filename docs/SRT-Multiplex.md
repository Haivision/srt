## srt-multiplex

**srt-multiplex** (formerly called "SIPLEX") is a sample program that can send 
multiple streams in one direction. This tool demonstrates two SRT features:
 - the ability to use a single UDP link (a source and destination pair 
 specified by IP address and port) for multiple SRT connections
 - the use of the `streamid` socket option to identify multiplexed resources

NOTE: due to changes in the common code that can't be properly handled in the
current form of srt-multiplex, this application is temporarily blocked. Instead
the `srt-test-multiplex` application was added with the same functionality,
although it's recommended for testing purposes only.

#### Usage

`srt-multiplex <SRT-URI> -i <INPUT-URI1>[id] <INPUT-URI2>[id]...`

  - Multiplexes data from one or more input URIs to transmit as an SRT stream. 
  The application reads multiple streams (INPUT URIs), each using a separate SRT 
  connection. All of the traffic from these connections is sent through the 
  same UDP link.

`srt-multiplex <SRT-URI> -o <OUTPUT-URI1>[id] <OUTPUT-URI2>[id]...`

  - Transmits data from a multiplexed SRT stream to the specified output URI(s).

An `<SRT URI>` can be identified as input or output using the **-i** or **-o** 
options. When `-i` is specified, the URIs provided are used as input, and will 
be output over the `<SRT URI>` socket. The reverse is true for any output URIs 
specified by `-o`.

Separate connections will be created for every specified URI, although all will 
be using the same UDP link. When SRT is in caller mode, the SRT socket created 
for transmitting data for a given URI will be set to the `streamid` socket option 
from this URI's `id` parameter. When SRT is in listener mode, the `streamid` 
option will already be set on the accepted socket, and will be matched with a 
URI that has the same value in its `id` parameter.

This `streamid` is the SRT socket option (`SRTO_STREAMID` in the API). The idea 
is that it can be set on a socket used for connecting. When a listener is 
getting an accepted socket for that connection, the `streamid` socket option 
can be read from it, with the result that it will be the same as was set on 
the caller side.

So, in caller mode, for every stream media URI (input or output) there will be
a separate SRT socket created. This socket will have its `socketid` option 
set to the value that is given by user as the `id` parameter attached to a 
particular URI. In listener mode this happens in the opposite direction — the 
value of the `streamid` option is extracted from the accepted socket, and then 
matched against all ids specified with the stream media URIs:
```
URI1?id=a --> s1(streamid=a).connect(remhost:2000)
URI2?id=b --> s2(streamid=b).connect(remhost:2000)
URI3?id=c --> s3(streamid=c).connect(remhost:2000)
```
And on the listener side:
```
(remhost:2000) -> accept --> s(SRT socket) --> in URI array find such that uri.id == s.streamid
```

#### Examples

  - **Sender:**  
    - `srt-multiplex srt://remhost:2000 -i udp://:5000?id=low udp://:6000?id=high`
  - **Receiver:**
    - `srt-multiplex srt://:2000 -o output-high.ts?id=high output-low.ts?id=low`

In this example a Sender is created which will connect to `remhost` port 2000 
using multiple SRT sockets, all of which will be using the same outgoing port. 
Here the outgoing port is automatically selected when connecting. Subsequent 
sockets will reuse that port. Alternatively you can enforce the outgoing port 
using the `port` parameter with the `<SRT URI>`.

  - **Sender:**  
    - `srt-multiplex srt://remhost:2000?port=5555 ...`

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
srt-multiplex will search for the resource ID among the registered resources 
(input/output URIs) and set an ID that matches the one on the caller side. If 
the resource is not found, the connection is closed immediately. 

The srt-multiplex program works the same way for connections initiated by a 
caller or a listener.
