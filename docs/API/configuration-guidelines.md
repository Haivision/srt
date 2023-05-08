# Configuration Guidelines

## Receiver Buffer Size

The receiver buffer can be configured with the [`SRTO_RCVBUF`](./API-socket-options.md#SRTO_RCVBUF) socket option.
Buffer size in bytes is expected to be passed in the `optval` argument of the `srt_setsockopt(..)` function.
However, internally the value will be converted into the number of packets stored in the receiver buffer.

The allowed value of `SRTO_RCVBUF` is also limited by the value of the flow control window size [`SRTO_FC`](./API-socket-options.md#SRTO_FC) socket option.
See issue [#700](https://github.com/Haivision/srt/issues/700).

The default flow control window size is 25600 packets. It is approximately:

- **270 Mbits** of payload in the default live streaming configuration with an SRT payload size of **1316 bytes**;
- **300 Mbits** of payload in the default file transfer configuration with an SRT payload size of **1456 bytes**.

The default receiver buffer size is 8192 packets. It is approximately: 
- **86 Mbits** of payload with the effective SRT payload size of **1316 bytes**.

### Setting Receiver Buffer Size

When the `SRTO_RCVBUF` option value is set using the `srt_setsockopt(..)` function,
the provided size in bytes is internally converted to the corresponding size in packets.
The size of a cell for a single packet in the buffer is defined by the
`SRTO_MSS` option, which is 1500 by default.  This value, decreased by 28 in
the case of IPv4 (20 bytes for the IPv4 header and 8 bytes for the UDP header), gives 1472
bytes per packet to be allocated. The actual memory occupied by the receiver
buffer will be a multiple of that value. For the default 8192 packets it
will be 11776 kB (11.5 MB).

Note that every cell has 16 bytes for the SRT header. The remaining space
is for the payload.

As already mentioned, the maximum allowed size of the receiver buffer is limited by the value of `SRTO_FC`.

The following function returns the configured buffer size in packets depending on the SRTO_RCVBUF, SRTO_MSS and SRTO_FC values set:

```c++
int getRbufSizePkts(int SRTO_RCVBUF, int SRTO_MSS, int SRTO_FC)
{
    // UDP/IPv4 header size is assumed to be 28 bytes
    // 20 bytes IPv4 + 8 bytes of UDP
    const int UDP_IPv4_HDR= 28;
    const int pkts = (rbuf_size / (SRTO_MSS - UDP_IPv4_HDR));

    return min(pkts, SRTO_FC);
}
```

If the value of `SRTO_RCVBUF` in packets exceeds `SRTO_FC`, then it is silently set to the value in bytes corresponding to `SRTO_FC`.
Therefore, to set higher values of `SRTO_RCVBUF` the value of `SRTO_FC` must be increased first.

### Calculating Target Size in Packets

The minimum size of the receiver buffer in packets can be calculated as follows:

`pktsRBufSize = bps / 8 × (RTTsec + latency_sec) / bytePayloadSize`

where

- `bps` is the payload bitrate of the stream in bits per second;
- `RTTsec` is the RTT of the network connection in seconds;

- `bytePayloadSize` is the expected size of the payload of the SRT data packet.

If the whole remainder of the MTU is expected to be used, payload size is calculated as follows: 

`bytePayloadSize = MSS - UDP_IPv4_HDR - SRT_HDR`

where

- `MSS`: Maximum Segment Size (size of the MTU); see `SRTO_MSS` (default: 1500)
- `UDP_IPv4_HDR`: 20 bytes for IPv4 + 8 bytes for UDP
- `SRT_HDR`: 16 bytes of SRT header (belonging to the user space)

### Calculating Target Size to Set

To determine the value to pass in `srt_setsockopt(..)` with `SRTO_RCVBUF`
the size in packets `pktsRBufSize` must be converted to the size in bytes
assuming the internal conversion of the `srt_setsockopt(..)` function.

The target size of the payload stored by the receiver buffer would be: 

`SRTO_RCVBUF = pktsRBufSize × (SRTO_MSS - UDPHDR_SIZE)`  

where

- `UDPHDR_SIZE` = 28 (20 bytes IPv4, 8 bytes of UDP)
- `SRTO_MSS` is the corresponding socket option value at the moment of setting `SRTO_RCVBUF`.


### Summing Up


```c++
auto CalculateTargetRBufSize(int msRTT, int bpsRate, int bytesPayloadSize, int msLatency, int SRTO_MSS)
{
    const int UDPHDR_SIZE = 28;
    const long long targetPayloadBytes = static_cast<long long>(msLatency + msRTT / 2) * bpsRate / 1000 / 8;
    const long long targetNumPackets   = targetPayloadBytes / bytesPayloadSize;
    const long long targetSizeValue    = targetNumPackets * (SRTO_MSS - UDPHDR_SIZE);
    return {targetNumPackets, targetSizeValue};
}

// Configuring

const auto [fc, rcvbuf] = CalculateTargetRBufSize(msRTT, bpsRate, bytesPayloadSize, SRTO_RCVLATENCY, SRTO_MSS);

int optval = fc;
int optlen = sizeof optval;
srt_setsockopt(sock, 0, SRTO_FC, (void*) &optval, optlen);

optval = rcvbuf;
srt_setsockopt(sock, 0, SRTO_RCVBUF, (void*) &optval, optlen);
```
