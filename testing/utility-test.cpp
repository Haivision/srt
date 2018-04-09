/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
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

    cout << "Creating array of bytes: 10, 11, 20, 25 - FormatBinaryString: ";
    uint8_t array[4] = { 10, 11, 20, 25 };
    cout << FormatBinaryString(array, 4) << endl;
    return 0;
}
