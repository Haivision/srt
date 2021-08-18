/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_COMMON_TRANMITBASE_HPP
#define INC_SRT_COMMON_TRANMITBASE_HPP

#include <atomic>
#include <string>
#include <memory>
#include <vector>
#include <iostream>
#include <stdexcept>

#include "uriparser.hpp"

typedef std::vector<char> bytevector;
extern std::atomic<bool> transmit_throw_on_interrupt;
extern int transmit_bw_report;
extern unsigned transmit_stats_report;
extern size_t transmit_chunk_size;
extern bool transmit_printformat_json;
extern bool transmit_use_sourcetime;
extern int transmit_retry_connect;
extern bool transmit_retry_always;

struct MediaPacket
{
    bytevector payload;
    int64_t time = 0;

    MediaPacket(bytevector&& src): payload(std::move(src)) {}
    MediaPacket(bytevector&& src, int64_t stime): payload(std::move(src)), time(stime) {}

    MediaPacket(const bytevector& src): payload(src) {}
    MediaPacket(const bytevector& src, int64_t stime): payload(src), time(stime) {}
    MediaPacket() {}
};


class Location
{
public:
    UriParser uri;
    Location() {}
    virtual bool IsOpen() = 0;
    virtual void Close() {}
};

class Source: public virtual Location
{
public:
    virtual MediaPacket Read(size_t chunk) = 0;
    virtual bool End() = 0;
    static std::unique_ptr<Source> Create(const std::string& url);
    virtual ~Source() {}

    class ReadEOF: public std::runtime_error
    {
    public:
        ReadEOF(const std::string& fn): std::runtime_error( "EOF while reading file: " + fn )
        {
        }
    };
};

class Target: public virtual Location
{
public:
    virtual void Write(const MediaPacket& portion) = 0;
    virtual bool Broken() = 0;
    virtual size_t Still() { return 0; }
    static std::unique_ptr<Target> Create(const std::string& url);
    virtual ~Target() {}
};


class Relay: public virtual Source, public virtual Target, public virtual Location
{
public:
    static std::unique_ptr<Relay> Create(const std::string& url);
    virtual ~Relay() {}
};


#endif
