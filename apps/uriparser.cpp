/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


// STL includes
#include <map>
#include <string>

#include "uriparser.hpp"

#ifdef TEST
#define TEST1 1
#endif

#ifdef TEST1
#include <iostream>
#endif


using namespace std;


map<string, UriParser::Type> types;

struct UriParserInit
{
    UriParserInit()
    {
        types["file"] = UriParser::FILE;
        types["srt"] = UriParser::SRT;
        types["udp"] = UriParser::UDP;
        types[""] = UriParser::UNKNOWN;
    }
} g_uriparser_init;


UriParser::UriParser(const string& strUrl, DefaultExpect exp)
{
    Parse(strUrl, exp);
}

UriParser::~UriParser(void)
{
}

string UriParser::proto(void)
{
    return m_proto;
}

UriParser::Type UriParser::type()
{
    return m_uriType;
}

string UriParser::host(void)
{
    return m_host;
}

string UriParser::port(void)
{
    return m_port;
}

unsigned short int UriParser::portno(void)
{
    // This returns port in numeric version. Fallback to 0.
    try
    {
        int i = atoi(m_port.c_str());
        if ( i <= 0 || i > 65535 )
            return 0;
        return i;
    }
    catch (...)
    {
        return 0;
    }
}

string UriParser::path(void)
{
    return m_path;
}

string UriParser::queryValue(const string& strKey)
{
    return m_mapQuery[strKey];
}

void UriParser::Parse(const string& strUrl, DefaultExpect exp)
{
    int iQueryStart = -1;

    size_t idx = strUrl.find("?");
    if (idx != string::npos)
    {
        m_host   = strUrl.substr(0, idx);
        iQueryStart = idx + 1;
    }
    else
    {
        m_host = strUrl;
    }

    idx = m_host.find("://");
    if (idx != string::npos)
    {
        m_proto = m_host.substr(0, idx);
        m_host  = m_host.substr(idx + 3, m_host.size() - (idx + 3));
    }

    idx = m_host.find("/");
    if (idx != string::npos)
    {
        m_path  = m_host.substr(idx, m_host.size() - idx);
        m_host = m_host.substr(0, idx);
    }


    // Check special things in the HOST entry.
    size_t atp = m_host.find('@');
    if ( atp != string::npos )
    {
        string realhost = m_host.substr(atp+1);
        string prehost;
        if ( atp > 0 )
        {
            prehost = m_host.substr(0, atp-0);
            size_t colon = prehost.find(':');
            if ( colon != string::npos )
            {
                string pw = prehost.substr(colon+1);
                string user;
                if ( colon > 0 )
                    user = prehost.substr(0, colon-0);
                m_mapQuery["user"] = user;
                m_mapQuery["password"] = pw;
            }
            else
            {
                m_mapQuery["user"] = prehost;
            }
        }
        else
        {
            m_mapQuery["multicast"] = "1";
        }
        m_host = realhost;
    }

    idx = m_host.find(":");
    if (idx != string::npos)
    {
        m_port = m_host.substr(idx + 1, m_host.size() - (idx + 1));
        m_host = m_host.substr(0, idx);
    }

    if ( m_port == "" && m_host != "" )
    {
        // Check if the host-but-no-port has specified
        // a single integer number. If so
        // We need to use C86 strtol, cannot use C++11
        const char* beg = m_host.c_str();
        const char* end = m_host.c_str() + m_host.size();
        char* eos = 0;
        long val = strtol(beg, &eos, 10);
        if ( val > 0 && eos == end )
        {
            m_port = m_host;
            m_host = "";
        }
    }

    string strQueryPair;
    while (iQueryStart > -1)
    {
        idx = strUrl.find("&", iQueryStart);
        if (idx != string::npos)
        {
            strQueryPair = strUrl.substr(iQueryStart, idx - iQueryStart);
            iQueryStart = idx + 1;
        }
        else
        {
            strQueryPair = strUrl.substr(iQueryStart, strUrl.size() - iQueryStart);
            iQueryStart = idx;
        }

        idx = strQueryPair.find("=");
        if (idx != string::npos)
        {
            m_mapQuery[strQueryPair.substr(0, idx)] = strQueryPair.substr(idx + 1, strQueryPair.size() - (idx + 1));
        }
    }

    if ( m_proto == "file" )
    {
        if ( m_path.size() > 3 && m_path.substr(0, 3) == "/./" )
            m_path = m_path.substr(3);
    }

    // Post-parse fixes
    // Treat empty protocol as a file. In this case, merge the host and path.
    if ( exp == EXPECT_FILE && m_proto == "" && m_port == "" )
    {
        m_proto = "file";
        m_path = m_host + m_path;
        m_host = "";
    }

    m_uriType = types[m_proto]; // default-constructed UNKNOWN will be used if not found (although also inserted)
    m_origUri = strUrl;
}

#ifdef TEST


using namespace std;

int main( int argc, char** argv )
{
    UriParser parser (argv[1]);
    (void)argc;

    cout << "PARSING URL: " << argv[1] << endl;
    cerr << "SCHEME INDEX: " << int(parser.type()) << endl;
    cout << "PROTOCOL: " << parser.proto() << endl;
    cout << "HOST: " << parser.host() << endl;
    cout << "PORT: " << parser.portno() << endl;
    cout << "PATH: " << parser.path() << endl;
    cout << "PARAMETERS:\n";
    for (auto p: parser.parameters())
        cout << "\t" << p.first << " = " << p.second << endl;


    return 0;
}
#endif
