
#ifndef INC_HVU_OPTIONS_H
#define INC_HVU_OPTIONS_H

#include <vector>
#include <set>
#include <map>
#include <string>
#include <stdexcept>

namespace hvu
{

// XXX Utility; move somewhere else


struct NumberError: public std::invalid_argument
{
    std::string msg;

    NumberError(const std::string& optname): std::invalid_argument("stoi")
    {
        msg = "Number conversion error for -" + optname;
    }

    const char* what() const noexcept override
    {
        return msg.c_str();
    }
};

inline bool CheckTrue(const std::vector<std::string>& in, bool ifempty = true)
{
    if (in.empty())
        return ifempty;

    const std::set<std::string> false_vals = { "0", "no", "off", "false" };
    if (false_vals.count(in[0]))
        return false;

    return true;
}

template<class Type, class Input>
struct NumberConvertFwd
{
    static Type convert(const Input& )
    {
        typename Type::incorrect_version wrong = Type::incorrect_version;
        return Type();
    }
};

template<class Number, class Input>
static inline Number StrToNumber(const Input& s)
{
    return NumberConvertFwd<Number, Input>::convert(s);
}

template<class Number>
struct NumberConvertFwd<Number, std::vector<std::string>>
{
    static Number convert(const std::vector<std::string>& sv)
    {
        std::string single;
        if (sv.empty())
            return StrToNumber<Number>(std::string("0"));
        return StrToNumber<Number>(sv.back());
    }
};

#define STON(type, function) \
template<> inline type StrToNumber(const std::string& s) { return function (s, 0, 0); }

STON(int, stoi);
STON(unsigned long, stoul);
STON(unsigned int, stoul);
STON(long long, stoll);
STON(unsigned long long, stoull);

#undef STON

template<class Number>
struct NumberConvertFwd<std::vector<Number>, std::vector<std::string>>
{
    static std::vector<Number> convert(const std::vector<std::string>& sv)
    {
        std::vector<Number> out;
        for (auto& s: sv)
            out.push_back(StrToNumber<Number>(s));
        return out;
    }
};

typedef std::map<std::string, std::vector<std::string>> options_t;

struct OutList
{
    typedef std::vector<std::string> type;
    static type process(const options_t::mapped_type& i) { return i; }
};

struct OutString
{
    typedef std::string type;
    static type process(const options_t::mapped_type& i)
    {
        if (i.empty())
            return type();

        size_t total = 0;
        for (auto& s: i)
            total += s.size();
        if (total == 0)
            return type();

        type out;
        out.reserve(total + i.size() + 2);
        out = i[0];
        for (size_t y = 1; y < i.size(); ++y)
        {
            out += " ";
            out += i[y];
        }

        return out;
    }
};

struct NumberAutoConvert
{
    options_t::mapped_type value;

    NumberAutoConvert(const options_t::mapped_type& args): value(args)
    {
    }

    // XXX Required for old API with Option<Out*> calls.
    // If so, the value type can be changed to const reference.
    NumberAutoConvert() {}
    NumberAutoConvert(const char* spec)
    {
        value.push_back(spec);
    }

    template<class Number>
    operator Number()
    {
        return StrToNumber<Number>(value);
    }
};

struct OutNumber
{
    typedef NumberAutoConvert type;
    static type process(const options_t::mapped_type& i)
    {
        return type(i);
    }
};

template <class Number>
struct OutNumberAs
{
    typedef Number type;
    static type process(const options_t::mapped_type& i)
    {
        return OutNumber::process(i);
    }
};


struct OutBool
{
    typedef bool type;
    static type process(const options_t::mapped_type& i) { return CheckTrue(i); }
};

struct OptionName;

struct OptionArgSpec
{
    int n;

    bool operator==(const OptionArgSpec& o) const { return n == o.n; }
    bool operator!=(const OptionArgSpec& o) const { return n != o.n; }

    size_t maxargs() const
    {
        if (n < 0)
            return -n - 1;
        return n;
    }
};

template<int N>
struct checkPositiveInteger
{
    static const bool valid = N >= 0;
};

template<bool pos>
struct checkPositiveIntegerValid
{
};

template<>
struct checkPositiveIntegerValid<true>
{
    static const bool valid = true;
};

struct OptionScheme
{
    const OptionName* pid;

    typedef OptionArgSpec Args;

    Args type;

    static bool checkPositive(int n)
    {
        if (n < 1)
            throw std::invalid_argument("Value expected >= 1");
        return true;
    }

    // Can't use constexpr - in C++11 it's still trying to link externally.
    static const Args ARG_NONE;
    static const Args ARG_ONE;
    static const Args ARG_VAR;
    static Args ARG_FIXED(int n) { return checkPositive(n), Args { n }; }
    static Args ARG_OPT(int n) { return checkPositive(n), Args { -n-1 }; }

    OptionScheme(const OptionScheme&) = default;
    OptionScheme(OptionScheme&& src)
        : pid(src.pid)
        , type(src.type)
    {
    }

    OptionScheme(const OptionName& id, Args tp);

    const std::set<std::string>& names() const;
    const std::string& helptext() const;
    const std::string& name() const;
    std::string helpitem() const;
};

class OptionHandler;

struct OptionName
{
    std::string helptext;
    std::string main_name;
    std::set<std::string> names;

    // XXX Check it this constructor is still usable.
    // Likely so, as long as you define the OptionScheme table separately.
    template <class... Args>
    OptionName(std::string ht, std::string first, Args... rest)
        : helptext(ht), main_name(first),
          names {first, rest...}
    {
    }
    OptionName(std::initializer_list<std::string> args): main_name(*args.begin()), names(args) {}

    template <class... Args>
    OptionName(std::vector<OptionScheme>& sc, OptionScheme::Args type,
            std::string ht, std::string first, Args... rest)
        : helptext(ht), main_name(first),
          names {first, rest...}
    {
        sc.push_back(OptionScheme(*this, type));
    }

    template <class... Args>
    OptionName(std::vector<OptionScheme>& sc,
            std::string ht, std::string first, Args... rest)
        : helptext(ht), main_name(first),
          names {first, rest...}
    {
        OptionScheme::Args type = DetermineTypeFromHelpText(ht);
        sc.push_back(OptionScheme(*this, type));
    }

    template <class... Args>
    OptionName(OptionHandler& sc, OptionScheme::Args type,
            std::string ht, std::string first, Args... rest);

    template <class... Args>
    OptionName(OptionHandler& sc,
            std::string ht, std::string first, Args... rest);

    operator std::set<std::string>() { return names; }
    operator const std::set<std::string>() const { return names; }

private:
    static OptionScheme::Args DetermineTypeFromHelpText(const std::string& helptext);
};

std::string OptionHelpItem(const OptionName& o,
        const std::string& px = "\t",
        size_t width = 24,
        char postfill = ' ');

inline OptionScheme::OptionScheme(const OptionName& id, Args tp): pid(&id), type(tp) {}
inline const std::set<std::string>& OptionScheme::names() const { return pid->names; }
inline const std::string& OptionScheme::helptext() const { return pid->helptext; }
inline const std::string& OptionScheme::name() const { return pid->main_name; }
inline std::string OptionScheme::helpitem() const { return OptionHelpItem(*pid); }

#ifndef HVU_OPTIONS_USE_OLD_API

template <class OutType, class OutValue> inline
typename OutType::type Option(const options_t&, OutValue deflt=OutValue()) { return deflt; }

template <class OutType, class OutValue, class... Args> inline
typename OutType::type Option(const options_t& options, OutValue deflt, std::string key, Args... further_keys)
{
    auto i = options.find(key);
    if ( i == options.end() )
        return Option<OutType>(options, deflt, further_keys...);
    return OutType::process(i->second);
}

template<typename TrapType>
struct OptionTrapType
{
    typedef TrapType type_t;
    static TrapType pass(TrapType v) { return v; }
};

template<>
struct OptionTrapType<const char*>
{
    typedef std::string type_t;
    static std::string pass(const char* v) { return v; }
};

template <class OutType, class OutValue> inline
typename OutType::type Option(const options_t& options, OutValue deflt, const OptionName& oname)
{
    (void)OptionTrapType<OutValue>::pass(deflt);
    for (auto key: oname.names)
    {
        auto i = options.find(key);
        if ( i != options.end() )
        {
            return OutType::process(i->second);
        }
    }
    return deflt;
}

template <class OutType> inline
typename OutType::type Option(const options_t& options, const OptionName& oname)
{
    typedef typename OutType::type out_t;
    for (auto key: oname.names)
    {
        auto i = options.find(key);
        if ( i != options.end() )
        {
            return OutType::process(i->second);
        }
    }
    return out_t();
}

inline bool OptionPresent(const options_t& options, const std::set<std::string>& keys)
{
    for (auto key: keys)
    {
        auto i = options.find(key);
        if ( i != options.end() )
            return true;
    }
    return false;
}

#endif

struct OptionStatus
{
    bool status;
    std::string error_option;

    enum Error
    {
        ERR_NONE = 0,
        ERR_MISSING = 1,
        ERR_EXCEED = 2,
        ERR_UNDEF = 3
    } error_code;

    OptionStatus(bool st, const std::string& s, Error e):
        status(st), error_option(s), error_code(e)
    {
    }

    operator bool() const { return status; }

    static OptionStatus success() { return { true, "", ERR_NONE }; }
    static OptionStatus error(const std::string& optname, Error code)
    {
        return { false, optname, code };
    }

    const char* error_code_str() const;

private:
    OptionStatus() {}
};

OptionStatus ProcessOptions(const char* const* argv, int argc, options_t& out,
        const std::vector<OptionScheme>& scheme,
        OptionScheme::Args default_scheme = OptionScheme::ARG_NONE,
        std::string separators = std::string(),
        std::set<std::string>* punknown = nullptr);

// OLD API
inline options_t ProcessOptions(const char* const* argv, int argc,
        const std::vector<OptionScheme>& scheme,
        OptionScheme::Args default_scheme = OptionScheme::ARG_NONE)
{
    options_t params;
    ProcessOptions(argv, argc, (params), scheme, default_scheme);
    return params;
}

struct OptionProxy
{
    typedef options_t::mapped_type value_t;
    std::string option;
    value_t value;
    bool found;

    // Universal is to treat it as number
    template<class TargetType> TargetType as() const;

    template<class TargetType>
    operator TargetType ()
    {
        if (!found)
            return TargetType();

        try
        {
            return as<TargetType>();
        }
        catch (std::invalid_argument& cve)
        {
            throw NumberError(option);
        }
    }

    template<class TargetType> TargetType as_default() const
    {
        return operator TargetType();
    }
};

template<class TargetType> inline
TargetType OptionProxy::as() const
{
    return OutNumberAs<TargetType>::process(value);
}


// Specializations are for bool, string, vector<string>
template<> inline
bool OptionProxy::as() const
{
    return OutBool::process(value);
}

template<> inline
std::string OptionProxy::as() const
{
    return OutString::process(value);
}

template<> inline
std::vector<std::string> OptionProxy::as() const
{
    return OutList::process(value);
}

template<class OutValue>
struct OptionProxyDef
{
    using value_t = OptionProxy::value_t;
    OptionProxy derived;
    OutValue deflt;

    OptionProxyDef(const std::string& name, const value_t& val, bool isexplicit /*= true*/): derived {name, val, isexplicit} {}
    OptionProxyDef(const std::string& name, const OutValue& val, bool isexplicit /*= false*/): derived {name, value_t(), isexplicit}, deflt(val) {}

    template<class TargetType>
    operator TargetType ()
    {
        if (!derived.found)
            return TargetType(deflt);

        return derived.as<TargetType>();
    }
};

template<>
struct OptionProxyDef<OptionProxy::value_t>
{
    using value_t = OptionProxy::value_t;
    OptionProxy derived;
    value_t deflt;

    OptionProxyDef(const std::string& name, const value_t& val, bool isexplicit): derived {name, isexplicit ? val : value_t(), isexplicit}, deflt(isexplicit ? value_t() : val) {}

    template<class TargetType>
    operator TargetType ()
    {
        if (!derived.found)
            return TargetType(deflt);

        return derived.as<TargetType>();
    }
};


class OptionHandler
{
    typedef options_t::mapped_type value_t;
    typedef options_t params_t;
    std::vector<OptionScheme> m_optargs;
    options_t m_params;
    std::set<std::string> m_unknown;
    OptionScheme::Args m_default_arg = OptionScheme::ARG_NONE;
    std::string m_sepchars = ": ,";

public:

    OptionHandler() {}

    void advice(OptionName& tag, const OptionScheme::Args& type)
    {
        m_optargs.push_back(OptionScheme(tag, type));
    }

    // PROPERTY:RW
    OptionScheme::Args default_arg() const { return m_default_arg; }
    void default_arg(OptionScheme::Args a) { m_default_arg = a; }

    // PROPERTY:RO
    const std::set<std::string>& unknown() const { return m_unknown; }

    // PROPERTY:WO
    void separators(const std::string& seps) { m_sepchars = seps; }

    // PROPERTY:RO
    const params_t& params() const { return m_params; }

    OptionStatus process(const char* const* argv, int argc)
    {
        return ProcessOptions(argv, argc, (m_params), m_optargs, m_default_arg, m_sepchars, &m_unknown);
    }

    /* XXX ProcessOptions not implemented to handle this;
       may be added in the future.
    void process(const std::vector<std::string>& args)
    {
        m_params = ProcessOptions(args);
    }
    */

    typedef OptionProxy Proxy;

    template<class OutValue> using ProxyDef = OptionProxyDef<OutValue>;

    bool exists(const OptionName& oname) const
    {
        return OptionPresent(m_params, oname);
    }

    template<class OutValue>
    ProxyDef<typename OptionTrapType<OutValue>::type_t> get(const OptionName& oname, OutValue deflt) const
    {
        (void)OptionTrapType<OutValue>::pass(deflt);
        typedef typename OptionTrapType<OutValue>::type_t RealType;
        for (auto key: oname.names)
        {
            auto i = m_params.find(key);
            if ( i != m_params.end() )
            {
                return ProxyDef<RealType> (key, i->second, true);
            }
        }
        return ProxyDef<RealType> (oname.main_name, RealType(deflt), false);
    }

    template <class OutValue>
    ProxyDef<typename OptionTrapType<OutValue>::type_t> getfree(OutValue deflt = OutValue())
    {
        typedef typename OptionTrapType<OutValue>::type_t RealType;
        return ProxyDef<RealType> ("", RealType(deflt), false);
    }

    template <class OutValue, class... Args>
    ProxyDef<typename OptionTrapType<OutValue>::type_t> getfree(OutValue deflt, const std::string& key, const Args&... further_keys)
    {
        typedef typename OptionTrapType<OutValue>::type_t RealType;
        auto i = m_params.find(key);
        if ( i == m_params.end() )
            return getfree(deflt, further_keys...);
        return ProxyDef<RealType> (i->first, i->second, true);
    }

    template <class OutValue, class... Args>
    ProxyDef<typename OptionTrapType<OutValue>::type_t> operator()(OutValue deflt, const std::string& key, const Args&... further_keys)
    {
        return getfree(deflt, key, further_keys...);
    }

    Proxy getfree1(const std::string& key) const
    {
        auto i = m_params.find(key);
        if ( i == m_params.end() )
            return Proxy { key, value_t(), false };
        return Proxy {key, i->second, true};
    }

    Proxy operator[](const std::string& name) const
    {
        return getfree1(name);
    }


    Proxy get(const OptionName& oname) const
    {
        for (auto key: oname.names)
        {
            auto i = m_params.find(key);
            if ( i != m_params.end() )
            {
                return Proxy {key, i->second, true};
            }
        }
        return Proxy {oname.main_name, value_t(), false };
    }

    Proxy operator[](const OptionName& oname) const
    {
        return get(oname);
    }

    const std::vector<OptionScheme>& options() const
    {
        return m_optargs;
    }
};

template <class... Args> inline
OptionName::OptionName(OptionHandler& sc, OptionScheme::Args type,
        std::string ht, std::string first, Args... rest):
    helptext(ht),
    main_name(first),
    names {first, rest...}
{
    sc.advice(*this, type);
}

    template <class... Args> inline
OptionName::OptionName(OptionHandler& sc,
        std::string ht, std::string first, Args... rest):
    helptext(ht),
    main_name(first),
    names {first, rest...}
{
    OptionScheme::Args type = DetermineTypeFromHelpText(ht);
    sc.advice(*this, type);
}




}


#endif
