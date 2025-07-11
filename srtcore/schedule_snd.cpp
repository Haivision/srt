
#include <sstream>
#include <iomanip>

#include "schedule_snd.h"
#include "core.h"
#include "logging.h"

using namespace std;
using namespace srt;
using namespace srt::sync;

using srt_logging::qslog;

namespace srt
{

SchedPacket::SchedPacket(CUDTSocket* sock, int32_t seqno, sched::Type t):
    m_Socket(CUDT::uglobal())
{
    if (sock)
        m_Socket = CUDT::keep(sock);

    m_iSeqNo = seqno;
    m_Type = t;
}

void SchedPacket::set_socket(CUDTSocket* sock)
{
    if (sock)
        m_Socket = CUDT::keep(sock);
}

static const char* const schedtype [] = {"regular", "rexmit", "pf-control"};

std::string SendTask::print(SendTask::taskiter_t v)
{
    std::ostringstream out;
    // Complicated pre-C++20 time formatting...
    time_t tval = count_seconds(v->m_tsSendTime.time_since_epoch());

    int64_t total_usec = count_microseconds(v->m_tsSendTime.time_since_epoch());
    int64_t usec = total_usec - (tval * 1000000);

    struct tm tme;
    localtime_r(&tval, &tme);
    out << "<" << std::put_time(&tme, "%T") << ".";
    out << setw(6) << setfill('0') << usec << "> @" << v->m_Packet.m_Socket.id();
    int32_t seq = v->m_Packet.seqno();
    if (seq == SRT_SEQNO_NONE)
    {
        out << " [empty]";
    }
    else
    {
        out << " [";
        out << schedtype[v->m_Packet.type()];
        out << "] %" << seq;
    }
    return out.str();
}

std::list<SendTask> SendTask::free_list;

SendTask::taskiter_t SendScheduler::enqueue_task(socket_t id, const SendTask& proto)
{
    if (m_bBroken)
    {
        HLOGC(qslog.Debug, log << "Schedule: ENQ: DENIED, schedule is broken");
        return SendTask::none();
    }

    sync::ScopedLock lk (m_Lock);
    SendTask::tasklist_t& wlist = m_TaskMap[id];
    wlist.push_back(proto);
    SendTask::taskiter_t itask = --wlist.end();
    itask->m_pBaseList = &wlist;

    bool was_ready = have_task_ready();

    // Now enqueue it in m_TaskQueue
    size_t pos = m_TaskQueue.insert(itask);

    IF_HEAVY_LOGGING(bool was_first = false);
    if (pos == 0) // earliest task
    {
        m_tsAboutTime = m_TaskQueue.top()->m_tsSendTime; // INSERTED: will not be empty
        IF_HEAVY_LOGGING(was_first = true);
    }

    if (!was_ready && have_task_ready())
    {
        HLOGC(qslog.Debug, log << "Schedule: ENQ: new READY task at T=" << FormatTime(itask->m_tsSendTime)
                << (was_first ? " (NEW TOP)" : "") << " - NOTIFY");
        m_TaskReadyCond.notify_all();
    }
    else
    {
        HLOGC(qslog.Debug, log << "Schedule: ENQ: new task at T=" << FormatTime(itask->m_tsSendTime)
                << (was_first ? " (NEW TOP)" : "") << (!was_ready ? " (NOT READY YET)" : " (?)"));
    }
    return itask;
}

bool SendScheduler::have_task_ready()
{
    if (!m_TaskQueue.empty())
    {
        SendTask::taskiter_t earliest = m_TaskQueue.top();
        if (earliest->is_ready(clock_type::now()))
        {
            return true;
        }
    }
    return false;
}

bool SendScheduler::wait()
{
    UniqueLock lk (m_Lock);
    typedef steady_clock::time_point ClockTime;
    for (;;)
    {
        if (m_bBroken)
        {
            HLOGC(qslog.Debug, log << "Schedule: WAIT: not waiting, schedule is broken");
            return false;
        }

        IF_HEAVY_LOGGING(ClockTime now = steady_clock::now());
        if (have_task_ready())
        {
            IF_HEAVY_LOGGING(ClockTime next = m_TaskQueue.top()->m_tsSendTime);
            HLOGC(qslog.Debug, log << "Schedule: WAIT: task ready since " << FormatDurationAuto(now - next));
            break;
        }

#if ENABLE_HEAVY_LOGGING
        if (m_TaskQueue.empty())
        {
            LOGC(qslog.Debug, log << "Schedule: WAIT: task NOT ready, NO NEW TASKS, WAIT FOR SIGNAL");
        }
        else
        {
            ClockTime next = m_TaskQueue.top()->m_tsSendTime;
            LOGC(qslog.Debug, log << "Schedule: WAIT: task not ready, next in "
                    << FormatDurationAuto(next - now) << " at T=" << FormatTime(next) << " - WAIT FOR READY");
        }
#endif

        m_TaskReadyCond.wait(lk);
    }
    return true;
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

    IF_HEAVY_LOGGING(string nextone = m_TaskQueue.empty()
            ? string("NO NEXT TASK")
            : "next in " + FormatDurationAuto(m_tsAboutTime - steady_clock::now()) + " from @" + Sprint(m_TaskQueue.top()->m_Packet.id()));

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
    sync::ScopedLock lk (m_Lock);
    // Wait until the time has come to execute
    // the next task. Extract the task structure
    // and remove the task from the list.
    for (;;)
    {
        if (m_bBroken)
        {
            HLOGC(qslog.Debug, log << "Schedule: wait_pop: broken");
            break;
        }
        bool have = wait();
        if (have)
        {
            break;
        }
        else
        {
            HLOGC(qslog.Debug, log << "Schedule: wait_pop: SPURIOUS");
        }
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
    HLOGC(qslog.Debug, log << "Schedule: wait_pop: PICKUP from @" << packet.id() << " %" << packet.seqno() << " type=" << typenames[packet.type()]);

    return packet;
}

}
