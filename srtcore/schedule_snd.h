
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

struct SchedPacketInfo
{
    SocketKeeper m_Socket;
    int32_t m_iSeqNo;

    void set_socket(CUDTSocket* sock);

    SRTSOCKET id() const { return m_Socket.id(); }

    bool empty() const { return m_iSeqNo == SRT_SEQNO_NONE; }

    int32_t seqno() const { return m_iSeqNo; }

    SchedPacketInfo(CUDTSocket* sock = NULL, int32_t seqno = SRT_SEQNO_NONE);
};

struct SchedPacket: SchedPacketInfo
{
    sched::Type m_Type;

    // NOTE: Both constructor and set_socket() call will need to
    // perform the official acquisition of the socket, which requires
    // locking CUDTUnited::m_GlobControlLock. Further copying of the
    // SocketKeeper object doesn't require any locking.
    SchedPacket(CUDTSocket* sock = NULL, int32_t seqno = SRT_SEQNO_NONE, sched::Type t = sched::TP_REGULAR):
        SchedPacketInfo(sock, seqno),
        m_Type(t)
    {
    }
    sched::Type type() const { return m_Type; }
};

struct SendTaskProto
{
    typedef sync::steady_clock::time_point ClockTime;
    // DOUBLE INTERPRETATION:
    // - for pending queue, it's the latest scheduling time = mctrl.srctime
    // - for schedule queue, it's the time to pickup
    ClockTime m_tsSendTime;
    ClockTime m_tsLatestDeliveryTime;
    SchedPacket m_Packet;

    SendTaskProto()
        : m_tsSendTime(), m_Packet() {}

    SendTaskProto(const SchedPacket& sp, sync::steady_clock::time_point when, sync::steady_clock::time_point delivery)
        : m_tsSendTime(when), m_tsLatestDeliveryTime(delivery), m_Packet(sp) {}
};

struct SendTask: SendTaskProto
{
    // NODE FIELDS for HeapSet
    typedef std::list<SendTask> tasklist_t;
    typedef typename tasklist_t::iterator taskiter_t;
    typedef sync::steady_clock::time_point key_type;

    key_type key() const { return m_tsSendTime; }
    sync::atomic<size_t> m_zHeapPos; // Required by HeapSet

    // "Payload" fields.

    // = mctrl.srctime + optimisticRTT() + latency
    // This is the latest time when the packet is expected to be
    // given up to the application - at latest, if in EAGER mode.
    // REXMIT packets that are past this time already should be dropped.
    key_type m_tsLatestDeliveryTime;
    SchedPacket m_Packet;
    std::list<SendTask>* m_pBaseList;

    // Same definition as by HeapSet; here a shortcut.
    // Can't use the definition from HeapSet because it's
    // a template that has requirements for the type parameter.
    static const size_t npos = std::string::npos;

    SendTask()
        : SendTaskProto(), m_zHeapPos(npos), m_pBaseList(0) {}

    SendTask(const SendTaskProto& proto)
        : SendTaskProto(proto), m_zHeapPos(npos), m_pBaseList(0) {}

    // Note: Copying a task is only allowed because of the need
    // to move from one container to another. A single task that is
    // pinned to a sender buffer may however exist only in one instance.
    SendTask(const SendTask& src):
        SendTaskProto(src.m_Packet, src.m_tsSendTime, src.m_tsLatestDeliveryTime),
        // m_zHeapPos(src.m_zHeapPos.load()),
        m_zHeapPos(npos), // Copied element is NEVER in ths container!
        m_Packet(src.m_Packet),
        m_pBaseList(src.m_pBaseList)
    {}

    // convenience
    void set_time(sync::steady_clock::time_point tp) { m_tsSendTime = tp; }

    bool is_ready(key_type basetime) const
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
    static key_type& key(taskiter_t v) { return v->m_tsSendTime; }
    static bool order(const key_type& left, const key_type& right)
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
    typedef clock_type::duration duration;

protected:
    std::map<socket_t, SendTask::tasklist_t> m_TaskMap;
    HeapSet<SendTask::taskiter_t, SendTask> m_TaskQueue;
    typedef std::deque<SendTask::taskiter_t> pending_t;
    pending_t m_PendingRegularQueue;

    // We use map in order to keep always only one entry per socket.
    std::map<socket_t, SchedPacketInfo> m_PendingRexmit;
    clock_time m_tsAboutTime;

    sync::Mutex m_Lock;
    sync::Condition m_TaskReadyCond;
    sync::atomic<bool> m_bBroken;

    SRT_TSA_NEEDS_LOCKED(m_Lock)
    void pop_update_time();

    // Helper functions to operate with the pending queues
    // You better check if q.empty() before the call!
    SendTask::taskiter_t pullTask(pending_t& q)
    {
        SendTask::taskiter_t i = *q.begin();
        q.pop_front();
        return i;
    }

    void deletePending(pending_t& q)
    {
        for (pending_t::iterator i = q.begin(); i != q.end(); ++i)
        {
            SendTask::taskiter_t pt = *i;
            pt->m_pBaseList->erase(pt);
            // NOTE: All elements become dangling iterators here!
        }
        q.clear();
    }

public:
    const HeapSet<SendTask::taskiter_t, SendTask>& queue() { return m_TaskQueue; }

    SendScheduler(): m_bBroken(false)
    {
    }

    void interrupt();

    bool running()
    {
        return !m_bBroken;
    }

    // SendTask::taskiter_t enqueue_task(socket_t id, const SendTask& proto);
    void prescheduleRegular(const SendTaskProto& sp);
    void prescheduleLoss(CUDTSocket* provider, int32_t first_seqno);

    SendTask::taskiter_t createTask(const SendTaskProto& sp);
    void updateTask(SendTask::taskiter_t ti);

protected:
    // This is NOLOCK; derived classes please use lock.
    SRT_TSA_NEEDS_LOCKED(m_Lock)
    bool have_task_ready();

    SRT_TSA_NEEDS_LOCKED(m_Lock)
    bool updatePreschedule(size_t max_sched);

public:

    // Wait until the time has come
    bool wait_extlock(srt::sync::UniqueLock&);

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
