
#include <sstream>
#include <iomanip>

#include "schedule_snd.h"
#include "api.h"
#include "core.h"
#include "logging.h"
#include "ofmt_iostream.h"

using namespace std;
using namespace srt;
using namespace srt::sync;

using srt::logging::qslog;
using namespace hvu;

namespace srt
{

SchedPacketInfo::SchedPacketInfo(CUDTSocket* sock, int32_t seqno):
    m_Socket(CUDT::uglobal())
{
    if (sock)
        m_Socket = CUDT::keep(sock);

    m_iSeqNo = seqno;
}

void SchedPacketInfo::set_socket(CUDTSocket* sock)
{
    if (sock)
        m_Socket = CUDT::keep(sock);
}

static const char* const schedtype [] = {"regular", "rexmit", "pf-control"};

std::string SendTask::print(SendTask::taskiter_t v)
{
    using namespace hvu;

    ofmt_bufs out;
    // Complicated pre-C++20 time formatting...
    time_t tval = count_seconds(v->m_tsSendTime.time_since_epoch());
    struct tm tme;
    localtime_r(&tval, &tme);

    int64_t total_usec = count_microseconds(v->m_tsSendTime.time_since_epoch());
    int64_t usec = total_usec - (tval * 1000000);
    out << "<" << fmt(tme, "%T") << "." << fmt(usec, fmtc().width(6).fillzero())
        << "> @" << v->m_Packet.m_Socket.id();
    int32_t seq = v->m_Packet.seqno();
    if (seq == SRT_SEQNO_NONE)
    {
        out << " [empty]";
    }
    else
    {
        out << " [" << schedtype[v->m_Packet.type()] << "] %" << seq;
    }
    return out.str();
}

std::list<SendTask> SendTask::free_list;

SendTask::taskiter_t SendScheduler::createTask(const SendTaskProto& sp)
{
    SRTSOCKET id = sp.m_Packet.m_Socket.socket->id();
    SendTask::tasklist_t& wlist = m_TaskMap[id];

    wlist.push_back(SendTask (sp));
    SendTask::taskiter_t itask = --wlist.end();
    itask->m_pBaseList = &wlist;

    return itask;
}

void SendScheduler::prescheduleLoss(CUDTSocket* provider, int32_t first_seqno)
{
    if (m_bBroken)
    {
        HLOGC(qslog.Debug, log << "Schedule: PRE-ENQ: DENIED, schedule is broken");
        return;
    }

    SRTSOCKET id = provider->id();
    sync::ScopedLock lk (m_Lock);

    SchedPacketInfo* psi;
    bool isnew;
    Tie(psi, isnew) = map_tryinsertp(m_PendingRexmit, id);

    if (!isnew)
        return;

    // Insert a new item
    psi->set_socket(provider);
    psi->m_iSeqNo = first_seqno;

    if (!have_task_ready())
    {
        m_TaskReadyCond.notify_all();
        //IF_HEAVY_LOGGING(notif = " [NOTIFIED]");
    }
}

void SendScheduler::prescheduleRegular(const SendTaskProto& sp)
{
    if (m_bBroken)
    {
        HLOGC(qslog.Debug, log << "Schedule: PRE-ENQ: DENIED, schedule is broken");
        return;
    }


    sync::ScopedLock lk (m_Lock);
    SendTask::taskiter_t itask = createTask(sp);

    m_PendingRegularQueue.push_back(itask);

    IF_HEAVY_LOGGING(sched::Type type = sp.m_Packet.m_Type);
    IF_HEAVY_LOGGING(SRTSOCKET id = sp.m_Packet.m_Socket.socket->id());
    IF_HEAVY_LOGGING(const char* notif = "");
    IF_HEAVY_LOGGING(static const char* const type_name[3] = {"REGULAR", "REXMIT", "CONTROL"});
    if (!have_task_ready())
    {
        m_TaskReadyCond.notify_all();
        IF_HEAVY_LOGGING(notif = " [NOTIFIED]");
    }

    HLOGC(qslog.Debug, log << "Schedule: PRE-ENQ: @" << id << " %" << sp.m_Packet.m_iSeqNo << " type=" << type_name[type]
        << "LATEST: SEND=" << FormatTime(sp.m_tsSendTime) << " DELIVERY=" << FormatTime(sp.m_tsLatestDeliveryTime) << notif);
}

// NOTE: This is a function that enqueues a task in the queue directly;
// that should be rather not used because you need to know the exact execution time.
 /*
SendTask::taskiter_t SendScheduler::enqueue_task(socket_t id, const SchedPacket& proto, const clock_time& when, const clock_time& delivery)
{
    if (m_bBroken)
    {
        HLOGC(qslog.Debug, log << "Schedule: ENQ: DENIED, schedule is broken");
        return SendTask::none();
    }

    sync::ScopedLock lk (m_Lock);
    SendTask::taskiter_t itask = create_task(proto, when, delivery);

    bool was_ready = have_task_ready();

    size_t pos = m_TaskQueue.insert(itask);

    IF_HEAVY_LOGGING(bool was_first = false);
    IF_HEAVY_LOGGING(bool now_ready = false);
    if (pos == 0) // earliest task
    {
        m_tsAboutTime = m_TaskQueue.top()->m_tsSendTime; // INSERTED: will not be empty
        IF_HEAVY_LOGGING(was_first = true);
    }

    // XXX Shouldn't it update always if m_tsAboutTime was updated?
    if (!was_ready && have_task_ready())
    {
        m_TaskReadyCond.notify_all();
        IF_HEAVY_LOGGING(now_ready = true);
    }

    HLOGC(qslog.Debug, log << "Schedule: ENQ: new"
            << fmt_if(now_ready, " READY")
            << " task at T=" << FormatTime(itask->m_tsSendTime)
            << fmt_if(was_first, " (NEW TOP)")
            << fmt_if(was_ready, " (NOW READY)", " (ONLY ADDED)")
            << fmt_if(now_ready, " - NOTIFY"));

    return itask;
}
// */

bool SendScheduler::updatePreschedule(size_t max_sched)
{
    // The next call to wait() will return an already ready task,
    // so do not reschedule new ones until they are all dispatched.
    if (have_task_ready())
    {
        HLOGC(qslog.Debug, log << "updatePreschedule: POSTPONED, have ready tasks");
        return true;
    }

    if (m_PendingRegularQueue.empty() && m_PendingRexmit.empty())
    {
        HLOGC(qslog.Debug, log << "updatePreschedule: NO PENDING tasks");
        return false;
    }

    steady_clock::time_point now = steady_clock::now();

    // Prioritization:
    // 1. Check if there is any regular packet already overdue. If so,
    //    it takes precedence before any retransmission packet.
    size_t nsched = 0;
    while (!m_PendingRegularQueue.empty())
    {
        SendTask::taskiter_t pt = pullTask(m_PendingRegularQueue);
        if (pt->m_tsSendTime > now)
            break; // stop at first being in the future

        SchedPacket& proto = pt->m_Packet;
        proto.m_Socket.socket->core().planSendingTime(proto.m_Type, pt->m_tsLatestDeliveryTime, (pt->m_tsSendTime));
        size_t pos = m_TaskQueue.insert(pt);

        if (pos == 0) // earliest task
        {
            // INSERTED: will not be empty
            m_tsAboutTime = m_TaskQueue.top_raw()->m_tsSendTime;
            // NOTE: theoretically you should notify() here, but it's not necessary
            // because this function has the same affinity as the wait() caller.
        }

        ++nsched;
        if (nsched >= max_sched)
        {
            HLOGC(qslog.Debug, log << "update_forequeue: enqueued " << max_sched << " REGULAR tasks, "
                    << fmt_if(pos == 0, " NEW HEAD,") << " still pending: "
                    << m_PendingRegularQueue.size() << " REG + " << m_PendingRegularQueue.size() << " REXMIT");
            return have_task_ready();
        }
    }

    // 2. Review the retransmission packets; insert those that still have
    //    remaining delivery time. Drop the others.

    IF_HEAVY_LOGGING(int regsched = nsched);
    IF_HEAVY_LOGGING(bool new_head = false);
    std::vector<socket_t> to_delete;

    // Ok, the rexmit queue is different.
    // We have normally information about ONE loss, which at the moment of
    // pickup need not hold. We then run the loop by index, with notifying up
    // to which element we have passed the task to the task queue, and those
    // will be then removed.

    // Walk through all entries. Later delete those that don't report any more lost packets
    for (map<socket_t, SchedPacketInfo>::const_iterator i = m_PendingRexmit.begin(); i != m_PendingRexmit.end(); ++i)
    {
        SendTaskProto proto;

        for (;;)
        {
            // This should check what losses are expected to be sent, which losses
            // are too old and should be deleted, and whether anything is still about to be sent.
            int loss_chain = i->second.m_Socket.socket->core().extractPlannedLoss((proto));
            // RETURNS:
            // 0: No loss scheduled at the moment for that iteration (note: should not happen IN ITERATION)
            // 1: One loss was detected, but then there is none.
            // 2: The loss was extracted and there's a new one waiting.
            if (loss_chain)
            {
                ++nsched;
                // We have something - schedule it.
                SendTask::taskiter_t itask = createTask(proto);

                size_t pos = m_TaskQueue.insert(itask);
                if (pos == 0) // earliest task
                {
                    m_tsAboutTime = m_TaskQueue.top_raw()->m_tsSendTime;
                    IF_HEAVY_LOGGING(new_head = true);
                }
            }

            if (loss_chain != 2)
            {
                to_delete.push_back(i->first);
                break; // forget about this one; check the next socket
            }

            if (nsched >= max_sched)
            {
                HLOGC(qslog.Debug, log << "update_forequeue: enqueued " << max_sched << " = " << regsched
                        << " REG + " << (nsched - regsched) << " REXMIT tasks,"
                        << fmt_if(new_head, " NEW HEAD,") << " still pending: "
                        << m_PendingRegularQueue.size() << " REG + " << m_PendingRexmit.size() << " sockets in REXMIT");
                return have_task_ready();
            }
        }

        if (nsched >= max_sched)
        {
            return have_task_ready();
        }
    }

    // Delete entries for sockets that do not report any rexmit
    for (size_t i = 0; i < to_delete.size(); ++i)
        m_PendingRexmit.erase(to_delete[i]);

    IF_HEAVY_LOGGING(int rexsched = nsched);
    // 3. Try to schedule regular packets, if there are still free slots
    while (!m_PendingRegularQueue.empty())
    {
        SendTask::taskiter_t pt = pullTask(m_PendingRegularQueue);
        SchedPacket& proto = pt->m_Packet;
        proto.m_Socket.socket->core().planSendingTime(proto.m_Type, pt->m_tsLatestDeliveryTime, (pt->m_tsSendTime));
        size_t pos = m_TaskQueue.insert(pt);

        if (pos == 0) // earliest task
        {
            // INSERTED: will not be empty
            m_tsAboutTime = m_TaskQueue.top_raw()->m_tsSendTime;
            // NOTE: theoretically you should notify() here, but it's not necessary
            // because this function has the same affinity as the wait() caller.
            IF_HEAVY_LOGGING(new_head = true);
        }

        ++nsched;
        if (nsched >= max_sched)
        {
            break;
        }
    }

    HLOGC(qslog.Debug, log << "update_forequeue: enqueued " << nsched << " = "
            << regsched << "YD + " << (rexsched - regsched) << "RX + "
            << (nsched - rexsched) << "RG tasks,"
            << fmt_if(new_head, " NEW HEAD,") << " still pending: "
            << m_PendingRegularQueue.size() << " REG + " << m_PendingRexmit.size() << " sockets in REXMIT");
    return have_task_ready();
}

bool SendScheduler::have_task_ready()
{
    // XXX Make sure it works. The m_tsAboutTime is being updated
    // with every update in m_Scheduler, so it should keep the top
    // element's time, or be 0 if there's no top element.

    return !sync::is_zero(m_tsAboutTime) && m_tsAboutTime <= clock_type::now();
#if 0

    if (!m_TaskQueue.empty())
    {
        SendTask::taskiter_t earliest = m_TaskQueue.top();
        if (earliest->is_ready(clock_type::now()))
        {
            return true;
        }
    }
    return false;
#endif
}

void SendScheduler::withdraw(socket_t id)
{
    sync::ScopedLock lk (m_Lock);
    // Delete all tasks for the given socket id.
    // We have them collected in the list: m_TaskMap

    SendTask::tasklist_t& id_list = m_TaskMap[id];

    // As we know that all these items were added to m_TaskQueue,
    // we need to withdraw them all from m_TaskQueue.

    IF_HEAVY_LOGGING(int nerased = 0);
    for (SendTask::taskiter_t idt = id_list.begin(); idt != id_list.end(); ++idt)
    {
        if (m_TaskQueue.erase(idt))
        {
            IF_HEAVY_LOGGING(++nerased);
        }
    }
    // The list should be empty, so delete the entry itself.
    int iderased SRT_ATR_UNUSED = m_TaskMap.erase(id);
    // We don't know if the earliest in the queue was deleted,
    // so just rewrite it anyway.
    pop_update_time();

#if HVU_ENABLE_HEAVY_LOGGING
    hvu::ofmt_bufs nextone;
    if (m_TaskQueue.empty())
        nextone << "NO NEXT TASK";
    else
        nextone << "next in " << FormatDurationAuto(m_tsAboutTime - steady_clock::now())
            << " from @" << m_TaskQueue.top()->m_Packet.id();
#endif
    HLOGC(qslog.Debug, log << "Schedule: withdrawn @" << int(id)
            << (iderased ? "" : " (NOT FOUND!)") << " - erased " << nerased << " tasks -" << nextone);
}

void SendScheduler::pop_update_time()
{
    if (!m_TaskQueue.empty())
        m_tsAboutTime = m_TaskQueue.top()->m_tsSendTime; // checked that ! empty
    else
        m_tsAboutTime = clock_time();
}

void SendScheduler::cancel(SendTask::taskiter_t itask)
{
    sync::ScopedLock lk (m_Lock);
    cancel_nolock(itask);
}

void SendScheduler::interrupt()
{
    m_bBroken = true;
    HLOGC(qslog.Debug, log << "Schedule: INTERRUPT: locking...");
    sync::ScopedLock hold (m_Lock);
    HLOGC(qslog.Debug, log << "Schedule: INTERRUPT: notifying waiters");

    m_TaskReadyCond.notify_all(); // Force waiting functions to exit
}


void SendScheduler::cancel_nolock(SendTask::taskiter_t itask)
{
    HLOGC(qslog.Debug, log << "Schedule: CANCEL: @" << itask->m_Packet.id() << " T=" << FormatTime(itask->m_tsSendTime));
    m_TaskQueue.erase(itask);
    itask->m_pBaseList->erase(itask);
    pop_update_time();
}

SchedPacket SendScheduler::wait_pop()
{
    SchedPacket packet;
    sync::UniqueLock lk (m_Lock);
    // Wait until the time has come to execute
    // the next task. Extract the task structure
    // and remove the task from the list.

    for (;;)
    {
        if (m_bBroken)
        {
            HLOGC(qslog.Debug, log << "Schedule: wait_pop: broken");
            return SchedPacket();
        }

        // This will move pending tasks if there's no ready task yet.
        // Returns true if there's at least one task ready for pickup
        if (updatePreschedule(4 /*XXX USE CONSTANT*/))
        {
            HLOGC(qslog.Debug, log << "Schedule: WAIT: task ready since " << FormatDurationAuto(steady_clock::now() - m_tsAboutTime));
            break;
        }

        if (m_TaskQueue.empty()) // This means that also m_ForeQueue was empty
        {
            HLOGC(qslog.Debug, log << "Schedule: WAIT: task NOT ready, NO NEW TASKS, WAIT FOR SIGNAL)");
            // This will be signaled if anything is added to m_ForeQueue or m_TaskQueue.
            m_TaskReadyCond.wait(lk);
        }
        else
        {
            clock_time now = steady_clock::now();
            HLOGC(qslog.Debug, log << "Schedule: WAIT: task NOT ready, next in " << FormatDurationAuto(m_tsAboutTime - now));

            duration remain = m_tsAboutTime - now;
            if (remain > duration()) // m_tsAboutTime is in the future
            {
                // Sleep accuracy may be problematic. Keep the rule of 10ms as the
                // minimum "long time"; if this is still so much time to wait, sleep
                // for 3/4 of this time and then check again. If less time remained,
                // sleep always 1ms. Long time sleeps should not happen during the
                // transmission.
                if (remain > sync::milliseconds_from(10))
                    this_thread::sleep_for(remain * 3 / 4);
                else
                    this_thread::sleep_for(milliseconds_from(1));
            }
        }
        // CONTINUE.
    }
    // Here we are sure that the top() task is ready to execute
    SendTask::taskiter_t itask = m_TaskQueue.pop();
    pop_update_time();

    if (itask == SendTask::none())
    {
        HLOGC(qslog.Debug, log << "Schedule: wait_pop: IPE: THE QUEUE IS EMPTY");
        return SchedPacket();
    }
    // The node is already removed from the heapset.

    // Extract the required data
    packet = itask->m_Packet;

    // Now remove it from the corresponding list.
    itask->m_pBaseList->erase(itask);

    IF_HEAVY_LOGGING(static string typenames[3] = {"REGULAR", "REXMIT", "CONTROL"});
    HLOGC(qslog.Debug, log << "Schedule: wait_pop: PICKUP from @" << packet.id()
            << " %" << packet.seqno()
            << " type=" << typenames[packet.type()]);

    return packet;
}

}
