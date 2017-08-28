/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>
 */

#include <iostream>
#include <string>
#include <iomanip>

#include <udt.h>
#include <utilities.h>
#include <packet.h>
#include <crypto.h>

int main()
{
    using namespace std;

    cout << "PacketBoundary: " << hex << MSGNO_PACKET_BOUNDARY::mask << endl;

    cout << "PB_FIRST: " << hex << PacketBoundaryBits(PB_FIRST) << endl;
    cout << "PB_LAST: " << hex << PacketBoundaryBits(PB_LAST) << endl;
    cout << "PB_SOLO: " << hex << PacketBoundaryBits(PB_SOLO) << endl;

    cout << "inorder: " << hex << MSGNO_PACKET_INORDER::mask << " (1 << " << dec << MSGNO_PACKET_INORDER::offset << ")" << endl;
    cout << "msgno-seq mask: " << hex << MSGNO_SEQ::mask << endl;
    cout << "3 wrapped into enckeyspec: " << hex << setw(8) << setfill('0') << MSGNO_ENCKEYSPEC::wrap(3) << " - mask: " << MSGNO_ENCKEYSPEC::mask << endl;

    cout << "SrtVersion test: 2.3.8 == 0x020308 -- SrtVersion(2, 3, 8) == 0x" << hex << setw(8) << setfill('0') << SrtVersion(2, 3, 8) << endl;

    cout << "SEQNO_CONTROL::mask: " << hex << SEQNO_CONTROL::mask << " SEQNO 0x80050000 has control = " << SEQNO_CONTROL::unwrap(0x80050000)
        << " type = " << SEQNO_MSGTYPE::unwrap(0x80050000) << endl;
    return 0;
}
