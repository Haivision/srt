/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC__COMMON_TRANMITBASE_HPP
#define INC__COMMON_TRANMITBASE_HPP

#include <string>
#include <memory>
#include <vector>
#include <iostream>
#include <stdexcept>

typedef std::vector<char> bytevector;
extern bool transmit_total_stats;
extern volatile bool transmit_throw_on_interrupt;
extern unsigned long transmit_bw_report;
extern unsigned long transmit_stats_report;
extern unsigned long transmit_chunk_size;
extern bool printformat_json;

class Location
{
public:
    UriParser uri;
    Location() {}
};

class Source: public Location
{
public:
    virtual bool Read(size_t chunk, bytevector& data) = 0;
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

    virtual SRTSOCKET GetSRTSocket() { return SRT_INVALID_SOCK; };
    virtual int GetSysSocket() { return -1; };
    virtual bool AcceptNewClient() { return false; }
};

class Target: public Location
{
public:
    virtual bool Write(const bytevector& portion) = 0;
    virtual bool IsOpen() = 0;
    virtual bool Broken() = 0;
    virtual void Close() {}
    virtual size_t Still() { return 0; }
    static std::unique_ptr<Target> Create(const std::string& url);
    virtual ~Target() {}

    virtual SRTSOCKET GetSRTSocket() { return SRT_INVALID_SOCK; }
    virtual int GetSysSocket() { return -1; }
    virtual bool AcceptNewClient() { return false; }
};



#endif
