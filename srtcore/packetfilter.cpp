/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


#include <string>
#include <map>
#include <vector>
#include <deque>
#include <iterator>

#include "packetfilter.h"
#include "packetfilter_builtin.h"
#include "core.h"
#include "packet.h"
#include "logging.h"

using namespace std;
using namespace srt_logging;
using namespace srt::sync;

bool ParseFilterConfig(std::string s, SrtFilterConfig& w_config)
{
    if (!SrtParseConfig(s, (w_config)))
        return false;

    PacketFilter::Factory* fac = PacketFilter::find(w_config.type);
    if (!fac)
        return false;

    // Extract characteristic data
    w_config.extra_size = fac->ExtraSize();

    return true;
}

struct SortBySequence
{
    bool operator()(const CUnit* u1, const CUnit* u2)
    {
        int32_t s1 = u1->m_Packet.getSeqNo();
        int32_t s2 = u2->m_Packet.getSeqNo();

        return CSeqNo::seqcmp(s1, s2) < 0;
    }
};

void PacketFilter::receive(CUnit* unit, std::vector<CUnit*>& w_incoming, loss_seqs_t& w_loss_seqs)
{
    const CPacket& rpkt = unit->m_Packet;

    if (m_filter->receive(rpkt, w_loss_seqs))
    {
        // For the sake of rebuilding MARK THIS UNIT GOOD, otherwise the
        // unit factory will supply it from getNextAvailUnit() as if it were not in use.
        unit->m_iFlag = CUnit::GOOD;
        HLOGC(mglog.Debug, log << "FILTER: PASSTHRU current packet %" << unit->m_Packet.getSeqNo());
        w_incoming.push_back(unit);
    }
    else
    {
        // Packet not to be passthru, update stats
        ScopedLock lg(m_parent->m_StatsLock);
        ++m_parent->m_stats.rcvFilterExtra;
        ++m_parent->m_stats.rcvFilterExtraTotal;
    }

    // w_loss_seqs enters empty into this function and can be only filled here. XXX ASSERT?
    for (loss_seqs_t::iterator i = w_loss_seqs.begin();
            i != w_loss_seqs.end(); ++i)
    {
        // Sequences here are low-high, if there happens any negative distance
        // here, simply skip and report IPE.
        int dist = CSeqNo::seqoff(i->first, i->second) + 1;
        if (dist > 0)
        {
            ScopedLock lg(m_parent->m_StatsLock);
            m_parent->m_stats.rcvFilterLoss += dist;
            m_parent->m_stats.rcvFilterLossTotal += dist;
        }
        else
        {
            LOGC(mglog.Error, log << "FILTER: IPE: loss record: invalid loss: %"
                    << i->first << " - %" << i->second);
        }
    }

    // Pack first recovered packets, if any.
    if (!m_provided.empty())
    {
        HLOGC(mglog.Debug, log << "FILTER: inserting REBUILT packets (" << m_provided.size() << "):");

        size_t nsupply = m_provided.size();
        InsertRebuilt(w_incoming, m_unitq);

        ScopedLock lg(m_parent->m_StatsLock);
        m_parent->m_stats.rcvFilterSupply += nsupply;
        m_parent->m_stats.rcvFilterSupplyTotal += nsupply;
    }

    // Now that all units have been filled as they should be,
    // SET THEM ALL FREE. This is because now it's up to the 
    // buffer to decide as to whether it wants them or not.
    // Wanted units will be set GOOD flag, unwanted will remain
    // with FREE and therefore will be returned at the next
    // call to getNextAvailUnit().
    unit->m_iFlag = CUnit::FREE;
    for (vector<CUnit*>::iterator i = w_incoming.begin(); i != w_incoming.end(); ++i)
    {
        CUnit* u = *i;
        u->m_iFlag = CUnit::FREE;
    }

    // Packets must be sorted by sequence number, ascending, in order
    // not to challenge the SRT's contiguity checker.
    sort(w_incoming.begin(), w_incoming.end(), SortBySequence());

    // For now, report immediately the irrecoverable packets
    // from the row.

    // Later, the `irrecover_row` or `irrecover_col` will be
    // reported only, depending on level settings. For example,
    // with default LATELY level, packets will be reported as
    // irrecoverable only when they are irrecoverable in the
    // vertical group.

    // With "always", do not report any losses, SRT will simply check
    // them itself.

    return;

}

bool PacketFilter::packControlPacket(int32_t seq, int kflg, CPacket& w_packet)
{
    bool have = m_filter->packControlPacket(m_sndctlpkt, seq);
    if (!have)
        return false;

    // Now this should be repacked back to CPacket.
    // The header must be copied, it's always part of CPacket.
    uint32_t* hdr = w_packet.getHeader();
    memcpy((hdr), m_sndctlpkt.hdr, SRT_PH_E_SIZE * sizeof(*hdr));

    // The buffer can be assigned.
    w_packet.m_pcData = m_sndctlpkt.buffer;
    w_packet.setLength(m_sndctlpkt.length);

    // This sets only the Packet Boundary flags, while all other things:
    // - Order
    // - Rexmit
    // - Crypto
    // - Message Number
    // will be set to 0/false
    w_packet.m_iMsgNo = SRT_MSGNO_CONTROL | MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO);

    // ... and then fix only the Crypto flags
    w_packet.setMsgCryptoFlags(EncryptionKeySpec(kflg));

    // Don't set the ID, it will be later set for any kind of packet.
    // Write the timestamp clip into the timestamp field.
    return true;
}


void PacketFilter::InsertRebuilt(vector<CUnit*>& incoming, CUnitQueue* uq)
{
    if (m_provided.empty())
        return;

    for (vector<SrtPacket>::iterator i = m_provided.begin(); i != m_provided.end(); ++i)
    {
        CUnit* u = uq->getNextAvailUnit();
        if (!u)
        {
            LOGC(mglog.Error, log << "FILTER: LOCAL STORAGE DEPLETED. Can't return rebuilt packets.");
            break;
        }

        // LOCK the unit as GOOD because otherwise the next
        // call to getNextAvailUnit will return THE SAME UNIT.
        u->m_iFlag = CUnit::GOOD;
        // After returning from this function, all units will be
        // set back to FREE so that the buffer can decide whether
        // it wants them or not.

        CPacket& packet = u->m_Packet;

        memcpy((packet.getHeader()), i->hdr, CPacket::HDR_SIZE);
        memcpy((packet.m_pcData), i->buffer, i->length);
        packet.setLength(i->length);

        HLOGC(mglog.Debug, log << "FILTER: PROVIDING rebuilt packet %" << packet.getSeqNo());

        incoming.push_back(u);
    }

    m_provided.clear();
}

bool PacketFilter::IsBuiltin(const string& s)
{
    return builtin_filters.count(s);
}

std::set<std::string> PacketFilter::builtin_filters;
PacketFilter::filters_map_t PacketFilter::filters;

PacketFilter::Factory::~Factory()
{
}

void PacketFilter::globalInit()
{
    // Add here builtin packet filters and mark them
    // as builtin. This will disallow users to register
    // external filters with the same name.

    filters["fec"] = new Creator<FECFilterBuiltin>;
    builtin_filters.insert("fec");
}

bool PacketFilter::configure(CUDT* parent, CUnitQueue* uq, const std::string& confstr)
{
    m_parent = parent;

    SrtFilterConfig cfg;
    if (!ParseFilterConfig(confstr, cfg))
        return false;

    // Extract the "type" key from parameters, or use
    // builtin if lacking.
    filters_map_t::iterator selector = filters.find(cfg.type);
    if (selector == filters.end())
        return false;

    SrtFilterInitializer init;
    init.socket_id = parent->socketID();
    init.snd_isn = parent->sndSeqNo();
    init.rcv_isn = parent->rcvSeqNo();
    init.payload_size = parent->OPT_PayloadSize();


    // Found a filter, so call the creation function
    m_filter = selector->second->Create(init, m_provided, confstr);
    if (!m_filter)
        return false;

    m_unitq = uq;

    // The filter should have pinned in all events
    // that are of its interest. It's stated that
    // it's ready after creation.
    return true;
}

bool PacketFilter::correctConfig(const SrtFilterConfig& conf)
{
    const string* pname = map_getp(conf.parameters, "type");

    if (!pname)
        return true; // default, parameters ignored

    if (*pname == "adaptive")
        return true;

    filters_map_t::iterator x = filters.find(*pname);
    if (x == filters.end())
        return false;

    return true;
}

PacketFilter::~PacketFilter()
{
    delete m_filter;
}

