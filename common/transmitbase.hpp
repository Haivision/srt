#ifndef INC__COMMON_TRANMITBASE_HPP
#define INC__COMMON_TRANMITBASE_HPP

#include <string>
#include <memory>
#include <vector>
#include <iostream>

typedef std::vector<char> bytevector;
extern bool transmit_verbose;
extern volatile bool transmit_throw_on_interrupt;
extern int transmit_bw_report;
extern unsigned transmit_stats_report;
extern std::ostream* transmit_cverb;
extern size_t transmit_chunk_size;

static const struct VerboseLogNoEol { VerboseLogNoEol() {} } VerbNoEOL;

class VerboseLog
{
    bool noeol;
public:

    VerboseLog(): noeol(false) {}

    template <class V>
    VerboseLog& operator<<(const V& arg)
    {
        std::ostream& os = transmit_cverb ? *transmit_cverb : std::cout;
        if (transmit_verbose)
            os << arg;
        return *this;
    }

    VerboseLog& operator<<(VerboseLogNoEol)
    {
        noeol = true;
        return *this;
    }

    ~VerboseLog()
    {
        if (transmit_verbose && !noeol)
        {
            std::ostream& os = transmit_cverb ? *transmit_cverb : std::cout;
            os << std::endl;
        }
    }
};

inline VerboseLog Verb() { return VerboseLog(); }

class Location
{
public:
    UriParser uri;
    Location() {}
};

class Source: public Location
{
public:
    virtual bytevector Read(size_t chunk) = 0;
    virtual bool IsOpen() = 0;
    virtual bool End() = 0;
    static std::unique_ptr<Source> Create(const std::string& url);
    virtual void Close() {}
    virtual ~Source() {}

    class ReadEOF: public std::runtime_error
    {
    public:
        ReadEOF(const std::string& fn): std::runtime_error( "EOF while reading file: " + fn )
        {
        }
    };
};

class Target: public Location
{
public:
    virtual void Write(const bytevector& portion) = 0;
    virtual bool IsOpen() = 0;
    virtual bool Broken() = 0;
    virtual void Close() {}
    virtual size_t Still() { return 0; }
    static std::unique_ptr<Target> Create(const std::string& url);
    virtual ~Target() {}
};



#endif
