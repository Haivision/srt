/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>
 */

#ifndef INC__URL_PARSER_H
#define INC__URL_PARSER_H

#include <string>
#include <map>
#include <cstdlib>


//++
// UriParser
//--

class UriParser
{
// Construction
public:

    enum DefaultExpect { EXPECT_FILE, EXPECT_HOST };

    UriParser(const std::string& strUrl, DefaultExpect exp = EXPECT_FILE);
    virtual ~UriParser(void);

    // Some predefined types
    enum Type
    {
        UNKNOWN, FILE, UDP, TCP, SRT, RTMP, HTTP
    };
    Type type();

// Operations
public:
    std::string uri() { return m_origUri; }
    std::string proto(void);
    std::string scheme() { return proto(); }
    std::string host(void);
    std::string port(void);
    unsigned short int portno();
    std::string hostport() { return host() + ":" + port(); }
    std::string path(void);
    std::string queryValue(const std::string& strKey);
    const std::map<std::string, std::string>& parameters() { return m_mapQuery; }

private:
    void Parse(const std::string& strUrl, DefaultExpect);

// Overridables
public:

// Overrides
public:

// Data
private:
    std::string m_origUri;
    std::string m_proto;
    std::string m_host;
    std::string m_port;
    std::string m_path;
    Type m_uriType;

    std::map<std::string, std::string> m_mapQuery;
};

//#define TEST1 1

#endif // _FMS_URL_PARSER_H_
