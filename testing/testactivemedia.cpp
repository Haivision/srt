
#include "testactivemedia.hpp"

void SourceMedium::Runner()
{
    ThreadName::set("SourceRN");

    Verb() << VerbLock << "Starting SourceMedium: " << this;
    for (;;)
    {
        auto input = med->Read(chunksize_);
        if (input.payload.empty() && med->End())
        {
            Verb() << VerbLock << "Exiting SourceMedium: " << this;
            return;
        }
        LOGP(applog.Debug, "SourceMedium(", typeid(*med).name(), "): [", input.payload.size(), "] MEDIUM -> BUFFER. signal(", &ready, ")");

        lock_guard<mutex> g(buffer_lock);
        buffer.push_back(input);
        ready.notify_one();
    }
}

MediaPacket SourceMedium::Extract()
{
    unique_lock<mutex> g(buffer_lock);
    for (;;)
    {
        if (::transmit_int_state)
            running = false;

        if (!buffer.empty())
        {
            MediaPacket top;
            swap(top, *buffer.begin());
            buffer.pop_front();
            LOGP(applog.Debug, "SourceMedium(", typeid(*med).name(), "): [", top.payload.size(), "] BUFFER -> CLIENT");
            return top;
        }
        else
        {
            // Don't worry about the media status as long as you have somthing in the buffer.
            // Purge the buffer first, then worry about the other things.
            if (!running)
            {
                //LOGP(applog.Debug, "Extract(", typeid(*med).name(), "): INTERRUPTED READING");
                //Verb() << "SourceMedium " << this << " not running";
                return {};
            }

        }

        // Block until ready
        //LOGP(applog.Debug, "Extract(", typeid(*med).name(), "): ", this, " wait(", &ready, ") -->");

        ready.wait_for(g, chrono::seconds(1), [this] { return running && !buffer.empty(); });

        // LOGP(applog.Debug, "Extract(", typeid(*med).name(), "): ", this, " <-- notified (running:"
        //     << boolalpha << running << " buffer:" << buffer.size() << ")");
    }
}

void TargetMedium::Runner()
{
    ThreadName::set("TargetRN");
    auto on_return_set = OnReturnSet(running, false);
    Verb() << VerbLock << "Starting TargetMedium: " << this;
    for (;;)
    {
        MediaPacket val;
        {
            unique_lock<mutex> lg(buffer_lock);
            if (buffer.empty())
            {
                if (!running)
                {
                    //LOGP(applog.Debug, "TargetMedium(", typeid(*med).name(), "): buffer empty, medium stopped, exiting.");
                    return;
                }

                bool gotsomething = ready.wait_for(lg, chrono::seconds(1), [this] { return !running || !buffer.empty(); } );
                LOGP(applog.Debug, "TargetMedium(", typeid(*med).name(), "): [", val.payload.size(), "] BUFFER update (timeout:",
                        boolalpha, gotsomething, " running: ", running, ")");
                if (::transmit_int_state || !running || !med || med->Broken())
                {
                    LOGP(applog.Debug, "TargetMedium(", typeid(*med).name(), "): buffer empty, medium ",
                                   (!::transmit_int_state ?
                                           (running ?
                                                (med ?
                                                    (med->Broken() ? "broken" : "UNKNOWN")
                                                : "deleted")
                                           : "stopped")
                                      : "killed"));
                    return;
                }
                if (!gotsomething) // exit on timeout
                    continue;
            }
            swap(val, *buffer.begin());
            LOGP(applog.Debug, "TargetMedium(", typeid(*med).name(), "): [", val.payload.size(), "] BUFFER extraction");

            buffer.pop_front();
        }

        // Check before writing
        if (med->Broken())
        {
            LOGP(applog.Debug, "TargetMedium(", typeid(*med).name(), "): [", val.payload.size(), "] BUFFER -> DISCARDED (medium broken)");
            running = false;
            return;
        }

        LOGP(applog.Debug, "TargetMedium(", typeid(*med).name(), "): [", val.payload.size(), "] BUFFER -> MEDIUM");
        // You get the data to send, send them.
        med->Write(val);
    }
}


