#include "bstow-log.hpp"
#include "utilities.h"

namespace bstow
{
std::string ExtractFunctionName(const char* fnspec)
{
    using namespace std;
    using namespace srt;

    string id = fnspec;

    // First find the first space that is not within parents
    int ipos = 0;
    int depth = 0;
    for (size_t i = 0; i < id.size(); ++i)
    {
        // Fortunately we don't have initial spaces.
        // The first space means end of the type specification
        if (id[i] == '(')
        {
            ++depth;
        }
        else if (id[i] == ')')
            --depth;
        else if (id[i] == ' ' && depth == 0)
        {
            ipos = i;
            break;
        }
    }
    if (ipos == 0)
    {
        id = "UNKNOWN";
    }
    else
    {
        id = id.substr(ipos + 1);
        size_t op = id.find('(');
        if (op != string::npos)
            id = id.substr(0, op);

        if (id.find(':') != string::npos)
        {
            vector<string> parts;
            Split(id, ':', back_inserter(parts));
            if (parts.size() < 3)
                id = parts.back();
            else
            {
                size_t last = parts.size() - 1;
                size_t from = parts.size() - 3;
                id = parts[from] + "." + parts[last];
            }
        }
    }

    return id;
}

std::ostream* g_logstream = &std::cerr;
int g_loglevel = LL_ERROR;

}
