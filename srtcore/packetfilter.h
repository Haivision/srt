/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC__PACKETFILTER_H
#define INC__PACKETFILTER_H

#include <cstdlib>
#include <map>
#include <string>

#include "packet.h"
#include "queue.h"
#include "utilities.h"
#include "packetfilter_api.h"

class PacketFilter
{
    friend class SrtPacketFilterBase;

public:

    typedef std::vector< std::pair<int32_t, int32_t> > loss_seqs_t;

    typedef SrtPacketFilterBase* filter_create_t(const SrtFilterInitializer& init, std::vector<SrtPacket>&, const std::string& config);

private:
    // Temporarily changed to linear searching, until this is exposed
    // for a user-defined filter.
    // Note that this is a pointer to function :)

    // The list of builtin names that are reserved.
    static std::set<std::string> builtin_filters;
    typedef std::map<std::string, filter_create_t*> filters_map_t;
    static filters_map_t filters;

    // This is a filter container.
    SrtPacketFilterBase* m_filter;
    void Check()
    {
#if ENABLE_DEBUG
        if (!m_filter)
            abort();
#endif
        // Don't do any check for now.
    }

public:

    static void globalInit();

    template <class Target>
    struct Creator
    {
        static SrtPacketFilterBase* Create(const SrtFilterInitializer& init, std::vector<SrtPacket>& provided, const std::string& confstr)
        { return new Target(init, provided, confstr); }
    };

    static bool IsBuiltin(const std::string&);

    template <class NewCorrector>
    static bool add(const std::string& name)
    {
        if (IsBuiltin(name))
            return false;

        filters[name] = Creator<NewCorrector>::Create;
        return true;
    }

    static bool exists(const std::string& type)
    {
        return filters.count(type);
    }

    // Corrector is optional, so this check should be done always
    // manually.
    bool installed() const { return m_filter; }
    operator bool() const { return installed(); }

    SrtPacketFilterBase* operator->() { Check(); return m_filter; }

    // In the beginning it's initialized as first, builtin default.
    // Still, it will be created only when requested.
    PacketFilter(): m_filter(), m_parent(), m_sndctlpkt(0), m_unitq() {}

    // Copy constructor - important when listener-spawning
    // Things being done:
    // 1. The filter is individual, so don't copy it. Set NULL.
    // 2. This will be configued anyway basing on possibly a new rule set.
    PacketFilter(const PacketFilter& source SRT_ATR_UNUSED): m_filter(), m_sndctlpkt(0), m_unitq() {}

    // This function will be called by the parent CUDT
    // in appropriate time. It should select appropriate
    // filter basing on the value in selector, then
    // pin oneself in into CUDT for receiving event signals.
    bool configure(CUDT* parent, CUnitQueue* uq, const std::string& confstr);

    static bool correctConfig(const SrtFilterConfig& c);

    // Will delete the pinned in filter object.
    // This must be defined in *.cpp file due to virtual
    // destruction.
    ~PacketFilter();

    // Simple wrappers
    size_t extraSize() const;
    void feedSource(ref_t<CPacket> r_packet);
    SRT_ARQLevel arqLevel();
    bool packControlPacket(ref_t<CPacket> r_packet, int32_t seq, int kflg);
    void receive(CUnit* unit, ref_t< std::vector<CUnit*> > r_incoming, ref_t<loss_seqs_t> r_loss_seqs);

protected:
    void InsertRebuilt(std::vector<CUnit*>& incoming, CUnitQueue* uq);

    CUDT* m_parent;

    // Sender part
    SrtPacket m_sndctlpkt;

    // Receiver part
    CUnitQueue* m_unitq;
    std::vector<SrtPacket> m_provided;
};


inline size_t PacketFilter::extraSize() const { SRT_ASSERT(m_filter); return m_filter->extraSize(); }
inline void PacketFilter::feedSource(ref_t<CPacket> r_packet) { SRT_ASSERT(m_filter); return m_filter->feedSource(*r_packet); }
inline SRT_ARQLevel PacketFilter::arqLevel() { SRT_ASSERT(m_filter); return m_filter->arqLevel(); }

#endif
