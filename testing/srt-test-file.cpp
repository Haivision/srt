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
#include <tuple>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <srt.h>
#include <udt.h>
#include <logging.h>

#include "apputil.hpp"
#include "uriparser.hpp"
#include "logsupport.hpp"
#include "socketoptions.hpp"
#include "verbose.hpp"
#include "testmedia.hpp"

#ifndef S_ISDIR
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#endif

bool Upload(UriParser& srt, UriParser& file);
bool Download(UriParser& srt, UriParser& file);

static size_t g_buffer_size = 1456;
static bool g_skip_flushing = false;

using namespace std;

srt_logging::Logger applog(SRT_LOGFA_APP, srt_logger_config, "srt-file");

int main( int argc, char** argv )
{
    vector<OptionScheme> optargs;

    OptionName
        o_loglevel ((optargs), "<severity=fatal|error|note|warning|debug> Minimum severity for logs", "ll",  "loglevel"),
        o_buffer   ((optargs), "<size[b]=1456> Size of the single reading operation", "b", "buffer"),
        o_verbose  ((optargs), " Print extra verbos output", "v",   "verbose"),
        o_noflush  ((optargs), " Do not wait safely 5 seconds at the end to flush buffers", "sf",  "skipflush"),
        o_help      ((optargs), " This help", "?",   "help", "-help")
            ;

    options_t params = ProcessOptions(argv, argc, optargs);

    bool need_help = OptionPresent(params, o_help);

    //*
    cerr << "OPTIONS (DEBUG)\n";
    for (auto o: params)
    {
        cerr << "[" << o.first << "] ";
        copy(o.second.begin(), o.second.end(), ostream_iterator<string>(cerr, " "));
        cerr << endl;
    }
    // */

    if (need_help)
    {
        cerr << "Usage:\n";
        cerr << "     " << argv[0] << " [options] <input> <output>\n";
        cerr << "*** (Position of [options] is unrestricted.)\n";
        cerr << "*** (<variadic...> option parameters can be only terminated by a next option.)\n";
        cerr << "where:\n";
        cerr << "    <input> and <output> is specified by an URI.\n";
        cerr << "SUPPORTED URI SCHEMES:\n";
        cerr << "    srt: use SRT connection\n";
        cerr << "    udp: read from bound UDP socket or send to given address as UDP\n";
        cerr << "    file (default if scheme not specified) specified as:\n";
        cerr << "       - empty host/port and absolute file path in the URI\n";
        cerr << "       - only a filename, also as a relative path\n";
        cerr << "       - file://con ('con' as host): designates stdin or stdout\n";
        cerr << "OPTIONS HELP SYNTAX: -option <parameter[unit]=default[meaning]>:\n";
        for (auto os: optargs)
            cout << OptionHelpItem(*os.pid) << endl;
        return 1;
    }

    vector<string> args = params[""];
    if ( args.size() < 2 )
    {
        cerr << "Usage: " << argv[0] << " <source> <target>\n";
        return 1;
    }

    string loglevel = Option<OutString>(params, "error", o_loglevel);
    srt_logging::LogLevel::type lev = SrtParseLogLevel(loglevel);
    srt::setloglevel(lev);
    srt::addlogfa(SRT_LOGFA_APP);

    bool verbo = OptionPresent(params, o_verbose);
    if (verbo)
    {
        Verbose::on = true;
        Verbose::cverb = &std::cout;
    }

    string bs = Option<OutString>(params, "", o_buffer);
    if ( bs != "" )
    {
        ::g_buffer_size = stoi(bs);
    }

    string sf = Option<OutString>(params, "no", o_noflush);
    if (sf == "" || !false_names.count(sf))
        ::g_skip_flushing = true;

    string source = args[0];
    string target = args[1];

    UriParser us(source), ut(target);

    Verb() << "SOURCE type=" << us.scheme() << ", TARGET type=" << ut.scheme();

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

tuple<string, string> ExtractPath(string path)
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
            // Don't bother with that now. We need something better for that anyway.
            throw std::invalid_argument("Path too long");
        }
        string wd = gwd;

        directory = wd + "/" + directory;
    }

    return make_tuple(directory, filename);
}

bool DoUpload(UriParser& ut, string path, string filename)
{
    SrtModel m(ut.host(), ut.portno(), ut.parameters());

    string id = filename;
    Verb() << "Passing '" << id << "' as stream ID\n";

    m.Establish((id));

    // Check if the filename was changed
    if (id != filename)
    {
        cerr << "SRT caller has changed the filename '" << filename << "' to '" << id << "' - rejecting\n";
        return false;
    }

    Verb() << "USING ID: " << id;

    // SrtTarget* tp = new SrtTarget;
    // tp->StealFrom(m);
    // unique_ptr<Target> target(tp);

    //SRTSOCKET ss = tp->Socket();
    SRTSOCKET ss = m.Socket();

    // Use a manual loop for reading from SRT
    vector<char> buf(::g_buffer_size);

    ifstream ifile(path, ios::binary);
    if ( !ifile )
    {
        cerr << "Error opening file: '" << path << "'";
        return false;
    }

    for (;;)
    {
        size_t n = ifile.read(buf.data(), ::g_buffer_size).gcount();
        size_t shift = 0;
        while (n > 0)
        {
            int st = srt_send(ss, buf.data()+shift, int(n));
            Verb() << "Upload: " << n << " --> " << st << (!shift ? string() : "+" + Sprint(shift));
            if (st == SRT_ERROR)
            {
                cerr << "Upload: SRT error: " << srt_getlasterror_str() << endl;
                return false;
            }

            n -= st;
            shift += st;
        }

        if (ifile.eof())
            break;

        if ( !ifile.good() )
        {
            cerr << "ERROR while reading file\n";
            return false;
        }
    }

    if ( !::g_skip_flushing )
    {
        // send-flush-loop

        for (;;)
        {
            size_t bytes;
            size_t blocks;
            int st = srt_getsndbuffer(ss, &blocks, &bytes);
            if (st == SRT_ERROR)
            {
                cerr << "Error in srt_getsndbuffer: " << srt_getlasterror_str() << endl;
                return false;
            }
            if (bytes == 0)
            {
                Verb() << "Sending buffer DEPLETED - ok.";
                break;
            }
            Verb() << "Sending buffer still: bytes=" << bytes << " blocks=" << blocks;
            this_thread::sleep_for(chrono::milliseconds(250));
        }
    }

    return true;
}

bool DoDownload(UriParser& us, string directory, string filename)
{
    SrtModel m(us.host(), us.portno(), us.parameters());

    string id = filename;
    m.Establish((id));

    // Disregard the filename, unless the destination file exists.

    string path = directory + "/" + id;
    struct stat state;
    if ( stat(path.c_str(), &state) == -1 )
    {
        switch ( errno )
        {
        case ENOENT:
            // This is expected, go on.
            break;

        default:
            cerr << "Download: error '" << errno << "'when checking destination location: " << path << endl;
            return false;
        }
    }
    else
    {
        // Check if destination is a regular file, if so, allow to overwrite.
        // Otherwise reject.
        if (!S_ISREG(state.st_mode))
        {
            cerr << "Download: target location '" << path << "' does not designate a regular file.\n";
            return false;
        }
    }

    ofstream ofile(path, ios::out | ios::trunc | ios::binary);
    if ( !ofile.good() )
    {
        cerr << "Download: can't create output file: " << path;
        return false;
    }
    SRTSOCKET ss = m.Socket();

    Verb() << "Downloading from '" << us.uri() << "' to '" << path;

    vector<char> buf(::g_buffer_size);

    for (;;)
    {
        int n = srt_recv(ss, buf.data(), int(::g_buffer_size));
        if (n == SRT_ERROR)
        {
            cerr << "Download: SRT error: " << srt_getlasterror_str() << endl;
            return false;
        }

        if (n == 0)
        {
            Verb() << "Download COMPLETE.";
            break;
        }

        // Write to file any amount of data received

        Verb() << "Download: --> " << n;
        ofile.write(buf.data(), n);
    }

    return true;
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
    tie(directory, filename) = ExtractPath(path);
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
    tie(directory, filename) = ExtractPath(path);

    srt_source_uri["transtype"] = "file";

    return DoDownload(srt_source_uri, directory, filename);
}

