## CPacket To Fix

- [ ] `CSndQueue::sendto(...)` should return send result.

- [ ] `CPacket::m_iID` - get rid if this reference shit.

- [ ] `CPacket::__pad` delete.

- [ ] `CChannel::sendto(...)` htonl should perform CPacket function

- [ ] Make constant or even delete `addrsize` variable `CChannel::sendto(...)`

      int addrsize = m_iSockAddrSize;
      int res = ::WSASendTo(m_iSocket, (LPWSABUF)packet.m_PacketVector, 2, &size, 0, addr, addrsize, NULL, NULL);

- [ ] Take a look at SRT_ENABLE_SYSTEMBUFFER_TRACE

- [ ] `SRTO_UDP_SNDBUF` use system default???

- [ ] WSASocket instead of `::sock`?

## To Read

* WinSock2 Story: [link](https://www.tenouk.com/Winsock/Winsock2story.html)
* I/O Completion Ports: [link](https://docs.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports?redirectedfrom=MSDN)
* WinSock I/O Strategies: [link](https://tangentsoft.net/wskfaq/articles/io-strategies.html)
* WinSock 2 Advanced I/O methods: [link](https://www.winsocketdotnetworkprogramming.com/winsock2programming/winsock2advancediomethod5e.html)

## References

* High-performance Windows Sockets Applications [link](https://docs.microsoft.com/en-us/windows/win32/winsock/high-performance-windows-sockets-applications-2)

## TODO

- [ ] Out of order flag
- [ ] WinSock Event tracing: [link](https://docs.microsoft.com/en-us/windows/win32/winsock/winsock-tracing)
- [ ] Edge-triggered epoll example: [link](https://github.com/eklitzke/epollet)

```
if (res == SOCKET_ERROR) {
    LOGC(mglog.Error, log << CONID() << "WSASendTo failed with error: " << WSAGetLastError()
    << " seqno: " << seqno << " msgno: " << msgno);
}
```