// Verifies a BSTOW file that refers to external MPEG-TS files:
// the checksum of every declared file, and whether all declared
// offsets and lengths can be extracted from them.

#include <iostream>
#include <string>

#include "bstow-verify.hpp"

using namespace std;

int main(int argc, char** argv)
{
    bool first_only = false;
    bool help = false;
    string path;

    for (int i = 1; i < argc; ++i)
    {
        string a = argv[i];
        if (a == "-first")
            first_only = true;
        else if (a == "-h" || a == "--help" || !path.empty())
            help = true;
        else
            path = a;
    }

    if (help || path.empty())
    {
        cerr << "Usage: " << argv[0] << " [-first] <file.bs>\n"
            "  -first   stop at the first error (default: report all errors)\n";
        return 2;
    }

    string lastfile;
    bool any = false;
    bstow::Verifier verifier(path, [&](const string& file, int64_t pos, const string& msg)
    {
        if (!any || file != lastfile)
            cerr << "ERROR in file '" << file << "':\n";
        any = true;
        lastfile = file;
        cerr << "  offset " << pos << ": " << msg << "\n";
    }, first_only);

    size_t errors = verifier.Run();
    if (errors)
    {
        cerr << errors << " error(s) found\n";
        return 1;
    }

    cerr << "OK\n";
    return 0;
}
