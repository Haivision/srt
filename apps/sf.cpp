
#include <iostream>
#include <iterator>
#include <vector>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <srt.h>
#include <udt.h>

#include "appcommon.hpp"
#include "uriparser.hpp"
#include "socketoptions.hpp"
#include "transmitbase.hpp"
#include "transmitmedia.hpp"


bool Upload(UriParser& srt, UriParser& file);
bool Download(UriParser& srt, UriParser& file);

int main( int argc, char** argv )
{
    vector<OptionScheme> optargs; // maybe later
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

    string vrb = Option<OutString>(params, "no", "verbose", "v");

    ::transmit_verbose = (vrb != "no");

    string source = args[0];
    string target = args[1];

    UriParser us(source), ut(target);

    Verb() << "SOURCE type=" << us.scheme() << ", TARGET type=" << ut.scheme();

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


    return 0;
}

void ExtractPath(string path, ref_t<string> dir, ref_t<string> fname)
{
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
        static const size_t MAX_PATH = 4096; // don't care how proper this is
        char tmppath[MAX_PATH];
        char* gwd = getcwd(tmppath, MAX_PATH);
        if ( !gwd )
        {
            // Don't bother with that now. We need something better for that anyway.
            throw std::invalid_argument("Path too long");
        }
        string wd = gwd;

        directory = wd + "/" + directory;
    }

    dir = directory;
    fname = filename;
}

bool DoUpload(UriParser& ut, string path, string filename)
{
    SrtModel m(ut.host(), ut.portno(), ut.parameters());

    string id = filename;
    if ( !m.Establish(Ref(id)) )
        return false;

    // Check if the filename was changed
    if (id != filename)
    {
        cerr << "SRT caller has changed the filename '" << filename << "' to '" << id << "' - rejecting\n";
        return false;
    }

    SrtTarget* tp = new SrtTarget;
    tp->StealFrom(m);
    unique_ptr<Target> target(tp);

    SRTSOCKET ss = tp->Socket();

    // Use a manual loop for reading from SRT
    char buf[4096];

    ifstream ifile(path);
    if ( !ifile )
    {
        cerr << "Error opening file: '" << path << "'";
        return false;
    }

    for (;;)
    {
        size_t n = ifile.read(buf, 4096).gcount();
        if (n > 0)
        {
            srt_send(ss, buf, n);
        }

        if (ifile.eof())
            break;

        if ( !ifile.good() )
        {
            cerr << "ERROR while reading file\n";
            return false;
        }
    }

    return true;
}

bool DoDownload(UriParser& us, string directory, string filename)
{
    SrtModel m(us.host(), us.portno(), us.parameters());

    string id = filename;
    if (m.Establish(Ref(id)))
        return false;

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
        // Check if destination is a regular file, of so, allow to overwrite.
        // Otherwise reject.
        if (!S_ISREG(state.st_mode))
        {
            cerr << "Download: target location '" << path << "' does not designate a regular file.\n";
            return false;
        }
    }

    ofstream ofile(path);
    SRTSOCKET ss = m.Socket();

    char buf[4096];

    for (;;)
    {
        int n = srt_recv(ss, buf, 4096);
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
        ofile.write(buf, n);
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

    string path = fileuri.path(), directory, filename;
    ExtractPath(path, Ref(directory), Ref(filename));
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

    srt_source_uri["transtype"] = "file";

    return DoDownload(srt_source_uri, directory, filename);
}

