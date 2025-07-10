
#include <sstream>
#include <iomanip>

#include "schedule_snd.h"
#include "core.h"

using namespace std;
using namespace srt;
using namespace srt::sync;

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
    if (broken)
        return SendTask::none();

    sync::ScopedLock lk (lock);
    SendTask::tasklist_t& wlist = taskmap[id];
    wlist.push_back(proto);
    SendTask::taskiter_t itask = --wlist.end();
    itask->m_pBaseList = &wlist;

    bool was_ready = have_task_ready();

    // Now enqueue it in tq
    size_t pos = tq.insert(itask);
    if (pos == 0) // earliest task
        about_time = tq.top()->m_tsSendTime; // INSERTED: will not be empty
    if (!was_ready && have_task_ready())
        task_ready.notify_all();
    return itask;
}

bool SendScheduler::have_task_ready()
{
    if (!tq.empty())
    {
        SendTask::taskiter_t earliest = tq.top();
        if (earliest->is_ready(clock_type::now()))
        {
            return true;
        }
    }
    return false;
}

bool SendScheduler::wait()
{
    UniqueLock lk (lock);
    for (;;)
    {
        if (broken)
            return false;
        if (have_task_ready())
            break;
        task_ready.wait(lk);
    }
    return true;
}

void SendScheduler::withdraw(socket_t id)
{
    sync::ScopedLock lk (lock);
    // Delete all tasks for the given socket id.
    // We have them collected in the list: taskmap

    SendTask::tasklist_t& id_list = taskmap[id];

    // As we know that all these items were added to tq,
    // we need to withdraw them all from tq.
    for (SendTask::taskiter_t idt = id_list.begin(); idt != id_list.end(); ++idt)
    {
        tq.erase(idt);
    }
    // The list should be empty, so delete the entry itself.
    taskmap.erase(id);
    // We don't know if the earliest in the queue was deleted,
    // so just rewrite it anyway.
    pop_update_time();
}

void SendScheduler::pop_update_time()
{
    if (!tq.empty())
        about_time = tq.top()->m_tsSendTime; // checked that ! empty
    else
        about_time = clock_time();
}

void SendScheduler::cancel(SendTask::taskiter_t itask)
{
    sync::ScopedLock lk (lock);
    cancel_nolock(itask);
}

void SendScheduler::cancel_nolock(SendTask::taskiter_t itask)
{
    tq.erase(itask);
    itask->m_pBaseList->erase(itask);
    pop_update_time();
}

SchedPacket SendScheduler::wait_pop()
{
    SchedPacket packet;
    sync::ScopedLock lk (lock);
    // Wait until the time has come to execute
    // the next task. Extract the task structure
    // and remove the task from the list.
    for (;;)
    {
        if (broken)
            break;
        bool have = wait();
        if (have)
            break;
    }
    // Here we are sure that the top() task is ready to execute
    SendTask::taskiter_t itask = tq.pop();
    pop_update_time();

    if (itask == SendTask::none())
        return SchedPacket();
    // The node is already removed from the heapset.

    // Extract the required data
    packet = itask->m_Packet;

    // Now remove it from the corresponding list.
    itask->m_pBaseList->erase(itask);

    return packet;
}

}
