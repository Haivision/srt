/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#include "socketoptions.hpp"
#include "verbose.hpp"

using namespace std;


extern const set<string> true_names = { "1", "yes", "on", "true" };
extern const set<string> false_names = { "0", "no", "off", "false" };

extern const std::map<std::string, int> enummap_transtype = {
    { "live", SRTT_LIVE },
    { "file", SRTT_FILE }
};


const char* const SocketOption::mode_names[3] = {
    "listener", "caller", "rendezvous"
};

SocketOption::Mode SrtInterpretMode(const string& modestr, const string& host, const string& adapter)
{
    SocketOption::Mode mode = SocketOption::FAILURE;

    if (modestr == "client" || modestr == "caller")
    {
        mode = SocketOption::CALLER;
    }
    else if (modestr == "server" || modestr == "listener")
    {
        mode = SocketOption::LISTENER;
    }
    else if (modestr == "rendezvous")
    {
        mode = SocketOption::RENDEZVOUS;
    }
    else if (modestr == "default")
    {
        // Use the following convention:
        // 1. Server for source, Client for target
        // 2. If host is empty, then always server.
        if ( host == "" )
            mode = SocketOption::LISTENER;
        //else if ( !dir_output )
        //mode = "server";
        else
        {
            // Host is given, so check also "adapter"
            if (adapter != "")
                mode = SocketOption::RENDEZVOUS;
            else
                mode = SocketOption::CALLER;
        }
    }
    else
    {
        mode = SocketOption::FAILURE;
    }

    return mode;
}

SocketOption::Mode SrtConfigurePre(SRTSOCKET socket, string host, map<string, string> options, vector<string>* failures)
{
    vector<string> dummy;
    vector<string>& fails = failures ? *failures : dummy;

    string modestr = "default", adapter;

    if (options.count("mode"))
    {
        modestr = options["mode"];
    }

    if (options.count("adapter"))
    {
        adapter = options["adapter"];
    }

    SocketOption::Mode mode = SrtInterpretMode(modestr, host, adapter);
    if (mode == SocketOption::FAILURE)
    {
        fails.push_back("mode");
    }

    if (options.count("linger"))
    {
        linger lin;
        lin.l_linger = stoi(options["linger"]);
        lin.l_onoff  = lin.l_linger > 0 ? 1 : 0;
        srt_setsockopt(socket, SocketOption::PRE, SRTO_LINGER, &lin, sizeof(linger));
    }


    bool all_clear = true;
    for (const auto &o: srt_options)
    {
        if ( o.binding == SocketOption::PRE && options.count(o.name) )
        {
            string value = options.at(o.name);
            bool ok = o.apply<SocketOption::SRT>(socket, value);
            if ( !ok )
            {
                fails.push_back(o.name);
                all_clear = false;
            }
        }
    }

    return all_clear ? mode : SocketOption::FAILURE;
}

void SrtConfigurePost(SRTSOCKET socket, map<string, string> options, vector<string>* failures)
{
    vector<string> dummy;
    vector<string>& fails = failures ? *failures : dummy;

    for (const auto &o: srt_options)
    {
        if ( o.binding == SocketOption::POST && options.count(o.name) )
        {
            string value = options.at(o.name);
            Verb() << "Setting option: " << o.name << " = " << value;
            bool ok = o.apply<SocketOption::SRT>(socket, value);
            if ( !ok )
                fails.push_back(o.name);
        }
    }
}

