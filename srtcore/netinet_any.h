/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC__NETINET_ANY_H
#define INC__NETINET_ANY_H

#include <cstring>
#include "platform_sys.h"

// This structure should replace every use of sockaddr and its currently
// used specializations, sockaddr_in and sockaddr_in6. This is to simplify
// the use of the original BSD API that relies on type-violating type casts.
// You can use the instances of sockaddr_any in every place where sockaddr is
// required.

struct sockaddr_any
{
    union
    {
        sockaddr_in sin;
        sockaddr_in6 sin6;
        sockaddr sa;
    };
    socklen_t len;

    sockaddr_any(int domain = AF_INET)
    {
        memset(this, 0, sizeof *this);
        sa.sa_family = domain;
        len = size();
    }

    sockaddr_any(const sockaddr* src)
    {
        // It's not safe to copy it directly, so check.
        if (src->sa_family == AF_INET)
        {
            memcpy(&sin, src, sizeof sin);
            len = sizeof sin;
        }
        else if (src->sa_family == AF_INET6)
        {
            // Note: this isn't too safe, may crash for stupid values
            // of src->sa_family or any other data
            // in the source structure, so make sure it's correct first.
            memcpy(&sin6, src, sizeof sin6);
            len = sizeof sin6;
        }
        else
        {
            // Clear as a sign or error
            memset(&sa, 0, sizeof *this);
            // Error fallback: no other families than IP are regarded.
            sa.sa_family = AF_UNSPEC;
            len = 0;
        }
    }

    sockaddr_any(const in_addr& i4_adr, uint16_t port)
    {
        // Some cases require separately IPv4 address passed as in_addr,
        // so port is given separately.
        sa.sa_family = AF_INET;
        sin.sin_addr = i4_adr;
        sin.sin_port = htons(port);
        len = sizeof sin;
    }

    sockaddr_any(const in6_addr& i6_adr, uint16_t port)
    {
        sa.sa_family = AF_INET6;
        sin6.sin6_addr = i6_adr;
        sin6.sin6_port = htons(port);
        len = sizeof sin6;
    }

    socklen_t size() const
    {
        switch (sa.sa_family)
        {
        case AF_INET: return socklen_t(sizeof sin);
        case AF_INET6: return socklen_t(sizeof sin6);

        default: return 0; // fallback, impossible
        }
    }

    int family() const { return sa.sa_family; }

    // port is in exactly the same location in both sin and sin6
    // and has the same size. This is actually yet another common
    // field, just not mentioned in the sockaddr structure.
    uint16_t& r_port() { return sin.sin_port; }
    uint16_t r_port() const { return sin.sin_port; }
    int hport() const { return ntohs(sin.sin_port); }

    void hport(int value)
    {
        // Port is fortunately located at the same position
        // in both sockaddr_in and sockaddr_in6 and has the
        // same size.
        sin.sin_port = htons(value);
    }

    sockaddr* get() { return &sa; }
    sockaddr* operator&() { return &sa; }

    const sockaddr* get() const { return &sa; }
    const sockaddr* operator&() const { return &sa; }

    template <int> struct TypeMap;

    template <int af_domain>
    typename TypeMap<af_domain>::type& get();

    struct Equal
    {
        bool operator()(const sockaddr_any& c1, const sockaddr_any& c2)
        {
            return memcmp(&c1, &c2, sizeof(c1)) == 0;
        }
    };

    struct EqualAddress
    {
        bool operator()(const sockaddr_any& c1, const sockaddr_any& c2)
        {
            if ( c1.sa.sa_family == AF_INET )
            {
                return c1.sin.sin_addr.s_addr == c2.sin.sin_addr.s_addr;
            }

            if ( c1.sa.sa_family == AF_INET6 )
            {
                return memcmp(&c1.sin6.sin6_addr, &c2.sin6.sin6_addr, sizeof (in6_addr)) == 0;
            }

            return false;
        }

    };

    bool equal_address(const sockaddr_any& rhs) const
    {
        return EqualAddress()(*this, rhs);
    }

    struct Less
    {
        bool operator()(const sockaddr_any& c1, const sockaddr_any& c2)
        {
            return memcmp(&c1, &c2, sizeof(c1)) < 0;
        }
    };

    // Tests if the current address is the "any" wildcard.
    bool isany() const
    {
        if (sa.sa_family == AF_INET)
            return sin.sin_addr.s_addr == INADDR_ANY;
        return memcmp(&sin6.sin6_addr, &in6addr_any, sizeof in6addr_any) == 0;
    }

    bool operator==(const sockaddr_any& other) const
    {
        return Equal()(*this, other);
    }

    bool operator!=(const sockaddr_any& other) const { return !(*this == other); }
};

template<> struct sockaddr_any::TypeMap<AF_INET> { typedef sockaddr_in type; };
template<> struct sockaddr_any::TypeMap<AF_INET6> { typedef sockaddr_in6 type; };

template <>
inline sockaddr_any::TypeMap<AF_INET>::type& sockaddr_any::get<AF_INET>() { return sin; }
template <>
inline sockaddr_any::TypeMap<AF_INET6>::type& sockaddr_any::get<AF_INET6>() { return sin6; }

#endif
