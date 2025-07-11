
#ifndef INC_SRT_SCHEDULE_SND_H
#define INC_SRT_SCHEDULE_SND_H

#include <map>
#include <list>
#include "sync.h"
#include "atomic.h"
#include "common.h" // SocketKeeper
#include "utilities.h"
#include "buffer_snd.h"

namespace srt
{

namespace sched
{
    enum Type
    {
        TP_REGULAR = 0,
        TP_REXMIT = 1,
        TP_CONTROL = 2
    };
}

// This structure contains the information about the
// socket, packet contents and sequence number of
// the packet that is about to be sent.

// Note: scheduling should happen at the exact place
// where the scheduling event should appear:
// - When calling srt_send: schedule regular packet
// - - If packetfilter control packet is ready AFTER that, schedule that, too.
// - When dispatching LOSSREPORT: schedule rexmit packet
// - When NAKREPORT timer expired: schedule rexmit packet
struct SchedPacket
{
    SocketKeeper m_Socket;
    int32_t m_iSeqNo;
    sched::Type m_Type;

    // NOTE: Both constructor and set_socket() call will need to
    // perform the official acquisition of the socket, which requires
    // locking CUDTUnited::m_GlobControlLock. Further copying of the
    // SocketKeeper object doesn't require any locking.
    SchedPacket(CUDTSocket* sock = NULL, int32_t seqno = SRT_SEQNO_NONE, sched::Type t = sched::TP_REGULAR);
    void set_socket(CUDTSocket* sock);

    SRTSOCKET id() const { return m_Socket.id(); }

    bool empty() const { return m_iSeqNo == SRT_SEQNO_NONE; }

    int32_t seqno() const { return m_iSeqNo; }
    sched::Type type() const { return m_Type; }
};

struct SendTask
{
    typedef std::list<SendTask> tasklist_t;
    typedef typename tasklist_t::iterator taskiter_t;
    typedef sync::steady_clock::time_point key_t;
    key_t m_tsSendTime;
    SchedPacket m_Packet;
    sync::atomic<size_t> m_zHeapPos; // Required by HeapSet
    std::list<SendTask>* m_pBaseList;

    // Same definition as by HeapSet; here a shortcut.
    static const size_t npos = HeapSet<int>::npos;

    SendTask()
        : m_tsSendTime(), m_Packet(), m_zHeapPos(npos), m_pBaseList(0) {}

    SendTask(const SchedPacket& sp, sync::steady_clock::time_point when)
        : m_tsSendTime(when), m_Packet(sp), m_zHeapPos(npos), m_pBaseList(0) {}

    // Note: Copying a task is only allowed because of the need
    // to move from one container to another. A single task that is
    // pinned to a sender buffer may however exist only in one instance.
    SendTask(const SendTask& src):
        m_tsSendTime(src.m_tsSendTime),
        m_Packet(src.m_Packet),
        m_zHeapPos(src.m_zHeapPos.load()),
        m_pBaseList(src.m_pBaseList)
    {}

    bool is_ready(key_t basetime) const
    {
        return m_tsSendTime < basetime;
    }

    SendTask& operator=(const SendTask& src)
    {
        m_tsSendTime = src.m_tsSendTime;
        m_Packet = src.m_Packet;
        m_zHeapPos = src.m_zHeapPos.load();
        return *this;
    }

    static sync::atomic<size_t>& position(taskiter_t v) { return v->m_zHeapPos; }
    static key_t& key(taskiter_t v) { return v->m_tsSendTime; }
    static bool order(const key_t& left, const key_t& right)
    {
        return left < right;
    }

    static std::list<SendTask> free_list;
    static taskiter_t none() { return free_list.end(); }

    static std::string print(taskiter_t v);
};

struct SendScheduler
{
    typedef SRTSOCKET socket_t;
    typedef sync::steady_clock clock_type;
    typedef clock_type::time_point clock_time;

protected:
    std::map<socket_t, SendTask::tasklist_t> m_TaskMap;
    HeapSet<SendTask::taskiter_t, SendTask> m_TaskQueue;
    clock_time m_tsAboutTime;
    void pop_update_time();

    sync::Mutex m_Lock;
    sync::Condition m_TaskReadyCond;
    sync::atomic<bool> m_bBroken;

public:
    const HeapSet<SendTask::taskiter_t, SendTask>& queue() { return m_TaskQueue; }

    SendScheduler(): m_bBroken(false)
    {
    }

    void interrupt()
    {
        m_bBroken = true;
        sync::ScopedLock hold (m_Lock);
        m_TaskReadyCond.notify_all(); // Force waiting functions to exit
    }

    bool running()
    {
        return !m_bBroken;
    }

    SendTask::taskiter_t enqueue_task(socket_t id, const SendTask& proto);

    void update_task(SendTask::taskiter_t ti);

protected:
    // This is NOLOCK; derived classes please use lock.
    bool have_task_ready();

public:
    // Wait until the time has come
    bool wait();

    void withdraw(socket_t id);

    template<class Predicate>
    void withdraw_if(socket_t id, Predicate match)
    {
        sync::ScopedLock lk (m_Lock);
        // Delete all tasks for the given socket id.
        // We have them collected in the list: m_TaskMap

        SendTask::tasklist_t& id_list = m_TaskMap[id];

        // As we know that all these items were added to m_TaskQueue,
        // we need to withdraw them all from m_TaskQueue.
        SendTask::taskiter_t idt_next = id_list.begin();
        for (SendTask::taskiter_t idt = idt_next; idt != id_list.end(); idt = idt_next)
        {
            ++idt_next;
            if (match(idt))
            {
                cancel_nolock(idt);
            }
        }
    }

protected:
    void cancel_nolock(SendTask::taskiter_t itask);

public:
    void cancel(SendTask::taskiter_t itask);

    SchedPacket wait_pop();

};

}

#endif
