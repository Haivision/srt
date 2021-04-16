

### srt_setsockopt(..)

1. s_UDTUnited.locateSocket: m_GlobControlLock (lock, unlock)
2. CUDT::setOpt: 
   - Read m_bBroken, m_bClosing - no lock
   - ScopedLock (m_ConnectionLock)
   - ScopedLock (m_SendLock)
   - ScopedLock (m_RecvLock)
   - Reading m_bConnected, m_bConnecting, m_bOpened
   - Change socket option value
   
   
### srt_connect(..)

1. s_UDTUnited.locateSocket: m_GlobControlLock (lock, unlock)
2. CUDTUnited::connectIn(..)
   - ScopedLock (m_ControlLock)
   - Read, write m_Status
   - CUDT::startConnect(..)
     - ScopedLock (m_ConnectionLock)
	   - m_RIDVectorLock: lock, unlock
	   - m_bConnecting: write
	   - if !bSynRecving return (non blocking mode)
	   - while (!m_bClosing)
	   
	   
	   
### srt_getsockstate

1. ScopedLock (m_GlobControlLock)
   - CUDTSocket::getStatus(): no lock. Accessing CUDT::m_bConnecting, m_bConnected, m_bBroken, m_Status
