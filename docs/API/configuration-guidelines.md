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

As already mentioned, the maximum allowed size of the receiver buffer is limited by the value of `SRTO_FC`.
When the `SRTO_RCVBUF` option value is set using the `srt_setsockopt(..)` function,
the provided size in bytes is internally converted to the corresponding size in packets
using the configured value of the `SRTO_MSS` option to estimate the maximum possible payload of a packet.

The following function returns the buffer size in packets:

```c++
int getRbufSizePkts(int SRTO_RCVBUF, int SRTO_MSS, int SRTO_FC)
{
    // UDP header size is assumed to be 28 bytes
    // 20 bytes IPv4 + 8 bytes of UDP
    const int UDPHDR_SIZE = 28;
    const int pkts = (rbuf_size / (SRTO_MSS - UDPHDR_SIZE));

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

`bytePayloadSize = MSS - 44`

where

- 44 is the size in bytes of an **IPv4** header: 
   - 20 bytes **IPv4** 
   - 8 bytes of UDP
   - 16 bytes of SRT packet header.

- `MSS` is the Maximum Segment Size (aka MTU); see `SRTO_MSS`.

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
