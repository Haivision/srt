#include <iostream>
#include <chrono>
#include <future>
#include <thread>
#include <condition_variable>
#include "gtest/gtest.h"
#define SRT_TEST_CIRCULAR_BUFFER
#include "api.h"
#include "common.h"

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
        cerr << "(Double/" << instance << ": empty costruction)\n";
    }

    Double(double dd): d(dd)
    {
        instance = ++sourceid;
        cerr << "(Double:/" << instance << " init construction:" << dd << ")\n";
    }

    Double(const Double& dd): d(dd.d)
    {
        instance = ++sourceid;
        cerr << "(Double:/" << instance << " copy construction:" << dd.d << " object/" << dd.instance << ")\n";
    }

    operator double() { return d; }

    ~Double()
    {
        cerr << "(Double:/" << instance << " destruction:" << d << ")\n";
    }

    void operator=(double dd)
    {
        cerr << "(Double:/" << instance << " copy assignment:" << d << " -> " << dd << " value)\n";
        d = dd;
    }

    void operator=(const Double& dd)
    {
        cerr << "(Double:/" << instance << " copy assignment:" << d << " -> " << dd.d << " object/" << dd.instance << ")\n";
        d = dd.d;
    }

    // Required for template-based gtest :(
    friend bool operator==(const Double& l, double r) { return l.d == r; }
    friend bool operator==(double l, const Double r) { return l == r.d; }
    bool operator == (const Double& r) { return d == r; }
};

size_t Double::sourceid = 0;


template <class Val> inline
void ShowCircularBuffer(const CircularBuffer<Val>& buf)
{
    cerr << "SIZE: " << buf.size() << " FREE:" << buf.spaceleft() << " BEGIN:" << buf.m_xBegin << " END: " << buf.m_xEnd << endl;
    for (int i = 0; i < buf.size(); ++i)
    {
        Double v;
        if (buf.get(i, (v)))
            cerr << "[" << i << "] = " << v << endl;
        else
            cerr << "[" << i << "] EMPTY!\n";
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


TEST(CircularBuffer, Overall)
{
    using namespace std;

    // Create some odd number of elements in a circular buffer.

    CircularBuffer<Double> buf(7);

    cerr << dec;

    // Now, add 3 elements to it and check if succeeded.
    buf.push(11.2);
    buf.push(12.3);
    buf.push(13.4);

    cerr << "After adding 3 elements: size=" << buf.size() << " capacity=" << buf.capacity() << ":\n";
    ShowCircularBuffer(buf);
    ASSERT_EQ(buf.size(), 3);

    cerr << "Adding element at position 5:\n";
    EXPECT_TRUE(buf.set(5, 15.5));
    ShowCircularBuffer(buf);
    ASSERT_EQ(buf.size(), 6);

    cerr << "Adding element at position 7 (should fail):\n";
    EXPECT_FALSE(buf.set(7, 10.0));
    ShowCircularBuffer(buf);
    ASSERT_EQ(buf.size(), 6);

    cerr << "Dropping first 2 elements:\n";
    buf.drop(2);
    ShowCircularBuffer(buf);
    ASSERT_EQ(buf.size(), 4);

    cerr << "Adding again element at position 6 (should roll):\n";
    buf.set(6, 22.1);
    ShowCircularBuffer(buf);

    cerr << "Adding element at existing position 2 (overwrite):\n";
    buf.set(2, 33.1);
    ShowCircularBuffer(buf);

    cerr << "Adding element at existing position 3 (no overwrite):\n";
    buf.set(3, 44.4, false);
    ShowCircularBuffer(buf);

    Double output;
    // [0] = 13.4 (after dropping first 2 elements)
    EXPECT_TRUE(buf.get(0, (output)));
    ASSERT_EQ(output, 13.4);
    // [2] = 33.1 overwriting
    EXPECT_TRUE(buf.get(2, (output)));
    ASSERT_EQ(output, 33.1);
    // [3] = was 15.5, requested to set 44.4, but not overwriting
    EXPECT_TRUE(buf.get(3, (output)));
    ASSERT_EQ(output, 15.5);
    // [6] = 22.1 (as set with rolling)
    EXPECT_TRUE(buf.get(6, (output)));
    ASSERT_EQ(output, 22.1);

    cerr << "Dropping first 4 positions:\n";
    buf.drop(4);
    ShowCircularBuffer(buf);
    EXPECT_TRUE(buf.get(2, (output))); // Was 6 before dropping
    ASSERT_EQ(output.d, 22.1);

    cerr << "Pushing 1 aslong there is capacity:\n";
    int i = 0;
    while (buf.push(1) != -1)
    {
        cerr << "Pushed, begin=" << buf.m_xBegin << " end=" << buf.m_xEnd << endl;
        ++i;
    }
    cerr << "Done " << i << " operations, buffer:\n";
    ShowCircularBuffer(buf);

    cerr << "Updating value at position 5:\n";
    EXPECT_TRUE(buf.update(5, Add(3.33)));
    ShowCircularBuffer(buf);
    EXPECT_TRUE(buf.get(5, (output)));
    ASSERT_EQ(output, 4.33);

    int offset = 9;
    cerr << "Forced adding at position 9 with dropping (capacity: " << buf.capacity() << "):\n";
    // State we already know it has failed. Calculate drop size.
    int dropshift = offset - (buf.capacity() - 1); // buf.capacity()-1 is the latest position
    offset -= dropshift;
    cerr << "Need to drop: " << dropshift << " New offset:" << offset << endl;
    ASSERT_GE(dropshift, 0);
    if (dropshift > 0)
    {
        buf.drop(dropshift);
        cerr << "AFTER DROPPING:\n";
        ShowCircularBuffer(buf);
        EXPECT_TRUE(buf.set(offset, 99.1, true));

        // size() - 1 is the latest possible offset
        ASSERT_EQ(buf.size() - 1 + dropshift, 9);
    }
    else
    {
        cerr << "NEGATIVE DROP!\n";
    }
    ShowCircularBuffer(buf);
    int size = buf.size();

    cerr << "Dropping rest of the items (passing " << (size) << "):\n";

    // NOTE: 'drop' gets the POSITION as argument, but this position
    // is allowed to be past the last addressable position. When passing
    // the current size, it should make the container empty.
    buf.drop(size);
    EXPECT_TRUE(buf.empty());

    ShowCircularBuffer(buf);

    cerr << "DONE.\n";
}

