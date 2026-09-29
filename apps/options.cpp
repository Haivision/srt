#include "options.hpp"
#include "../logging/ofmt.h"

namespace hvu
{

const OptionScheme::Args OptionScheme::ARG_NONE = Args { 0 };
const OptionScheme::Args OptionScheme::ARG_ONE = Args { 1 };
const OptionScheme::Args OptionScheme::ARG_VAR = Args { -1 };

OptionScheme::Args OptionName::DetermineTypeFromHelpText(const std::string& helptext)
{
    if (helptext.empty())
        return OptionScheme::ARG_NONE;

    if (helptext[0] == '<')
    {
        // If the argument is <one-argument>, then it's ARG_NONE.
        // If it's <multiple-arguments...>, then it's ARG_VAR.
        // When closing angle bracket isn't found, fallback to ARG_ONE.
        size_t pos = helptext.find('>');
        if (pos == std::string::npos)
            return OptionScheme::ARG_ONE; // mistake, but acceptable

        if (pos >= 4 && helptext.substr(pos-3, 4) == "...>")
            return OptionScheme::ARG_VAR;

        // We have < and > without ..., simply one argument
        return OptionScheme::ARG_ONE;
    }

    if (helptext[0] == '[')
    {
        // Argument in [] means it is optional; in this case
        // you should state that the argument can be given or not.
        return OptionScheme::ARG_VAR;
    }

    // Also as fallback
    return OptionScheme::ARG_NONE;
}

#ifdef HVU_OPTIONS_ENABLE_DEBUG
#define ODEBUG(...) ofprintl(std::cerr, __VA_ARGS___)
std::string g_debug_arg [3] = { "NONE", "ONE", "VAR" };
#else
#define ODEBUG(...)
#endif

// XXX Required:
//
// Remove this function as API; required will be processing
// using fields of ProcessHandler
//
// Add a feature: bool grouped_single. If true, then:
// - if the scheme isn't empty
// - if the option name wasn't found in the scheme
// - if the option doesn't start with dash
// : Split the name into single characters and review
//   them all as if they were multiple options; also
//   if any of them grabs arguments, expect arguments
//   for it in next words

OptionStatus ProcessOptions(const char* const* argv, int argc,
//        map<string, vector<string>>& w_params,
        options_t& w_params,
        const std::vector<OptionScheme>& scheme,
        OptionScheme::Args default_scheme,
        std::string separators,
        std::set<std::string>* pw_unknown)
{
    using namespace std;

    string current_key;
    vector<string> extra_args;
    size_t vals = 0;
    OptionScheme::Args type = OptionScheme::ARG_VAR; // This is for no-option-yet or consumed
    bool moreoptions = true;
    map<string, const OptionScheme*> found_schemes;

    for (const char* const* p = argv+1; p != argv+argc; ++p)
    {
        const char* a = *p;
        ODEBUG("*D ARG: '", a, "'");
        bool isoption = false;
        if (a[0] == '-')
        {
            isoption = true;
            // If a[0] isn't NUL - because it is dash - then
            // we can safely check a[1].
            // An expression starting with a dash is not
            // an option marker if it is a single dash or
            // a negative number.
            if (!a[1] || isdigit(a[1]))
            {
                ODEBUG("*D NOT OPTION");
                isoption = false;
            }
        }

        if (moreoptions && isoption)
        {
            bool arg_specified = false;
            size_t seppos = std::string::npos; // (see goto, it would jump over initialization)
            char argsep = ':'; // needed when separator detected
            current_key = a+1;
            if (current_key == "-")
            {
                // The -- argument terminates the options.
                // The default key is restored to empty so that
                // it collects now all arguments under the empty key
                // (not-option-assigned argument).
                moreoptions = false;
                goto EndOfArgs;
            }

            if (current_key == "/")
            {
                // Argument termination tag: -/
                // This can be used for terminating the argument list
                // for a scheme type ARG_VAR; turns the current key
                // back to empty so that next arguments belong to free ones.

                // XXX Consider handling cases like -a list of args -/a others
                // in order to specify the option name that was last open;
                // it requires some more robust error handling, however.
                goto EndOfArgs;
            }

            // Alternative ways to specify arguments for options basing
            // on a special character found in the alleged option-spec
            for (char sep: separators)
            {
                seppos = current_key.find(sep);
                if (seppos != string::npos)
                {
                    argsep = sep;
                    break;
                }
            }

            if (seppos != string::npos)
            {
                // Old option specification.
                string extra_arg = current_key.substr(seppos + 1);
                current_key = current_key.substr(0, 0 + seppos);
                arg_specified = true; // Prevent eating args from option list
                ODEBUG("*D Consumed arg '", extra_arg, "' for option -", current_key);

                for (;;)
                {
                    // At start, assume that the argument starts
                    // where extra_arg begins, but it may contain
                    // further positions
                    seppos = extra_arg.find(argsep);
                    extra_args.push_back(extra_arg.substr(0, seppos));
                    if (seppos == string::npos)
                        break;
                    extra_arg = extra_arg.substr(seppos+1);
                }

                // But then, continue the same argument search for the next ones.
            }

            w_params[current_key].clear();
            vals = 0;

            if (!extra_args.empty())
            {
                auto& target = w_params[current_key];
                vals += extra_args.size();
                target.insert(target.end(),
                        make_move_iterator(extra_args.begin()),
                        make_move_iterator(extra_args.end()));

                extra_args.clear();
            }

            if (!scheme.empty())
            {
                // Find the key in the scheme. If not found, treat it as ARG_NONE.
                for (const auto& s: scheme)
                {
                    if (s.names().count(current_key))
                    {
                        found_schemes[current_key] = &s;
                        ODEBUG("*D found '", current_key, "' in scheme type=", int(s.type));
                        // If argument was specified using the old way, like
                        // -v:0 or "-v 0", then consider the argument specified and
                        // treat further arguments as either no-option arguments or
                        // new options.
                        if (s.type == OptionScheme::ARG_NONE || arg_specified)
                        {
                            // Anyway, consider it already processed.
                            goto EndOfArgs;
                        }
                        type = s.type;

                        if (vals == type.maxargs())
                        //if (vals == 1 && type == OptionScheme::ARG_ONE)
                        {
                            // Argument for one-arg option already consumed,
                            // so set to free args.
                            goto EndOfArgs;
                        }
                        goto Found;
                    }

                }
                ODEBUG("*D NOTIFYING UNKNOWN KEY: -", current_key);
                // Insert it if unknown bypass is provided and there
                // are any options defined. In case of "free specification"
                // this isn't done because this time "any option is unknown"
                if (pw_unknown)
                    pw_unknown->insert(current_key);
            }
            // Not found: set ARG_NONE.
            ODEBUG("*D KEY '", current_key, "' assumed type ", g_debug_arg[default_scheme]);
            if (default_scheme != OptionScheme::ARG_NONE)
            {
                type = default_scheme;
                continue;
            }
EndOfArgs:
            // Setting empty-string key assumes that the further
            // non-option arguments belong to "free arguments", and
            // its scheme is "var".
            type = OptionScheme::ARG_VAR;
            current_key = "";
Found:
            continue;
        }

        // Collected a value - check if full
        ODEBUG("*D COLLECTING '", a, "' for key '", current_key, "' (", vals, " so far)");
        w_params[current_key].push_back(a);
        ++vals;
        if (vals == type.maxargs())
        //if ( vals == 1 && type == OptionScheme::ARG_ONE )
        {
            ODEBUG("*D KEY TYPE ONE - resetting to empty key");
            // Reset the key to "default one".
            current_key = "";
            vals = 0;
            type = OptionScheme::ARG_VAR;
        }
        else
        {
            ODEBUG("*D KEY type VAR - still collecting until the end of options or next option.");
        }
    }

    // Post-verification
    if (!scheme.empty())
    {
        // Find the key in the scheme. If not found, treat it as ARG_NONE.
        for (const auto& a: w_params)
        {
            if (a.first == "") // discard free arguments
                continue;

            auto it = found_schemes.find(a.first);
            if (it == found_schemes.end())
            {
                // Report error: option not found in the scheme
                return OptionStatus::error(a.first, OptionStatus::ERR_UNDEF);
            }

            const OptionScheme& s = *it->second;
            if (s.type.n == 0)
            {
                if (!a.second.empty())
                    return OptionStatus::error(a.first, OptionStatus::ERR_EXCEED);
            }
            else if (s.type.n > 0)
            {
                // The number of arguments must be equal
                int dif = s.type.n - int(a.second.size());
                if (dif > 0)
                    return OptionStatus::error(a.first, OptionStatus::ERR_MISSING);
                else if (dif < 0)
                    return OptionStatus::error(a.first, OptionStatus::ERR_EXCEED);
            }
            else // < 0
            {
                // This means optional so only check if exceeded the maximum
                size_t nexpect = -s.type.n - 1;
                if (a.second.size() > nexpect)
                    return OptionStatus::error(a.first, OptionStatus::ERR_EXCEED);
            }
        }
    }

    return OptionStatus::success();
}

std::string OptionHelpItem(const OptionName& o, const std::string& px, size_t width, char postfill)
{
    using namespace std;

    ofmt_bufs ospec;

    ospec << px << "-" << o.main_name;
    string hlp = o.helptext;
    string prefix;

    if (hlp == "")
    {
        hlp = " (Undocumented)";
    }
    else if (hlp[0] != ' ')
    {
        size_t end = string::npos;
        if (hlp[0] == '<')
        {
            end = hlp.find('>');
        }
        else if (hlp[0] == '[')
        {
            end = hlp.find(']');
        }

        if (end != string::npos)
        {
            ++end;
        }
        else
        {
            end = hlp.find(' ');
        }

        if (end != string::npos)
        {
            prefix = hlp.substr(0, end);
            //while (hlp[end] == ' ')
            //    ++end;
            hlp = hlp.substr(end);
            ospec <<  " " << prefix;
        }
    }

    return ofcat( fmt(ospec.str(), fmtc().left().fill(postfill).width(width)), "-", hlp);
}

const char* OptionStatus::error_code_str() const
{
    static const char* const s_codes [] = {
        "success",
        "missing arguments",
        "too many arguments",
        "unknown option"
    };

    if (int(error_code) < 0 || int(error_code) > ERR_UNDEF)
        return "UNKNOWN ERROR";
    return s_codes[error_code];
}

}
