/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_URL_PARSER_H
#define INC_SRT_URL_PARSER_H

#include <string>
#include <map>
#include <cstdlib>
#include "utilities.h"


//++
// UriParser
//--

class UriParser
{
// Construction
public:

    enum DefaultExpect { EXPECT_FILE, EXPECT_HOST };
    enum Type
    {
        UNKNOWN, FILE, UDP, TCP, SRT, RTMP, HTTP, RTP
    };

    UriParser(const std::string& strUrl, DefaultExpect exp = EXPECT_FILE);
    UriParser(): m_uriType(UNKNOWN) {}
    virtual ~UriParser(void);

    // Some predefined types
    Type type() const;

    typedef MapProxy<std::string, std::string> ParamProxy;

// Operations
public:
    std::string uri() const { return m_origUri; }
    std::string proto() const;
    std::string scheme() const { return proto(); }
    std::string host() const;
    std::string port() const;
    unsigned short int portno() const;
    std::string hostport() const { return host() + ":" + port(); }
    std::string path() const;
    std::string queryValue(const std::string& strKey) const;
    std::string makeUri();
    ParamProxy operator[](const std::string& key) { return ParamProxy(m_mapQuery, key); }
    const std::map<std::string, std::string>& parameters() const { return m_mapQuery; }
    typedef std::map<std::string, std::string>::const_iterator query_it;

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
    DefaultExpect m_expect;

    std::map<std::string, std::string> m_mapQuery;
};

//#define TEST1 1

#endif // INC_SRT_URL_PARSER_H
