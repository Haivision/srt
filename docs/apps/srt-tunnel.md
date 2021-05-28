SRT Tunnel
==========

Purpose
-------

SRT Tunnel is a typical tunnelling application, that is, it simply passes the
transmission from a given endpoint to another endpoint in both directions.

Tunnels can be also "chained", that is, there can be more than one tunnel on
the way between the real peers.

This tunnel application can use both TCP and SRT as endpoint type and the
typically predicted use case is to hand over the transmission for SRT for a
longer distance, leaving TCP close to the caller and listener locations:

```
 <TCP client> --> <Tunnel: TCP->SRT> --> ...
            ....
          (long distance)
           ....
  --> <Tunnel: SRT->TCP> --> <TCP server>
```

Usage
-----

The `srt-tunnel` command line accepts two argument, beside the options:
* Listener: the URI at which this tunnel should await connections
* Caller: where this tunnel should connect when its Listener connected

Options:

* -ll, -loglevel: logging level, default:error
* -lf, -logfa: logging Functional Area enabled
* -c, -chunk: piece of data amount read at once, default=4096 bytes
* -v, -verbose: display transmission details
* -s, -skipflush: exit without waiting for data to complete

