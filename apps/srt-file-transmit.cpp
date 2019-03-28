/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/
#ifdef _WIN32
#include <direct.h>
#endif
#include <iostream>
#include <iterator>
#include <vector>
#include <map>
#include <stdexcept>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <cassert>
#include <sys/stat.h>
#include <srt.h>
#include <udt.h>

#include "apputil.hpp"
#include "uriparser.hpp"
#include "logsupport.hpp"
#include "socketoptions.hpp"
#include "transmitmedia.hpp"
#include "verbose.hpp"


#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#endif


bool Upload(UriParser& srt, UriParser& file);
bool Download(UriParser& srt, UriParser& file);

const logging::LogFA SRT_LOGFA_APP = 10;

static size_t g_buffer_size = 1456;
static bool g_skip_flushing = false;

using namespace std;

static bool interrupt = false;
void OnINT_ForceExit(int)
{
    Verb() << "\n-------- REQUESTED INTERRUPT!\n";
    interrupt = true;
}

int main( int argc, char** argv )
{
    set<string>
        o_loglevel = { "ll", "loglevel" },
        o_buffer = {"b", "buffer" },
        o_verbose = {"v", "verbose" },
        o_noflush = {"s", "skipflush" },
        o_fullstats = {"f", "fullstats" };

    // Options that expect no arguments (ARG_NONE) need not be mentioned.
    vector<OptionScheme> optargs = {
        { o_loglevel, OptionScheme::ARG_ONE },
        { o_buffer, OptionScheme::ARG_ONE },
        { o_noflush, OptionScheme::ARG_NONE },
        { o_fullstats, OptionScheme::ARG_NONE }
    };
    options_t params = ProcessOptions(argv, argc, optargs);

    /*
    cerr << "OPTIONS (DEBUG)\n";
    for (auto o: params)
    {
        cerr << "[" << o.first << "] ";
        copy(o.second.begin(), o.second.end(), ostream_iterator<string>(cerr, " "));
        cerr << endl;
    }
    */

    vector<string> args = params[""];
    if ( args.size() < 2 )
    {
        cerr << "Usage: " << argv[0] << " <source> <target>\n";
        return 1;
    }

    string loglevel = Option<OutString>(params, "error", o_loglevel);
    logging::LogLevel::type lev = SrtParseLogLevel(loglevel);
    UDT::setloglevel(lev);
    UDT::addlogfa(SRT_LOGFA_APP);

   string verbo = Option<OutString>(params, "no", o_verbose);
   if ( verbo == "" || !false_names.count(verbo) )
       Verbose::on = true;

    string bs = Option<OutString>(params, "", o_buffer);
    if ( bs != "" )
    {
        ::g_buffer_size = stoi(bs);
    }

    string sf = Option<OutString>(params, "no", o_noflush);
    if (sf == "" || !false_names.count(sf))
        ::g_skip_flushing = true;

    string sfull = Option<OutString>(params, "no", o_fullstats);
    if (sfull == "" || !false_names.count(sfull))
        ::transmit_total_stats = true;

    string source = args[0];
    string target = args[1];

    UriParser us(source), ut(target);

    Verb() << "SOURCE type=" << us.scheme() << ", TARGET type=" << ut.scheme();

    signal(SIGINT, OnINT_ForceExit);
    signal(SIGTERM, OnINT_ForceExit);

    try
    {
        if (us.scheme() == "srt")
        {
            if (ut.scheme() != "file")
            {
                cerr << "SRT to FILE should be specified\n";
                return 1;
            }
            Download(us, ut);
        }
        else if (ut.scheme() == "srt")
        {
            if (us.scheme() != "file")
            {
                cerr << "FILE to SRT should be specified\n";
                return 1;
            }
            Upload(ut, us);
        }
        else
        {
            cerr << "SRT URI must be one of given media.\n";
            return 1;
        }
    }
    catch (std::exception& x)
    {
        cerr << "ERROR: " << x.what() << endl;
        return 1;
    }


    return 0;
}

void ExtractPath(string path, ref_t<string> dir, ref_t<string> fname)
{
    //string& dir = r_dir;
    //string& fname = r_fname;

    string directory = path;
    string filename = "";

    struct stat state;
    stat(path.c_str(), &state);

    if (!S_ISDIR(state.st_mode))
    {
        // Extract directory as a butlast part of path
        size_t pos = path.find_last_of("/");
        if ( pos == string::npos )
        {
            filename = path;
            directory = ".";
        }
        else
        {
            directory = path.substr(0, pos);
            filename = path.substr(pos+1);
        }
    }

    if (directory[0] != '/')
    {
        // Glue in the absolute prefix of the current directory
        // to make it absolute. This is needed to properly interpret
        // the fixed uri.
        static const size_t s_max_path = 4096; // don't care how proper this is
        char tmppath[s_max_path];
        char* gwd = getcwd(tmppath, s_max_path);
        if ( !gwd )
        {
            // Don't bother with that now. We need something better for
            // that anyway.
            throw std::invalid_argument("Path too long");
        }
        string wd = gwd;

        directory = wd + "/" + directory;
    }

    *dir = directory;
    *fname = filename;
}

bool DoUpload(UriParser& ut, string path, string filename)
{
    bool result = false;
    unique_ptr<Target> tar;
    SRTSOCKET s = SRT_INVALID_SOCK;
    bool connected = false;
    int pollid = -1;

    ifstream ifile(path, ios::binary);
    if ( !ifile )
    {
        cerr << "Error opening file: '" << path << "'";
        goto exit;
    }

    pollid = srt_epoll_create();
    if ( pollid < 0 )
    {
        cerr << "Can't initialize epoll";
        goto exit;
    }


    while (!interrupt)
    {
        if (!tar.get())
        {
            int sockopt = SRTT_FILE;

            tar = Target::Create(ut.uri());
            if (!tar.get())
            {
                cerr << "Unsupported target type: " << ut.uri() << endl;
                goto exit;
            }

            srt_setsockflag(tar->GetSRTSocket(), SRTO_TRANSTYPE,
                &sockopt, sizeof sockopt);

            int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
            if (srt_epoll_add_usock(pollid,
                    tar->GetSRTSocket(), &events))
            {
                cerr << "Failed to add SRT destination to poll, "
                    << tar->GetSRTSocket() << endl;
                goto exit;
            }
            UDT::setstreamid(tar->GetSRTSocket(), filename);
        }

        s = tar->GetSRTSocket();
        assert(s != SRT_INVALID_SOCK);

        SRTSOCKET efd;
        int efdlen = 1;
        if (srt_epoll_wait(pollid,
            0, 0, &efd, &efdlen,
            100, nullptr, nullptr, 0, 0) < 0)
        {
            continue;
        }

        assert(efd == s);
        assert(efdlen == 1);

        SRT_SOCKSTATUS status = srt_getsockstate(s);
        Verb() << "Event with status " << status << "\n";

        switch (status)
        {
            case SRTS_LISTENING:
            {
                if (!tar->AcceptNewClient())
                {
                    cerr << "Failed to accept SRT connection" << endl;
                    goto exit;
                }

                srt_epoll_remove_usock(pollid, s);

                s = tar->GetSRTSocket();
                int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                if (srt_epoll_add_usock(pollid, s, &events))
                {
                    cerr << "Failed to add SRT client to poll" << endl;
                    goto exit;
                }
                cerr << "Target connected (listener)" << endl;
                connected = true;
            }
            break;
            case SRTS_CONNECTED:
            {
                if (!connected)
                {
                    cerr << "Target connected (caller)" << endl;
                    connected = true;
                }
            }
            break;
            case SRTS_BROKEN:
            case SRTS_NONEXIST:
            case SRTS_CLOSED:
            {
                cerr << "Target disconnected" << endl;
                goto exit;
            }
            default:
            {
                // No-Op
            }
            break;
        }

        if (connected)
        {
            vector<char> buf(::g_buffer_size);
            size_t n = ifile.read(buf.data(), ::g_buffer_size).gcount();
            size_t shift = 0;
            while (n > 0)
            {
                int st = srt_send(s, buf.data() + shift, n);
                Verb() << "Upload: " << n << " --> " << st
                    << (!shift ? string() : "+" + Sprint(shift));
                if (st == SRT_ERROR)
                {
                    cerr << "Upload: SRT error: " << srt_getlasterror_str()
                        << endl;
                    goto exit;
                }

                n -= st;
                shift += st;
            }

            if (ifile.eof())
            {
                cerr << "File sent" << endl;
                result = true;
                break;
            }

            if ( !ifile.good() )
            {
                cerr << "ERROR while reading file\n";
                goto exit;
            }

        }
    }

    if ( result && !::g_skip_flushing )
    {
        assert(s != SRT_INVALID_SOCK);

        // send-flush-loop
        result = false;
        while (!interrupt)
        {
            size_t bytes;
            size_t blocks;
            int st = srt_getsndbuffer(s, &blocks, &bytes);
            if (st == SRT_ERROR)
            {
                cerr << "Error in srt_getsndbuffer: " << srt_getlasterror_str()
                    << endl;
                goto exit;
            }
            if (bytes == 0)
            {
                cerr << "Buffers flushed" << endl;
                result = true;
                break;
            }
            Verb() << "Sending buffer still: bytes=" << bytes << " blocks="
                << blocks;
            this_thread::sleep_for(chrono::milliseconds(250));
        }
    }

exit:
    if (pollid >= 0)
    {
        srt_epoll_release(pollid);
    }

    return result;
}

bool DoDownload(UriParser& us, string directory, string filename)
{
    bool result = false;
    unique_ptr<Source> src;
    SRTSOCKET s = SRT_INVALID_SOCK;
    bool connected = false;
    int pollid = -1;
    string id;
    ofstream ofile;
    SRT_SOCKSTATUS status;
    SRTSOCKET efd;
    int efdlen = 1;

    pollid = srt_epoll_create();
    if ( pollid < 0 )
    {
        cerr << "Can't initialize epoll";
        goto exit;
    }

    while (!interrupt)
    {
        if (!src.get())
        {
            int sockopt = SRTT_FILE;

            src = Source::Create(us.uri());
            if (!src.get())
            {
                cerr << "Unsupported source type: " << us.uri() << endl;
                goto exit;
            }

            srt_setsockflag(src->GetSRTSocket(), SRTO_TRANSTYPE,
                &sockopt, sizeof sockopt);

            int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
            if (srt_epoll_add_usock(pollid,
                    src->GetSRTSocket(), &events))
            {
                cerr << "Failed to add SRT source to poll, "
                    << src->GetSRTSocket() << endl;
                goto exit;
            }
        }

        s = src->GetSRTSocket();
        assert(s != SRT_INVALID_SOCK);

        if (srt_epoll_wait(pollid,
            &efd, &efdlen, 0, 0,
            100, nullptr, nullptr, 0, 0) < 0)
        {
            continue;
        }

        assert(efd == s);
        assert(efdlen == 1);

        status = srt_getsockstate(s);
        Verb() << "Event with status " << status << "\n";

        switch (status)
        {
            case SRTS_LISTENING:
            {
                if (!src->AcceptNewClient())
                {
                    cerr << "Failed to accept SRT connection" << endl;
                    goto exit;
                }

                srt_epoll_remove_usock(pollid, s);

                s = src->GetSRTSocket();
                int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                if (srt_epoll_add_usock(pollid, s, &events))
                {
                    cerr << "Failed to add SRT client to poll" << endl;
                    goto exit;
                }
                id = UDT::getstreamid(s);
                cerr << "Source connected (listener), id ["
                    << id << "]" << endl;
                connected = true;
                continue;
            }
            break;
            case SRTS_CONNECTED:
            {
                if (!connected)
                {
                    id = UDT::getstreamid(s);
                    cerr << "Source connected (caller), id ["
                        << id << "]" << endl;
                    connected = true;
                }
            }
            break;
            case SRTS_BROKEN:
            case SRTS_NONEXIST:
            case SRTS_CLOSED:
            {
                cerr << "Source disconnected" << endl;
                goto exit;
            }
            break;
            default:
            {
                // No-Op
            }
            break;
        }

        if (connected)
        {
            vector<char> buf(::g_buffer_size);
            int n;

            if(!ofile.is_open())
            {
                const char * fn = id.empty() ? filename.c_str() : id.c_str();
                directory.append("/");
                directory.append(fn);
                ofile.open(directory.c_str(), ios::out | ios::trunc | ios::binary);

                if(!ofile.is_open())
                {
                    cerr << "Error opening file [" << directory << "]" << endl;
                    goto exit;
                }
                cerr << "Writing output to [" << directory << "]" << endl;
            }

            n = srt_recv(s, buf.data(), ::g_buffer_size);
            if (n == SRT_ERROR)
            {
                cerr << "Download: SRT error: " << srt_getlasterror_str() << endl;
                goto exit;
            }

            if (n == 0)
            {
                result = true;
                cerr << "Download COMPLETE.";
                break;
            }

            // Write to file any amount of data received
            Verb() << "Download: --> " << n;
            ofile.write(buf.data(), n);
            if (!ofile.good())
            {
                cerr << "Error writing file" << endl;
                goto exit;
            }

        }
    }

exit:
    if (pollid >= 0)
    {
        srt_epoll_release(pollid);
    }

    return result;
}

bool Upload(UriParser& srt_target_uri, UriParser& fileuri)
{
    if ( fileuri.scheme() != "file" )
    {
        cerr << "Upload: source accepted only as a file\n";
        return false;
    }
    // fileuri is source-reading file
    // srt_target_uri is SRT target

    string path = fileuri.path();
    string directory, filename;
    ExtractPath(path, ref(directory), ref(filename));
    Verb() << "Extract path '" << path << "': directory=" << directory << " filename=" << filename;
    // Set ID to the filename.
    // Directory will be preserved.

    // Add some extra parameters.
    srt_target_uri["transtype"] = "file";

    return DoUpload(srt_target_uri, path, filename);
}

bool Download(UriParser& srt_source_uri, UriParser& fileuri)
{
    if (fileuri.scheme() != "file" )
    {
        cerr << "Download: target accepted only as a file\n";
        return false;
    }

    string path = fileuri.path(), directory, filename;
    ExtractPath(path, Ref(directory), Ref(filename));
    Verb() << "Extract path '" << path << "': directory=" << directory << " filename=" << filename;

    return DoDownload(srt_source_uri, directory, filename);
}

