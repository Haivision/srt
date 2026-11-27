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

// 4. On shutdown, close the socket: once srt_close() has returned, no
//    callback runs and the queue can be destroyed.
srt_close(sock);
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
* SRT calls the send callback from its sending and receiving worker threads
  and from application threads (for example `srt_connect()`, `srt_close()`),
  possibly all at once; the queue's send lock is what serializes those calls
  into the application's single contiguous-buffer send callback. No callback
  may call an SRT function that can wait (`srt_close()`, `srt_connect()`, a
  blocking send, receive or accept): it deadlocks - see
  [Broken medium and callback lifetime](../docs/API/API-functions.md#broken-medium-and-callback-lifetime).
* `detach()` is how the application breaks its medium on purpose: `push()`
  and the send callback then fail, and the SRT side sees a broken medium. It
  blocks until an in-flight send callback returns, and it is not needed
  before destruction.
* The object must outlive the last `srt_close()` of every socket bound with
  its config - the config passes the object to SRT as the callbacks' opaque
  value. Once that close has returned, no callback runs and the object can
  be destroyed. If several threads close those sockets at once, wait for all
  of them: a close that returned earlier does not cover the others.

See `srttransport.h` for the complete reference.
