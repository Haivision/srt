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


bool Upload(UriParser& srt, UriParser& file, std::ostream &out_stats);
bool Download(UriParser& srt, UriParser& file, std::ostream &out_stats);

const srt_logging::LogFA SRT_LOGFA_APP = 10;

static size_t g_buffer_size = 1456;
static bool g_skip_flushing = false;

map<string, string> g_options;

string Option(string deflt = "") { return deflt; }

template <class... Args>
string Option(string deflt, string key, Args... further_keys)
{
    map<string, string>::iterator i = g_options.find(key);
    if (i == g_options.end())
        return Option(deflt, further_keys...);
    return i->second;
}

using namespace std;

static bool interrupt = false;
void OnINT_ForceExit(int)
{
    Verb() << "\n-------- REQUESTED INTERRUPT!\n";
    interrupt = true;
}

int main( int argc, char** argv )
{
    vector<string> args;
    copy(argv + 1, argv + argc, back_inserter(args));

    // Check options
    vector<string> params;

    for (string a : args)
    {
        if (a[0] == '-')
        {
            string key = a.substr(1);
            size_t pos = key.find(':');
            if (pos == string::npos)
                pos = key.find(' ');
            string value = pos == string::npos ? "" : key.substr(pos + 1);
            key = key.substr(0, pos);
            g_options[key] = value;
            continue;
        }

        params.push_back(a);
    }

    if (params.size() != 2)
    {
        cerr << "Usage: " << argv[0] << " [options] <input-uri> <output-uri>\n";
        cerr << "\t-t:<timeout=0> - exit timer in seconds\n";
        cerr << "\t-c:<chunk=1316> - max size of data read in one step\n";
        cerr << "\t-b:<bandwidth> - set SRT bandwidth\n";
        cerr << "\t-buffer:<buffer> - set SRT buffer\n";
        cerr << "\t-r:<report-frequency=0> - bandwidth report frequency\n";
        cerr << "\t-s:<stats-report-freq=0> - frequency of status report\n";
        cerr << "\t-pf:<format> - printformat (json or default)\n";
        cerr << "\t-statsreport:<filename> - stats report file name (cout for output to cout, or a filename)\n";
        cerr << "\t-f - full counters in stats-report (prints total statistics)\n";
        cerr << "\t-q - quiet mode (default no)\n";
        cerr << "\t-v - verbose mode (default no)\n";
        return 1;
    }

    int timeout = stoi(Option("0", "t", "to", "timeout"), 0, 0);
    unsigned long chunk = stoul(Option("0", "c", "chunk"), 0, 0);
    if (chunk == 0)
    {
        chunk = SRT_LIVE_DEF_PLSIZE;
    }
    else
    {
        transmit_chunk_size = chunk;
    }

    bool quiet = Option("no", "q", "quiet") != "no";
    Verbose::on = !quiet && Option("no", "v", "verbose") != "no";
    string loglevel = Option("error", "loglevel");
    string logfa = Option("general", "logfa");
    string logfile = Option("", "logfile");
    bool autoreconnect = Option("yes", "a", "auto") != "no";
    transmit_total_stats = Option("no", "f", "fullstats") != "no";

    const string sf = Option("no", "skipflush");
    if (sf == "" || !false_names.count(sf))
        ::g_skip_flushing = true;

    const string bs = Option("", "buffer");
    if (bs != "")
        ::g_buffer_size = stoi(bs);


    // Print format
    const string pf = Option("default", "pf", "printformat");
    if (pf == "json")
    {
        printformat = PRINT_FORMAT_JSON;
    }
    if (pf == "csv")
    {
        printformat = PRINT_FORMAT_CSV;
    }
    else if (pf != "default")
    {
        cerr << "ERROR: Unsupported print format: " << pf << endl;
        return 1;
    }

    try
    {
        transmit_bw_report = stoul(Option("0", "r", "report", "bandwidth-report", "bitrate-report"));
        transmit_stats_report = stoul(Option("0", "s", "stats", "stats-report-frequency"));
    }
    catch (std::invalid_argument &)
    {
        cerr << "ERROR: Incorrect integer number specified for an option.\n";
        return 1;
    }


    std::ofstream logfile_stream; // leave unused if not set

    srt_setloglevel(SrtParseLogLevel(loglevel));
    set<srt_logging::LogFA> fas = SrtParseLogFA(logfa);
    for (set<srt_logging::LogFA>::iterator i = fas.begin(); i != fas.end(); ++i)
        srt_addlogfa(*i);


    std::ofstream logfile_stats; // leave unused if not set
    const string statsfile = Option("cout", "statsfile");
    if (statsfile != "" && statsfile != "cout")
    {
        logfile_stats.open(statsfile.c_str());
        if (!logfile_stats)
        {
            cerr << "ERROR: Can't open '" << statsfile << "' for writing stats. Fallback to cout.\n";
            logfile_stats.close();
        }
    }

    ostream &out_stats = logfile_stats.is_open() ? logfile_stats : cout;

    string source = params[0];
    string target = params[1];

    UriParser us(source), ut(target);

    Verb() << "SOURCE type=" << us.scheme() << ", TARGET type=" << ut.scheme();

    signal(SIGINT,  OnINT_ForceExit);
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
            Download(us, ut, out_stats);
        }
        else if (ut.scheme() == "srt")
        {
            if (us.scheme() != "file")
            {
                cerr << "FILE to SRT should be specified\n";
                return 1;
            }
            Upload(ut, us, out_stats);
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

bool DoUpload(UriParser& ut, string path, string filename, std::ostream &out_stats)
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
                int st = tar->Write(buf.data() + shift, n, out_stats);
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

bool DoDownload(UriParser& us, string directory, string filename, std::ostream &out_stats)
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

            n = src->Read(::g_buffer_size, buf, out_stats);
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

bool Upload(UriParser& srt_target_uri, UriParser& fileuri, std::ostream &out_stats)
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

    return DoUpload(srt_target_uri, path, filename, out_stats);
}

bool Download(UriParser& srt_source_uri, UriParser& fileuri, std::ostream &out_stats)
{
    if (fileuri.scheme() != "file" )
    {
        cerr << "Download: target accepted only as a file\n";
        return false;
    }

    string path = fileuri.path(), directory, filename;
    ExtractPath(path, Ref(directory), Ref(filename));
    Verb() << "Extract path '" << path << "': directory=" << directory << " filename=" << filename;

    return DoDownload(srt_source_uri, directory, filename, out_stats);
}

