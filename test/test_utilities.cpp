#include <iostream>
#include <array>
#include <chrono>
#include <future>
#include <thread>
#include <condition_variable>
#include "gtest/gtest.h"
#define SRT_TEST_CIRCULAR_BUFFER
#include "api.h"
#include "common.h"
#include "window.h"
#include "sync.h"

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
        IF_HEAVY_LOGGING(cerr << "(Double/" << instance << ": empty costruction)\n");
    }

    Double(double dd): d(dd)
    {
        instance = ++sourceid;
        IF_HEAVY_LOGGING(cerr << "(Double:/" << instance << " init construction:" << dd << ")\n");
    }

    Double(const Double& dd): d(dd.d)
    {
        instance = ++sourceid;
        IF_HEAVY_LOGGING(cerr << "(Double:/" << instance << " copy construction:" << dd.d << " object/" << dd.instance << ")\n");
    }

    operator double() { return d; }

    ~Double()
    {
        IF_HEAVY_LOGGING(cerr << "(Double:/" << instance << " destruction:" << d << ")\n");
    }

    void operator=(double dd)
    {
        IF_HEAVY_LOGGING(cerr << "(Double:/" << instance << " copy assignment:" << d << " -> " << dd << " value)\n");
        d = dd;
    }

    void operator=(const Double& dd)
    {
        IF_HEAVY_LOGGING(cerr << "(Double:/" << instance << " copy assignment:" << d << " -> " << dd.d << " object/" << dd.instance << ")\n");
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

    IF_HEAVY_LOGGING(cerr << "After adding 3 elements: size=" << buf.size() << " capacity=" << buf.capacity() << ":\n");
    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));
    ASSERT_EQ(buf.size(), 3);

    IF_HEAVY_LOGGING(cerr << "Adding element at position 5:\n");
    EXPECT_TRUE(buf.set(5, 15.5));
    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));
    ASSERT_EQ(buf.size(), 6);

    IF_HEAVY_LOGGING(cerr << "Adding element at position 7 (should fail):\n");
    EXPECT_FALSE(buf.set(7, 10.0));
    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));
    ASSERT_EQ(buf.size(), 6);

    IF_HEAVY_LOGGING(cerr << "Dropping first 2 elements:\n");
    buf.drop(2);
    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));
    ASSERT_EQ(buf.size(), 4);

    IF_HEAVY_LOGGING(cerr << "Adding again element at position 6 (should roll):\n");
    buf.set(6, 22.1);
    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));

    IF_HEAVY_LOGGING(cerr << "Adding element at existing position 2 (overwrite):\n");
    buf.set(2, 33.1);
    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));

    IF_HEAVY_LOGGING(cerr << "Adding element at existing position 3 (no overwrite):\n");
    buf.set(3, 44.4, false);
    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));

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

    IF_HEAVY_LOGGING(cerr << "Dropping first 4 positions:\n");
    buf.drop(4);
    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));
    EXPECT_TRUE(buf.get(2, (output))); // Was 6 before dropping
    ASSERT_EQ(output.d, 22.1);

    IF_HEAVY_LOGGING(cerr << "Pushing 1 aslong there is capacity:\n");
    int i = 0;
    while (buf.push(1) != -1)
    {
        IF_HEAVY_LOGGING(cerr << "Pushed, begin=" << buf.m_xBegin << " end=" << buf.m_xEnd << endl);
        ++i;
    }
    IF_HEAVY_LOGGING(cerr << "Done " << i << " operations, buffer:\n");
    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));

    IF_HEAVY_LOGGING(cerr << "Updating value at position 5:\n");
    EXPECT_TRUE(buf.update(5, Add(3.33)));
    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));
    EXPECT_TRUE(buf.get(5, (output)));
    ASSERT_EQ(output, 4.33);

    int offset = 9;
    IF_HEAVY_LOGGING(cerr << "Forced adding at position 9 with dropping (capacity: " << buf.capacity() << "):\n");
    // State we already know it has failed. Calculate drop size.
    int dropshift = offset - (buf.capacity() - 1); // buf.capacity()-1 is the latest position
    offset -= dropshift;
    IF_HEAVY_LOGGING(cerr << "Need to drop: " << dropshift << " New offset:" << offset << endl);
    ASSERT_GE(dropshift, 0);
    if (dropshift > 0)
    {
        buf.drop(dropshift);
        IF_HEAVY_LOGGING(cerr << "AFTER DROPPING:\n");
        IF_HEAVY_LOGGING(ShowCircularBuffer(buf));
        EXPECT_TRUE(buf.set(offset, 99.1, true));

        // size() - 1 is the latest possible offset
        ASSERT_EQ(buf.size() - 1 + dropshift, 9);
    }
    else
    {
        IF_HEAVY_LOGGING(cerr << "NEGATIVE DROP!\n");
    }
    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));
    int size = buf.size();

    IF_HEAVY_LOGGING(cerr << "Dropping rest of the items (passing " << (size) << "):\n");

    // NOTE: 'drop' gets the POSITION as argument, but this position
    // is allowed to be past the last addressable position. When passing
    // the current size, it should make the container empty.
    buf.drop(size);
    EXPECT_TRUE(buf.empty());

    IF_HEAVY_LOGGING(ShowCircularBuffer(buf));

    IF_HEAVY_LOGGING(cerr << "DONE.\n");
}

TEST(ConfigString, Setting)
{
    using namespace std;

    static const auto STRSIZE = 20;
    StringStorage<STRSIZE> s;

    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0);
    EXPECT_EQ(s.str(), std::string());

    char example_ac1[] = "example_long";
    char example_ac2[] = "short";
    char example_ac3[] = "example_longer";
    char example_acx[] = "example_long_excessively";
    char example_ace[] = "";

    // According to the standard, this array gets automatically
    // the number of characters + 1 for terminating 0. Get sizeof()-1
    // to get the number of characters.

    EXPECT_TRUE(s.set(example_ac1, sizeof (example_ac1)-1));
    EXPECT_EQ(s.size(), sizeof (example_ac1)-1);
    EXPECT_FALSE(s.empty());

    EXPECT_TRUE(s.set(example_ac2, sizeof (example_ac2)-1));
    EXPECT_EQ(s.size(), sizeof (example_ac2)-1);

    EXPECT_TRUE(s.set(example_ac3, sizeof (example_ac3)-1));
    EXPECT_EQ(s.size(), sizeof (example_ac3)-1);

    EXPECT_FALSE(s.set(example_acx, sizeof (example_acx)-1));
    EXPECT_EQ(s.size(), sizeof (example_ac3)-1);

    EXPECT_TRUE(s.set(example_ace, sizeof (example_ace)-1));
    EXPECT_EQ(s.size(), 0);

    string example_s1 = "example_long";
    string example_s2 = "short";
    string example_s3 = "example_longer";
    string example_sx = "example_long_excessively";
    string example_se = "";

    EXPECT_TRUE(s.set(example_s1));
    EXPECT_EQ(s.size(), example_s1.size());
    EXPECT_FALSE(s.empty());

    EXPECT_TRUE(s.set(example_s2));
    EXPECT_EQ(s.size(), example_s2.size());

    EXPECT_TRUE(s.set(example_s3));
    EXPECT_EQ(s.size(), example_s3.size());

    EXPECT_FALSE(s.set(example_sx));
    EXPECT_EQ(s.size(), example_s3.size());

    EXPECT_TRUE(s.set(example_se));
    EXPECT_EQ(s.size(), 0);
    EXPECT_TRUE(s.empty());
}

struct AckData
{
    int32_t journal;
    int32_t ackseq;
};

static void TestAckWindow(const std::array<AckData, 5>& data, size_t initpos, const std::string& casename)
{
    using srt::sync::steady_clock;

    typedef CACKWindow<10> ackwindow_t;
    ackwindow_t ackwindow;

    int b4 = data[0].journal - initpos;

    for (size_t i = 0; i < initpos; ++i)
    {
        ackwindow.store(b4, 0);
        ++b4;
    }

    for (auto& n: data)
        ackwindow.store(n.journal, n.ackseq);

    steady_clock::time_point now = steady_clock::now();

    // Now remove those initial ones
    int32_t dummy1, dummy2;
    ackwindow.acknowledge(data[0].journal-1, now, (dummy1), (dummy2));

    ASSERT_EQ(ackwindow.first().iJournal, data[0].journal) << " (" << casename << ")";
    ASSERT_EQ(ackwindow.last().iJournal, data[4].journal) << " (" << casename << ")";
    ASSERT_EQ(ackwindow.size(), 5) << " (" << casename << ")";

    int iack = 0;
    int td =0;

    // Remove oldest node. Should go ok.
    ACKWindow::Status stat = ackwindow.acknowledge(data[0].journal, now, (iack), (td));
    EXPECT_EQ(iack, data[0].ackseq) << " (" << casename << ")";
    EXPECT_EQ(stat, ACKWindow::OK) << " (" << casename << ")";
    EXPECT_EQ(ackwindow.size(), 4) << " (" << casename << ")";
    EXPECT_EQ(ackwindow.first().iJournal, data[1].journal) << " (" << casename << ")";

    // Now remove the node +2
    stat = ackwindow.acknowledge(data[2].journal, now, (iack), (td));
    EXPECT_EQ(iack, data[2].ackseq) << " (" << casename << ")";
    EXPECT_EQ(stat, ACKWindow::OK) << " (" << casename << ")";
    EXPECT_EQ(ackwindow.size(), 2) << " (" << casename << ")";
    EXPECT_EQ(ackwindow.first().iJournal, data[3].journal) << " (" << casename << ")";

    // Now remove too old node
    stat = ackwindow.acknowledge(data[1].journal, now, (iack), (td));
    EXPECT_EQ(stat, ACKWindow::OLD) << "(" << casename << ")";
    // Like above - no changes were expected
    EXPECT_EQ(ackwindow.size(), 2) << " (" << casename << ")";
    EXPECT_EQ(ackwindow.first().iJournal, data[3].journal) << " (" << casename << ")";

    // And remove the node that wasn't inserted
    int32_t wrongnode = data[4].journal+1;
    stat = ackwindow.acknowledge(wrongnode, now, (iack), (td));
    EXPECT_EQ(stat, ACKWindow::ROGUE);
    // Like above - no changes were expected
    EXPECT_EQ(ackwindow.size(), 2) << " (" << casename << ")";
    EXPECT_EQ(ackwindow.first().iJournal, data[3].journal) << " (" << casename << ")";

    // Now insert one value that jumps over. It's not exactly
    // possible in the normal SRT runtime, but the reaction should be
    // prepared just in case.
    ackwindow.store(data[4].journal+2, data[4].ackseq);
    // Now a search of data[4].journal+1 should fail appropriately.
    stat = ackwindow.acknowledge(data[4].journal+1, now, (iack), (td));
    EXPECT_EQ(stat, ACKWindow::WIPED);
}

TEST(ACKWindow, API)
{
    // We have a situation with circular buffer with circular
    // numbers with two different cirtulations. We need then
    // permutations of 4 special plus 1 regular, in total:
    //
    // 1. Regular numbers in a regular range
    // 2. Regular numbers in a split range
    // 3. Number overflow in a regular range.
    // 4. Number ovewflow in a split range in lower part
    // 5. Number overflow in a split range in upper part

    int32_t seq0 = CSeqNo::m_iSeqNoTH;

    int32_t basej = 100;
    std::array<AckData, 5> regular = {
        AckData {basej + 0, seq0},
        AckData {basej + 1, seq0 + 10},
        AckData {basej + 2, seq0 + 20},
        AckData {basej + 3, seq0 + 30},
        AckData {basej + 4, seq0 + 40}
    };

    // 1.
    TestAckWindow(regular, 0, "regular/0");

    // 2.
    TestAckWindow(regular, 7, "regular/7");

    basej = CSeqNo::m_iMaxSeqNo-2;

    std::array<AckData, 5> overflow = {
        AckData {basej + 0, seq0},
        AckData {basej + 1, seq0 + 10},
        AckData {basej + 2, seq0 + 20},
        AckData {basej + 3, seq0 + 30},
        AckData {basej + 4, seq0 + 40}
    };

    // 3.
    TestAckWindow(overflow, 0, "overflow/0");

    // 4.
    TestAckWindow(overflow, 3, "overflow/3");

    // 5.
    TestAckWindow(overflow, 7, "overflow/7");
}
