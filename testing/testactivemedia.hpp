#include <string>
#include <sstream>
#include <vector>
#include <list>
#include <memory>
#include <exception>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include "testmedia.hpp"
#include "logsupport.hpp"

#define SRT_ENABLE_VERBOSE_LOCK 1
#include "verbose.hpp"

#include "logging.h"
#include "threadname.h"

extern srt_logging::Logger applog;

template<class MediumDir>
struct Medium
{
    MediumDir* med = nullptr;
    std::unique_ptr<MediumDir> pinned_med;
    std::list<MediaPacket> buffer;
    std::mutex buffer_lock;
    std::thread thr;
    std::condition_variable ready;
    std::atomic<bool> running {false};
    std::exception_ptr xp; // To catch exception thrown by a thread

    virtual void Runner() = 0;

    void RunnerBase()
    {
        try
        {
            running = true;
            Runner();
        }
        catch (...)
        {
            xp = std::current_exception();
        }

        //Verb() << "Medium: " << this << ": thread exit";
        std::unique_lock<std::mutex> g(buffer_lock);
        running = false;
        ready.notify_all();
        //Verb() << VerbLock << "Medium: EXIT NOTIFIED";
    }

    void run()
    {
        running = true;
        std::ostringstream tns;
        tns << typeid(*this).name() << ":" << this;
        ThreadName tn(tns.str().c_str());
        thr = thread( [this] { RunnerBase(); } );
    }

    void quit()
    {
        if (!med)
            return;

        LOGP(applog.Debug, "Medium(", typeid(*med).name(), ") quit. Buffer contains ", buffer.size(), " blocks");

        std::string name;
        if (Verbose::on)
            name = typeid(*med).name();

        med->Close();
        if (thr.joinable())
        {
            LOGP(applog.Debug, "Medium::quit: Joining medium thread (", name, ") ...");
            thr.join();
            LOGP(applog.Debug, "... done");
        }

        if (xp)
        {
            try {
                std::rethrow_exception(xp);
            } catch (TransmissionError& e) {
                if (Verbose::on)
                    Verb() << VerbLock << "Medium " << this << " exited with Transmission Error:\n\t" << e.what();
                else
                    cerr << "Transmission Error: " << e.what() << endl;
            } catch (...) {
                if (Verbose::on)
                    Verb() << VerbLock << "Medium " << this << " exited with UNKNOWN EXCEPTION:";
                else
                    cerr << "UNKNOWN EXCEPTION on medium\n";
            }
        }

        // Prevent further quits from running
        med = nullptr;
    }

    void Setup(MediumDir* t)
    {
        med = t;
        // Leave pinned_med as 0
    }

    void Setup(std::unique_ptr<MediumDir>&& medbase)
    {
        pinned_med = std::move(medbase);
        med = pinned_med.get();
    }

    virtual ~Medium()
    {
        //Verb() << "Medium: " << this << " DESTROYED. Threads quit.";
        quit();
    }

    virtual void Start() { run(); }
    virtual void Stop() { quit(); }
};

struct SourceMedium: Medium<Source>
{
    size_t chunksize_ = 0;
    typedef Medium<Source> Base;

    // Source Runner: read payloads and put on the buffer
    void Runner() override;

    // External user: call this to get the buffer.
    MediaPacket Extract();

    template<class Arg>
    void Setup(Arg&& medium, size_t chunksize)
    {
        chunksize_ = chunksize;
        return Base::Setup(std::move(medium));
    }
};

struct TargetMedium: Medium<Target>
{
    void Runner() override;

    bool Schedule(const MediaPacket& data)
    {
        LOGP(applog.Debug, "TargetMedium::Schedule LOCK ... ");
        lock_guard<mutex> lg(buffer_lock);
        LOGP(applog.Debug, "TargetMedium::Schedule LOCKED - checking: running=", running, " interrupt=", ::transmit_int_state);
        if (!running || ::transmit_int_state)
        {
            LOGP(applog.Debug, "TargetMedium::Schedule: not running, discarding packet");
            return false;
        }

        LOGP(applog.Debug, "TargetMedium(", typeid(*med).name(), "): Schedule: [", data.payload.size(), "] CLIENT -> BUFFER");
        buffer.push_back(data);
        ready.notify_one();
        return true;
    }

    void Clear()
    {
        lock_guard<mutex> lg(buffer_lock);
        buffer.clear();
    }

    void Interrupt()
    {
        lock_guard<mutex> lg(buffer_lock);
        running = false;
        ready.notify_one();
    }

    ~TargetMedium()
    {
        //Verb() << "TargetMedium: DESTROYING";
        Interrupt();
        // ~Medium will do quit() additionally, which joins the thread
    }
};


