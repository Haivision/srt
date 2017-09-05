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
    enum Type
    {
        UNKNOWN, FILE, UDP, TCP, SRT, RTMP, HTTP
    };

    UriParser(const std::string& strUrl, DefaultExpect exp = EXPECT_FILE);
    UriParser(): m_uriType(UNKNOWN) {}
    virtual ~UriParser(void);

    // Some predefined types
    Type type();

    struct ParamProxy
    {
        std::map<std::string, std::string>& mp;
        const std::string& key;

        ParamProxy(std::map<std::string, std::string>& m, const std::string& k): mp(m), key(k) {}

        void operator=(const std::string& val)
        {
            mp[key] = val;
        }


        std::map<std::string, std::string>::iterator find()
        {
            return mp.find(key);
        }

        operator std::string()
        {
            std::map<std::string, std::string>::iterator p = find();
            if (p == mp.end())
                return "";
            return p->second;
        }

        bool exists()
        {
            return find() != mp.end();
        }
    };

// Operations
public:
    std::string uri() { return m_origUri; }
    std::string proto();
    std::string scheme() { return proto(); }
    std::string host();
    std::string port();
    unsigned short int portno();
    std::string hostport() { return host() + ":" + port(); }
    std::string path();
    std::string queryValue(const std::string& strKey);
    ParamProxy operator[](const std::string& key) { return ParamProxy(m_mapQuery, key); }
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
