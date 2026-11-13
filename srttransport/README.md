# srttransport

An optional owner-side helper for the SRT [virtual transport](../docs/API/API-functions.md#srt_bind_virtual):
a push queue that implements the three callbacks of
`SRT_VirtualTransportConfig`, so that an application owning a datagram medium
(for example WebTransport) gets back an easy push API instead of writing the
`send`/`waitread`/`deliver` callback contract by hand.

The helper is built as a static library together with
`-DENABLE_VIRTUAL_TRANSPORT=ON` and is NOT part of the SRT library proper: it
uses only the public types from `srt.h`, calls no `srt_*` function, and needs
nothing from `libsrt` to be linked against it. Being outside of srtcore, it is
also free of srtcore's portability constraints (C++98 compatibility, the
`srt::sync` wrappers) and may use the standard C++ library directly.

The library is currently a build-tree component: it is built together with the
rest of the project (only with `-DENABLE_VIRTUAL_TRANSPORT=ON`) but has no
install rules, so `make install` does not deliver it. Consume it straight from
the build tree - the static library `libsrttransport.a` at the build root plus
the `srttransport/srttransport.h` header - or vendor the two source files into
your project. Install rules may be added later if a need emerges.

## Usage

```cpp
#include <srt/srt.h>
#include "srttransport.h"

// 1. A plain send callback: the medium accepts one contiguous buffer.
int MySend(void* opaq, const char* data, int len,
           const struct sockaddr* dst, int dstlen)
{
    MyMedium* medium = (MyMedium*)opaq;
    return medium->sendDatagram(data, len);  // bytes accepted, or -1 to drop
}

// 2. Create the queue around the callback, and bind a socket with its config.
srt::SRTTransportQueue queue(&MySend, &medium);
SRTSOCKET sock = srt_create_socket();
SRT_VirtualTransportConfig cfg = queue.getConfig(/*initial_rtt_ms=*/0);
sockaddr_in label = ...;                    // only a label, nothing OS-bound
srt_bind_virtual(sock, (sockaddr*)&label, sizeof label, &cfg);
srt_listen(sock, 5);                        // or srt_connect(sock, ...)

// 3. Push every datagram the medium receives; SRT picks them up.
//    (any thread; exactly one datagram per call)
queue.push(datagram, len, (sockaddr*)&src, sizeof src);

// 4. On shutdown, break the transport and wait for in-flight sends.
srt_close(sock);
queue.detach();
```

Notes:

* The queue is bounded: when full, `push()` drops the datagram and returns -1,
  like a UDP socket dropping into a full kernel buffer. Both bounds (the number
  of pending datagrams and the maximum datagram size) are constructor
  parameters.
* An application feeding a live medium should monitor `dropped()`: it counts
  exactly the overflow drops - the benign, UDP-like losses - and an unattended
  overflow is silent packet loss, like a UDP socket whose kernel buffer stays
  full.
* Every `push()` must carry a source address: it is the label SRT dispatches
  the datagram by, and the channel discards a datagram reported without one -
  a source-less push is rejected with -1.
* SRT may call the send callback concurrently from more than one internal
  thread; the queue's send lock is what serializes those calls into the
  application's single contiguous-buffer send callback.
* `detach()` makes `push()` and the send callback fail and makes the SRT side
  see a broken medium; it blocks until an in-flight send callback returns.
* The object must outlive every socket bound with its config - the config
  passes the object to SRT as the callbacks' opaque value. After the final
  `srt_close()` no new callback invocation is started, and a single closing
  thread also has every invocation finished by then; when the sockets of one
  multiplexer are closed from several threads at once, `detach()` before
  destruction - one close can return while another still joins an invocation
  in flight.

See `srttransport.h` for the complete reference.
