# srt-file-transmit

The `srt-file-transmit` tool is a tool for transmitting files over SRT.

You need:

 - a file to transmit
 - a destination to store it into
 - this application run on both sides, one sending, one receiving

## Introduction

The `srt-file-transmit` application will transmit your file over the SRT connection,
the application on the other side will receive it and store to the desired location.
Both caller-listener and rendezvous arrangement of the connection are possible
and in whatever direction.

The `streamid` socket option will be used to pass the filename to the other side
so that it is either written with this name or matched with the filename internally.

The application the will be sending a file should use the file path as source and
SRT URI as a destination, and vice versa for receiving.

## Caller mode

If you use sender in caller mode, then the caller SRT URI destination should be
specified, and the "root name" from the file path will be set to `streamid`.
This will allow the listener to use it when writing.

If a receiver is used as a caller, then the destination filepath's rootname
will be also passed as `streamid` so that the listener receiver can pick up the
file by name.

In caller mode you must specify the full filename path of the received or sent
file.

## Listener mode

If you use sender in listener mode, then you start it with specifying either the
full filename path, or only the directory where the file is located; in the latter
case the filename will be tried from the `streamid` option extracted from the
connected socket (as set by the other side's caller). If the full filename was
specified, it must match the rootname extraced from this option, or otherwise
transmission will not be done.

If you use receiver in listener mode, then you start it with specifying either
the full filename path, or just the directory. In the latter case the root name
will be extracted from `streamid` socket option, and this one will be transmitted.

## Usage

```
srt-file-transmit [options] <input-uri> <output-uri>
```

