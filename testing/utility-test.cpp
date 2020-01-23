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

#define SRT_TEST_CIRCULAR_BUFFER 1

#include <udt.h>
#include <utilities.h>
#include <common.h>
#include <packet.h>
#include <crypto.h>
#include <common.h>

void ShowDistance(int32_t s1, int32_t s2)
{
    using namespace std;

    cout << "s1=" << s1 << "s2=" << s2 << " DISTANCE:\n";
    cout << "seqcmp -> " << CSeqNo::seqcmp(s1, s2) << endl;
    cout << "seqoff -> " << CSeqNo::seqoff(s2, s1) << endl;
}

using namespace std;

// To test CircularBuffer
struct Double
{
    double d;
    size_t instance;
    static size_t sourceid;

    Double(): d(0.0)
    {
        instance = ++sourceid;
        cout << "(Double/" << instance << ": empty costruction)\n";
    }

    Double(double dd): d(dd)
    {
        instance = ++sourceid;
        cout << "(Double:/" << instance << " init construction:" << dd << ")\n";
    }

    Double(const Double& dd): d(dd.d)
    {
        instance = ++sourceid;
        cout << "(Double:/" << instance << " copy construction:" << dd.d << " object/" << dd.instance << ")\n";
    }

    operator double() { return d; }

    ~Double()
    {
        cout << "(Double:/" << instance << " destruction:" << d << ")\n";
    }

    void operator=(double dd)
    {
        cout << "(Double:/" << instance << " copy assignment:" << d << " -> " << dd << " value)\n";
        d = dd;
    }

    void operator=(const Double& dd)
    {
        cout << "(Double:/" << instance << " copy assignment:" << d << " -> " << dd.d << " object/" << dd.instance << ")\n";
        d = dd.d;
    }
};

size_t Double::sourceid = 0;


template <class Val> inline
void ShowCircularBuffer(const CircularBuffer<Val>& buf)
{
    cout << "SIZE: " << buf.size() << " FREE:" << buf.spaceleft() << " BEGIN:" << buf.m_xBegin << " END: " << buf.m_xEnd << endl;
    for (int i = 0; i < buf.size(); ++i)
    {
        Double v;
        if (buf.get(i, (v)))
            cout << "[" << i << "] = " << v << endl;
        else
            cout << "[" << i << "] EMPTY!\n";
    }
}

struct Add
{
    Double v;
    Add(const Double& vv): v(vv) {}
    void operator()(Double& accessed, bool isnew)
    {
        if (isnew)
            accessed = v;
        else
            accessed = Double(accessed.d + v.d);
    }
};

void TestCircularBuffer()
{
    // Create some odd number of elements in a circular buffer.

    CircularBuffer<Double> buf(7);

    cout << dec;

    // Now, add 3 elements to it and check if succeeded.
    buf.push(11.2);
    buf.push(12.3);
    buf.push(13.4);

    cout << "After adding 3 elements: size=" << buf.size() << " capacity=" << buf.capacity() << ":\n";
    ShowCircularBuffer(buf);

    cout << "Adding element at position 5:\n";
    if (!buf.set(5, 15.5))
        cout << "FAILED!!!\n";
    ShowCircularBuffer(buf);

    cout << "Adding element at position 7 (should fail):\n";
    if (buf.set(7, 10.0))
        cout << "added (ERROR!)\n";
    else
        cout << "failed. (OK)\n";
    ShowCircularBuffer(buf);

    cout << "Dropping first 2 elements:\n";
    buf.drop(2);
    ShowCircularBuffer(buf);

    cout << "Adding again element at position 6 (should roll):\n";
    buf.set(6, 22.1);
    ShowCircularBuffer(buf);

    cout << "Adding element at existing position 2 (overwrite):\n";
    buf.set(2, 33.1);
    ShowCircularBuffer(buf);

    cout << "Adding element at existing position 3 (no overwrite):\n";
    buf.set(3, 44.4, false);
    ShowCircularBuffer(buf);

    cout << "Dropping first 4 positions:\n";
    buf.drop(4);
    ShowCircularBuffer(buf);

    cout << "Pushing 1 until there is capacity:\n";
    int i = 0;
    while (buf.push(1) != -1)
    {
        cout << "Pushed, begin=" << buf.m_xBegin << " end=" << buf.m_xEnd << endl;
        ++i;
    }
    cout << "Done " << i << " operations, buffer:\n";
    ShowCircularBuffer(buf);

    cout << "Updating value at position 5:\n";
    buf.update(5, Add(3.33));
    ShowCircularBuffer(buf);

    int offset = 9;
    cout << "Forced adding at position 9 with dropping (capacity: " << buf.capacity() << "):\n";
    // State we already know it has failed. Calculate drop size.
    int dropshift = offset - (buf.capacity() - 1); // buf.capacity()-1 is the latest position
    offset -= dropshift;
    cout << "Need to drop: " << dropshift << " New offset:" << offset << endl;
    if (dropshift > 0)
    {
        buf.drop(dropshift);
        cout << "AFTER DROPPING:\n";
        ShowCircularBuffer(buf);
        buf.set(offset, 99.1, true);
    }
    else
    {
        cout << "NEGATIVE DROP!\n";
    }
    ShowCircularBuffer(buf);
    int size = buf.size();

    cout << "Dropping rest of the items:\n";
    buf.drop(size-1);

    cout << "Buffer empty: " << boolalpha << buf.empty() << endl;
    ShowCircularBuffer(buf);

    cout << "DONE.\n";
}

int main()
{
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

    cout << "TESTING: CircularBuffer\n";
    TestCircularBuffer();

    cout << "-------------------------------\n";
    cout << "SEQUENCES:\n";
    int32_t s1 = 100, s2 = 200;
    ShowDistance(s1, s2);

    cout << "GO BACK BY -150:\n";
    s1 = CSeqNo::decseq(s1, 150);
    s2 = CSeqNo::decseq(s2, 150);
    ShowDistance(s1, s2);

    return 0;
}
