#include "gtest/gtest.h"
#include "core.h"
#include "queue.h"


//TEST(CSndUList, Update)
//{
//    CSndUList list;
//
//    CSNode nodes[16];
//    CUDT udts[16];
//
//    for (size_t i = 0; i < 16; ++i)
//    {
//        udts[i].m_pSNode = nodes + i;
//    }
//
//
//    for (size_t i = 0; i < 16; ++i)
//    {
//        list.update(&udts[i], CSndUList::DONT_RESCHEDULE);
//    }
//
//
//    for (size_t i = 0; i < 16; ++i)
//    {
//        udts[i].m_pSNode = nullptr;
//    }
//}



struct node
{
    int heaploc;
    int ts;
};



class UList
{

public:

    UList()
    {
        m_pHeap = new node * [m_iArrayLength];
    }

    ~UList()
    {
        delete[] m_pHeap;
    }


public:

    void print_state() const
    {
        for (int i = 0; i <= m_iLastEntry; ++i)
        {
            std::cout << m_pHeap[i]->ts << " ";
        }

        std::cout << std::endl;
    }

    void insert_norealloc(int64_t ts, node* n)
    {
        // do not insert repeated node
        if (n->heaploc >= 0)
            return;

        m_iLastEntry++;
        m_pHeap[m_iLastEntry] = n;
        n->ts = ts;

        int q = m_iLastEntry;
        int p = q;
        while (p != 0)
        {
            p = (q - 1) >> 1;
            if (m_pHeap[p]->ts <= m_pHeap[q]->ts)
                break;

            std::swap(m_pHeap[p], m_pHeap[q]);
            m_pHeap[q]->heaploc = q;
            q = p;
        }

        n->heaploc = q;
    }


    void remove_(node* n)
    {
        if (n->heaploc >= 0)
        {
            // remove the node from heap
            m_pHeap[n->heaploc] = m_pHeap[m_iLastEntry];
            m_iLastEntry--;
            m_pHeap[n->heaploc]->heaploc = n->heaploc;

            int q = n->heaploc;
            int p = q * 2 + 1;
            while (p <= m_iLastEntry)
            {
                if ((p + 1 <= m_iLastEntry) && (m_pHeap[p]->ts > m_pHeap[p + 1]->ts))
                    p++;

                if (m_pHeap[q]->ts > m_pHeap[p]->ts)
                {
                    node* t = m_pHeap[p];
                    m_pHeap[p] = m_pHeap[q];
                    m_pHeap[p]->heaploc = p;
                    m_pHeap[q] = t;
                    m_pHeap[q]->heaploc = q;

                    q = p;
                    p = q * 2 + 1;
                }
                else
                    break;
            }

            n->heaploc = -1;
        }
    }

private:

    int m_iLastEntry = -1;
    node** m_pHeap = nullptr;     // The heap array
    int m_iArrayLength = 512;     // physical length of the array

};





TEST(CSndUList, Heap)
{
    node nodes[16];
    UList lst;

    for (size_t i = 0; i < 16; ++i)
    {
        nodes[i].ts = 1 + (rand() % static_cast<int>(32));
        lst.insert_norealloc(16 - i, nodes + i);
        lst.print_state();
    }

}




