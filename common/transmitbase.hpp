#ifndef INC__COMMON_TRANMITBASE_HPP
#define INC__COMMON_TRANMITBASE_HPP

#include <string>
#include <memory>
#include <vector>

typedef std::vector<char> bytevector;
extern bool transmit_verbose;
extern volatile bool transmit_throw_on_interrupt;
extern int transmit_bw_report;
extern unsigned transmit_stats_report;
extern std::ostream* transmit_cverb;

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
    static std::unique_ptr<Target> Create(const std::string& url);
    virtual ~Target() {}
};



#endif
