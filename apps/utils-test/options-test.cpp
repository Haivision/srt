
// NOTE: DEMONSTRATION purposes only.

#include "../../logging/ofmt_iostream.h"
#include <iomanip>

#include "../options.hpp"

using namespace hvu;
using namespace std;

int main( int argc, char** argv )
{
    OptionHandler optargs;

    OptionName
        o_timeout ((optargs), "<timeout[s]=0> Data transmission timeout", "t",   "to", "timeout" ),
        o_logfa   ((optargs), "<FA=FA-list...> Enabled Functional Areas (see --help logging)", "lfa", "logfa"),
        o_verbose ((optargs), "[channel=0|1|./file] Print size of every packet transferred on stdout or specified [channel]", "v",   "verbose"),
        o_quiet   ((optargs), " Set quiet mode", "q",   "quiet"),
        o_coord   ((optargs), OptionScheme::ARG_FIXED(2), "<x> <y> Coordinates", "c", "coord"),
        o_help    ((optargs), "[special=logging] This help", "?",   "help", "-help")
            ;

    OptionStatus ost = optargs.process(argv, argc);

    if (!ost)
    {
        ofprintl(cout, "ERROR: ", ost.error_code_str(), ": ", ost.error_option);
    }

    bool need_help = false;
    try
    {
        need_help = optargs.exists(o_help);

        vector<string> logfas = optargs.get(o_logfa);

        string helpspec = optargs[o_help];

        string verbspec = optargs.get(o_verbose);

        int timeout = optargs.get(o_timeout, -1);

        ofprintl(cout, "Need help: ", fmt_if(need_help, "yes", "no"), " spec: ", helpspec);
        ofprintl(cout, "Verbose: ", verbspec, " (", fmt_if(optargs.exists(o_verbose), "wanted", "unwanted"), ")");

        ofprint(cout, "Log FAs: ");
        if (logfas.empty())
            ofprint(cout, "(empty)");
        else
        {
            ofprint(cout, logfas[0]);
            for (size_t i = 1; i < logfas.size(); ++i)
                ofprint(cout, ", ", logfas[i]);
        }
        ofprintl(cout);

        ofprintl(cout, "Timeout: ", timeout);

        vector<int> coords = optargs.get(o_coord);
        ofprintl(cout, "Coordinates: (", coords[0], " ", coords[1], ")");

    }
    catch (std::exception& e)
    {
        ofprintl(cout, "EXCEPTION: ", e.what());
    }

    if (need_help)
    {
        ofprintl(cout, "WITH NEED HELP, here is the option help:");
        for (auto& option: optargs.options())
        {
            ofprintl(cout, option.helpitem());
        }
    }

    auto& unknown = optargs.unknown();
    if (!unknown.empty())
    {
        ofprint(cout, "Unrecognized options: ");
        for (auto& o: unknown)
            ofprint(cout, o, " ");
        ofprintl(cout);
    }

    return 0;
}

