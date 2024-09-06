/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 02/12/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

//////////////////////////////////////////////////////////////////////////////
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                        Packet Header                          |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                                                               |
//   ~              Data / Control Information Field                 ~
//   |                                                               |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |0|                        Sequence Number                      |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |ff |o|kf |r|               Message Number                      |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                          Time Stamp                           |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                     Destination Socket ID                     |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//   bit 0:
//      0: Data Packet
//      1: Control Packet
//   bit ff:
//      11: solo message packet
//      10: first packet of a message
//      01: last packet of a message
//   bit o:
//      0: in order delivery not required
//      1: in order delivery required
//   bit kf: HaiCrypt Key Flags
//      00: not encrypted
//      01: encrypted with even key
//      10: encrypted with odd key
//   bit r: retransmission flag (set to 1 if this packet was sent again)
//
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |1|            Type             |             Reserved          |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                       Additional Info                         |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                          Time Stamp                           |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                     Destination Socket ID                     |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//   bit 1-15: Message type -- see @a UDTMessageType
//      0: Protocol Connection Handshake (UMSG_HANDSHAKE}
//              Add. Info:    Undefined
//              Control Info: Handshake information (see @a CHandShake)
//      1: Keep-alive (UMSG_KEEPALIVE)
//              Add. Info:    Undefined
//              Control Info: None
//      2: Acknowledgement (UMSG_ACK)
//              Add. Info:    The ACK sequence number
//              Control Info: The sequence number to which (but not include) all the previous packets have beed received
//              Optional:     RTT
//                            RTT Variance
//                            available receiver buffer size (in bytes)
//                            advertised flow window size (number of packets)
//                            estimated bandwidth (number of packets per second)
//      3: Negative Acknowledgement (UMSG_LOSSREPORT)
//              Add. Info:    Undefined
//              Control Info: Loss list (see loss list coding below)
//      4: Congestion/Delay Warning (UMSG_CGWARNING)
//              Add. Info:    Undefined
//              Control Info: None
//      5: Shutdown (UMSG_SHUTDOWN)
//              Add. Info:    Undefined
//              Control Info: None
//      6: Acknowledgement of Acknowledement (UMSG_ACKACK)
//              Add. Info:    The ACK sequence number
//              Control Info: None
//      7: Message Drop Request (UMSG_DROPREQ)
//              Add. Info:    Message ID
//              Control Info: first sequence number of the message
//                            last seqeunce number of the message
//      8: Error Signal from the Peer Side (UMSG_PEERERROR)
//              Add. Info:    Error code
//              Control Info: None
//      0x7FFF: Explained by bits 16 - 31 (UMSG_EXT)
//
//   bit 16 - 31:
//      This space is used for future expansion or user defined control packets.
//
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |1|                 Sequence Number a (first)                   |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |0|                 Sequence Number b (last)                    |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |0|                 Sequence Number (single)                    |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//   Loss List Field Coding:
//      For any consecutive lost seqeunce numbers that the differnece between
//      the last and first is more than 1, only record the first (a) and the
//      the last (b) sequence numbers in the loss list field, and modify the
//      the first bit of a to 1.
//      For any single loss or consecutive loss less than 2 packets, use
//      the original sequence numbers in the field.

#include "platform_sys.h"
#include <cstddef>
#include <cstring>
#include "packet.h"
#include "handshake.h"
#include "logging.h"
#include "handshake.h"

namespace srt_logging
{
extern Logger inlog;
}
using namespace srt_logging;

namespace srt {

// Set up the aliases in the constructure
CPacket::CPacket()
    : m_nHeader() // Silences GCC 12 warning "used uninitialized".
    , m_extra_pad()
    , m_data_owned(false)
    , m_pcData((char*&)(m_PacketVector[PV_DATA].dataRef()))
{
    m_nHeader.clear();

    // The part at PV_HEADER will be always set to a builtin buffer
    // containing SRT header.
    m_PacketVector[PV_HEADER].set(m_nHeader.raw(), HDR_SIZE);

    // The part at PV_DATA is zero-initialized. It should be
    // set (through m_pcData and setLength()) to some externally
    // provided buffer before calling CChannel::sendto().
    m_PacketVector[PV_DATA].set(NULL, 0);
}

char* CPacket::getData()
{
    return (char*)m_PacketVector[PV_DATA].dataRef();
}

void CPacket::allocate(size_t alloc_buffer_size)
{
    if (m_data_owned)
    {
        if (getLength() == alloc_buffer_size)
            return; // already allocated

        // Would be nice to reallocate; for now just allocate again.
        delete[] m_pcData;
    }
    m_PacketVector[PV_DATA].set(new char[alloc_buffer_size], alloc_buffer_size);
    m_data_owned = true;
}

void CPacket::deallocate()
{
    if (m_data_owned)
        delete[](char*) m_PacketVector[PV_DATA].data();
    m_PacketVector[PV_DATA].set(NULL, 0);
    m_data_owned = false;
}

char* CPacket::release()
{
    // When not owned, release returns NULL.
    char* buffer = NULL;
    if (m_data_owned)
    {
        buffer       = getData();
        m_data_owned = false;
    }

    deallocate(); // won't delete because m_data_owned == false
    return buffer;
}

CPacket::~CPacket()
{
    // PV_HEADER is always owned, PV_DATA may use a "borrowed" buffer.
    // Delete the internal buffer only if it was declared as owned.
    deallocate();
}

size_t CPacket::getLength() const
{
    return m_PacketVector[PV_DATA].size();
}

void CPacket::setLength(size_t len)
{
    m_PacketVector[PV_DATA].setLength(len);
}

void CPacket::setLength(size_t len, size_t cap)
{
   SRT_ASSERT(len <= cap);
   setLength(len);
   m_zCapacity = cap;
}

#if ENABLE_HEAVY_LOGGING
// Debug only
static std::string FormatNumbers(UDTMessageType pkttype, const int32_t* lparam, void* rparam, const size_t size)
{
    // This may be changed over time, so use special interpretation
    // only for certain types, and still display all data, no matter
    // if it is expected to provide anything or not.
    std::ostringstream out;

    out << "ARG=";
    if (lparam)
        out << *lparam;
    else
        out << "none";

    if (size == 0)
    {
        out << " [no data]";
        return out.str();
    }
    else if (!rparam)
    {
        out << " [ {" << size << "} ]";
        return out.str();
    }

    bool interp_as_seq = (pkttype == UMSG_LOSSREPORT || pkttype == UMSG_DROPREQ);
    bool display_dec = (pkttype == UMSG_ACK || pkttype == UMSG_ACKACK || pkttype == UMSG_DROPREQ);

    out << " [ ";

    // Will be effective only for hex/oct.
    out << std::showbase;

    const size_t size32 = size/4;
    for (size_t i = 0; i < size32; ++i)
    {
        int32_t val = ((int32_t*)rparam)[i];
        if (interp_as_seq)
        {
            if (val & LOSSDATA_SEQNO_RANGE_FIRST)
                out << "<" << (val & (~LOSSDATA_SEQNO_RANGE_FIRST)) << ">";
            else
                out << val;
        }
        else
        {
            if (!display_dec)
            {
                out << std::hex;
                out << val << "/";
                out << std::dec;
            }
            out << val;

        }
        out << " ";
    }

    out << "]";
    return out.str();
}
#endif

void CPacket::pack(UDTMessageType pkttype, const int32_t* lparam, void* rparam, size_t size)
{
    // Set (bit-0 = 1) and (bit-1~15 = type)
    setControl(pkttype);
    HLOGC(inlog.Debug, log << "pack: type=" << MessageTypeStr(pkttype) << " " << FormatNumbers(pkttype, lparam, rparam, size));

    // Set additional information and control information field
    switch (pkttype)
    {
    case UMSG_ACK: // 0010 - Acknowledgement (ACK)
        // ACK packet seq. no.
        if (NULL != lparam)
            m_nHeader[SRT_PH_MSGNO] = *lparam;

        // data ACK seq. no.
        // optional: RTT (microsends), RTT variance (microseconds) advertised flow window size (packets), and estimated
        // link capacity (packets per second)
        m_PacketVector[PV_DATA].set(rparam, size);

        break;

    case UMSG_ACKACK: // 0110 - Acknowledgement of Acknowledgement (ACK-2)
        // ACK packet seq. no.
        m_nHeader[SRT_PH_MSGNO] = *lparam;

        // control info field should be none
        // but "writev" does not allow this
        m_PacketVector[PV_DATA].set((void*)&m_extra_pad, 4);

        break;

    case UMSG_LOSSREPORT: // 0011 - Loss Report (NAK)
        // loss list
        m_PacketVector[PV_DATA].set(rparam, size);

        break;

    case UMSG_CGWARNING: // 0100 - Congestion Warning
        // control info field should be none
        // but "writev" does not allow this
        m_PacketVector[PV_DATA].set((void*)&m_extra_pad, 4);

        break;

    case UMSG_KEEPALIVE: // 0001 - Keep-alive
        if (lparam)
        {
            // XXX EXPERIMENTAL. Pass the 32-bit integer here.
            m_nHeader[SRT_PH_MSGNO] = *lparam;
        }
        // control info field should be none
        // but "writev" does not allow this
        m_PacketVector[PV_DATA].set((void*)&m_extra_pad, 4);

        break;

    case UMSG_HANDSHAKE: // 0000 - Handshake
        // control info filed is handshake info
        m_PacketVector[PV_DATA].set(rparam, size);

        break;

    case UMSG_SHUTDOWN: // 0101 - Shutdown
        // control info field should be none
        // but "writev" does not allow this
        m_PacketVector[PV_DATA].set((void*)&m_extra_pad, 4);

        break;

    case UMSG_DROPREQ: // 0111 - Message Drop Request
        // msg id
        m_nHeader[SRT_PH_MSGNO] = *lparam;

        // first seq no, last seq no
        m_PacketVector[PV_DATA].set(rparam, size);

        break;

    case UMSG_PEERERROR: // 1000 - Error Signal from the Peer Side
        // Error type
        m_nHeader[SRT_PH_MSGNO] = *lparam;

        // control info field should be none
        // but "writev" does not allow this
        m_PacketVector[PV_DATA].set((void*)&m_extra_pad, 4);

        break;

    case UMSG_EXT: // 0x7FFF - Reserved for user defined control packets
        // for extended control packet
        // "lparam" contains the extended type information for bit 16 - 31
        // "rparam" is the control information
        m_nHeader[SRT_PH_SEQNO] |= *lparam;

        if (NULL != rparam)
        {
            m_PacketVector[PV_DATA].set(rparam, size);
        }
        else
        {
            m_PacketVector[PV_DATA].set((void*)&m_extra_pad, 4);
        }

        break;

    default:
        break;
    }
}

void CPacket::toNetworkByteOrder()
{
    // The payload of data packet should remain in network byte order.
    if (isControl())
    {
        HtoNLA((uint32_t*) m_pcData, (const uint32_t*) m_pcData, getLength() / 4);
    }

    // Convert packet header independent of packet type.
    uint32_t* p = m_nHeader;
    HtoNLA(p, p, 4);
}

void CPacket::toHostByteOrder()
{
    // Convert packet header independent of packet type.
    uint32_t* p = m_nHeader;
    NtoHLA(p, p, 4);

	// The payload of data packet should remain in network byte order.
    if (isControl())
    {
        NtoHLA((uint32_t*)m_pcData, (const uint32_t*)m_pcData, getLength() / 4);
    }
}

IOVector* CPacket::getPacketVector()
{
    return m_PacketVector;
}

UDTMessageType CPacket::getType() const
{
    return UDTMessageType(SEQNO_MSGTYPE::unwrap(m_nHeader[SRT_PH_SEQNO]));
}

int CPacket::getExtendedType() const
{
    return SEQNO_EXTTYPE::unwrap(m_nHeader[SRT_PH_SEQNO]);
}

int32_t CPacket::getAckSeqNo() const
{
    // read additional information field
    // This field is used only in UMSG_ACK and UMSG_ACKACK,
    // so 'getAckSeqNo' symbolically defines the only use of it
    // in case of CONTROL PACKET.
    return m_nHeader[SRT_PH_MSGNO];
}

uint16_t CPacket::getControlFlags() const
{
    // This returns exactly the "extended type" value,
    // which is not used at all in case when the standard
    // type message is interpreted. This can be used to pass
    // additional special flags.
    return SEQNO_EXTTYPE::unwrap(m_nHeader[SRT_PH_SEQNO]);
}

PacketBoundary CPacket::getMsgBoundary() const
{
    return PacketBoundary(MSGNO_PACKET_BOUNDARY::unwrap(m_nHeader[SRT_PH_MSGNO]));
}

bool CPacket::getMsgOrderFlag() const
{
    return 0 != MSGNO_PACKET_INORDER::unwrap(m_nHeader[SRT_PH_MSGNO]);
}

int32_t CPacket::getMsgSeq(bool has_rexmit) const
{
    if (has_rexmit)
    {
        return MSGNO_SEQ::unwrap(m_nHeader[SRT_PH_MSGNO]);
    }
    else
    {
        return MSGNO_SEQ_OLD::unwrap(m_nHeader[SRT_PH_MSGNO]);
    }
}

bool CPacket::getRexmitFlag() const
{
    return 0 != MSGNO_REXMIT::unwrap(m_nHeader[SRT_PH_MSGNO]);
}

void CPacket::setRexmitFlag(bool bRexmit)
{
    const int32_t clr_msgno = m_nHeader[SRT_PH_MSGNO] & ~MSGNO_REXMIT::mask;
    m_nHeader[SRT_PH_MSGNO] = clr_msgno | MSGNO_REXMIT::wrap(bRexmit? 1 : 0);
}

EncryptionKeySpec CPacket::getMsgCryptoFlags() const
{
    return EncryptionKeySpec(MSGNO_ENCKEYSPEC::unwrap(m_nHeader[SRT_PH_MSGNO]));
}

// This is required as the encryption/decryption happens in place.
// This is required to clear off the flags after decryption or set
// crypto flags after encrypting a packet.
void CPacket::setMsgCryptoFlags(EncryptionKeySpec spec)
{
    int32_t clr_msgno       = m_nHeader[SRT_PH_MSGNO] & ~MSGNO_ENCKEYSPEC::mask;
    m_nHeader[SRT_PH_MSGNO] = clr_msgno | EncryptionKeyBits(spec);
}

uint32_t CPacket::getMsgTimeStamp() const
{
    // SRT_DEBUG_TSBPD_WRAP used to enable smaller timestamps for faster testing of how wraparounds are handled
    return (uint32_t)m_nHeader[SRT_PH_TIMESTAMP] & TIMESTAMP_MASK;
}

CPacket* CPacket::clone() const
{
    CPacket* pkt = new CPacket;
    memcpy((pkt->m_nHeader), m_nHeader, HDR_SIZE);
    pkt->allocate(this->getLength());
    SRT_ASSERT(this->getLength() == pkt->getLength());
    memcpy((pkt->m_pcData), m_pcData, this->getLength());
    pkt->m_DestAddr = m_DestAddr;

    return pkt;
}

// Useful for debugging
std::string PacketMessageFlagStr(uint32_t msgno_field)
{
    using namespace std;

    stringstream out;

    static const char* const boundary[] = {"PB_SUBSEQUENT", "PB_LAST", "PB_FIRST", "PB_SOLO"};
    static const char* const order[]    = {"ORD_RELAXED", "ORD_REQUIRED"};
    static const char* const crypto[]   = {"EK_NOENC", "EK_EVEN", "EK_ODD", "EK*ERROR"};
    static const char* const rexmit[]   = {"SN_ORIGINAL", "SN_REXMIT"};

    out << boundary[MSGNO_PACKET_BOUNDARY::unwrap(msgno_field)] << " ";
    out << order[MSGNO_PACKET_INORDER::unwrap(msgno_field)] << " ";
    out << crypto[MSGNO_ENCKEYSPEC::unwrap(msgno_field)] << " ";
    out << rexmit[MSGNO_REXMIT::unwrap(msgno_field)];

    return out.str();
}

inline void SprintSpecialWord(std::ostream& os, int32_t val)
{
    if (val & LOSSDATA_SEQNO_RANGE_FIRST)
        os << "<" << (val & (~LOSSDATA_SEQNO_RANGE_FIRST)) << ">";
    else
        os << val;
}

#if ENABLE_LOGGING
std::string CPacket::Info()
{
    std::ostringstream os;
    os << "TARGET=@" << id() << " ";

    if (isControl())
    {
        os << "CONTROL: size=" << getLength() << " type=" << MessageTypeStr(getType(), getExtendedType());

        if (getType() == UMSG_HANDSHAKE)
        {
            os << " HS: ";
            // For handshake we already have a parsing method
            CHandShake hs;
            hs.load_from(m_pcData, getLength());
            os << hs.show();
        }
        else
        {
            // This is a value that some messages use for some purposes.
            // The "ack seq no" is one of the purposes, used by UMSG_ACK and UMSG_ACKACK.
            // This is simply the SRT_PH_MSGNO field used as a message number in data packets.
            os << " ARG: 0x";
            os << std::hex << getAckSeqNo() << " ";
            os << std::dec << getAckSeqNo();

            // It would be nice to see the extended packet data, but this
            // requires strictly a message-dependent interpreter. So let's simply
            // display all numbers in the array with the following restrictions:
            // - all data contained in the buffer are considered 32-bit integer
            // - sign flag will be cleared before displaying, with additional mark
            size_t   wordlen = getLength() / 4; // drop any remainder if present
            int32_t* array   = (int32_t*)m_pcData;
            os << " [ ";
            for (size_t i = 0; i < wordlen; ++i)
            {
                SprintSpecialWord(os, array[i]);
                os << " ";
            }
            os << "]";
        }
    }
    else
    {
        // It's hard to extract the information about peer's supported rexmit flag.
        // This is only a log, nothing crucial, so we can risk displaying incorrect message number.
        // Declaring that the peer supports rexmit flag cuts off the highest bit from
        // the displayed number.
        os << "DATA: size=" << getLength() << " " << BufferStamp(m_pcData, getLength()) << " #" << getMsgSeq(true)
           << " %" << getSeqNo() << " " << MessageFlagStr();
    }

    return os.str();
}
#endif

} // end namespace srt
