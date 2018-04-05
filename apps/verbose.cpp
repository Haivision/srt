#include "verbose.hpp"

namespace Verbose
{
    bool on = false;
    std::ostream* cverb = &std::cerr;

    Log& Log::operator<<(LogNoEol)
    {
        noeol = true;
        return *this;
    }

    Log::~Log()
    {
        if (on && !noeol)
        {
            (*cverb) << std::endl;
        }
    }
}
