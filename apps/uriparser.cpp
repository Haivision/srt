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
#include <algorithm>
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
        types["udp"] = UriParser::UDP;
        types["tcp"] = UriParser::TCP;
        types["srt"] = UriParser::SRT;
        types["rtmp"] = UriParser::RTMP;
        types["http"] = UriParser::HTTP;
        types["rtp"] = UriParser::RTP;
        types[""] = UriParser::UNKNOWN;
    }
} g_uriparser_init;

UriParser::UriParser(const string& strUrl, DefaultExpect exp)
{
    m_expect = exp;
    Parse(strUrl, exp);
}

UriParser::~UriParser(void)
{
}

string UriParser::makeUri()
{
    // Reassemble parts into the URI
    string prefix = "";
    if (m_proto != "")
    {
        prefix = m_proto + "://";
    }

    std::ostringstream out;

    out << prefix << m_host;
    if ((m_port == "" || m_port == "0") && m_expect == EXPECT_FILE)
    {
        // Do not add port
    }
    else
    {
        out << ":" << m_port;
    }

    if (m_path != "")
    {
        if (m_path[0] != '/')
            out << "/";
        out << m_path;
    }

    if (!m_mapQuery.empty())
    {
        out << "?";

        query_it i = m_mapQuery.begin();
        for (;;)
        {
            out << i->first << "=" << i->second;
            ++i;
            if (i == m_mapQuery.end())
                break;
            out << "&";
        }
    }

    m_origUri = out.str();
    return m_origUri;
}

string UriParser::proto(void) const
{
    return m_proto;
}

UriParser::Type UriParser::type() const
{
    return m_uriType;
}

string UriParser::host(void) const
{
    return m_host;
}

string UriParser::port(void) const
{
    return m_port;
}

unsigned short int UriParser::portno(void) const
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

string UriParser::path(void) const
{
    return m_path;
}

string UriParser::queryValue(const string& strKey) const
{
    return m_mapQuery.at(strKey);
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
        transform(m_proto.begin(), m_proto.end(), m_proto.begin(), [](char c){ return tolower(c); });
        m_host  = m_host.substr(idx + 3, m_host.size() - (idx + 3));
    }

    // Handle the IPv6 specification in square brackets.
    // This actually handles anything specified in [] so potentially
    // you can also specify the usual hostname here as well. If the
    // whole host results to have [] at edge positions, they are stripped,
    // otherwise they remain. In both cases the search for the colon
    // separating the port specification starts only after ].
    const size_t i6pos = m_host.find("[");
    size_t i6end = string::npos;

    // Search for the "path" part only behind the closed bracket,
    // if both open and close brackets were found
    size_t path_since = 0;
    if (i6pos != string::npos)
    {
        i6end = m_host.find("]", i6pos);
        if (i6end != string::npos)
            path_since = i6end;
    }

    idx = m_host.find("/", path_since);
    if (idx != string::npos)
    {
        m_path = m_host.substr(idx, m_host.size() - idx);
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

    bool stripbrackets = false;
    size_t hostend = 0;
    if (i6pos != string::npos)
    {
        // IPv6 IP address. Find the terminating ]
        hostend = m_host.find("]", i6pos);
        idx = m_host.rfind(":");
        if (hostend != string::npos)
        {
            // Found the end. But not necessarily it was
            // at the beginning. If it was at the beginning,
            // strip them from the host name.

            size_t lasthost = idx;
            if (idx != string::npos && idx < hostend)
            {
                idx = string::npos;
                lasthost = m_host.size();
            }

            if (i6pos == 0 && hostend == lasthost - 1)
            {
                stripbrackets = true;
            }
        }
    }
    else
    {
        idx = m_host.rfind(":");
    }

    if (idx != string::npos)
    {
        m_port = m_host.substr(idx + 1, m_host.size() - (idx + 1));

        // Extract host WITHOUT stripping brackets
        m_host = m_host.substr(0, idx);
    }

    if (stripbrackets)
    {
        if (!hostend)
            hostend = m_host.size() - 1;
        m_host = m_host.substr(1, hostend - 1);
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

#include <vector>

using namespace std;

int main( int argc, char** argv )
{
    if ( argc < 2 ) 
    {
        return 0;
    }
    UriParser parser (argv[1], UriParser::EXPECT_HOST);
    std::vector<std::string> args;

    if (argc > 2)
    {
        copy(argv+2, argv+argc, back_inserter(args));
    }


    (void)argc;

    cout << "PARSING URL: " << argv[1] << endl;
    cout << "SCHEME INDEX: " << int(parser.type()) << endl;
    cout << "PROTOCOL: " << parser.proto() << endl;
    cout << "HOST: " << parser.host() << endl;
    cout << "PORT (string): " << parser.port() << endl;
    cout << "PORT (numeric): " << parser.portno() << endl;
    cout << "PATH: " << parser.path() << endl;
    cout << "PARAMETERS:\n";
    for (auto& p: parser.parameters()) 
    {
        cout << "\t" << p.first << " = " << p.second << endl;
    }

    if (!args.empty())
    {
        for (string& s: args)
        {
            vector<string> keyval;
            Split(s, '=', back_inserter(keyval));
            if (keyval.size() < 2)
                keyval.push_back("");
            parser[keyval[0]] = keyval[1];
        }

        cout << "REASSEMBLED: " << parser.makeUri() << endl;
    }
    return 0;
}

#endif
