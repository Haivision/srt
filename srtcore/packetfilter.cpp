/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
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

#include "packetfilter.h"
#include "packetfilter_builtin.h"
#include "core.h"
#include "packet.h"
#include "logging.h"

using namespace std;

bool ParseCorrectorConfig(std::string s, FilterConfig& out)
{
    vector<string> parts;
    Split(s, ',', back_inserter(parts));

    out.type = parts[0];
    if (!PacketFilter::exists(out.type))
        return false;

    // Minimum arguments are: rows,cols.
    if (parts.size() < 2)
        return false;

    for (vector<string>::iterator i = parts.begin()+1; i != parts.end(); ++i)
    {
        vector<string> keyval;
        Split(*i, ':', back_inserter(keyval));
        if (keyval.size() != 2)
            return false;
        out.parameters[keyval[0]] = keyval[1];
    }

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

void PacketFilter::receive(CUnit* unit, ref_t< std::vector<CUnit*> > r_incoming, ref_t<loss_seqs_t> r_loss_seqs)
{
    const CPacket& rpkt = unit->m_Packet;

    if (corrector->receive(rpkt, *r_loss_seqs))
    {
        // For the sake of rebuilding MARK THIS UNIT GOOD, otherwise the
        // unit factory will supply it from getNextAvailUnit() as if it were not in use.
        unit->m_iFlag = CUnit::GOOD;
        HLOGC(mglog.Debug, log << "FILTER: PASSTHRU current packet %" << unit->m_Packet.getSeqNo());
        r_incoming.get().push_back(unit);
    }

    // Pack first recovered packets, if any.
    if (!provided.empty())
    {
        HLOGC(mglog.Debug, log << "FILTER: inserting REBUILT packets (" << provided.size() << "):");
        InsertRebuilt(*r_incoming, unitq);
    }

    // Now that all units have been filled as they should be,
    // SET THEM ALL FREE. This is because now it's up to the 
    // buffer to decide as to whether it wants them or not.
    // Wanted units will be set GOOD flag, unwanted will remain
    // with FREE and therefore will be returned at the next
    // call to getNextAvailUnit().
    unit->m_iFlag = CUnit::FREE;
    vector<CUnit*>& inco = *r_incoming;
    for (vector<CUnit*>::iterator i = inco.begin(); i != inco.end(); ++i)
    {
        CUnit* u = *i;
        u->m_iFlag = CUnit::FREE;
    }

    // Packets must be sorted by sequence number, ascending, in order
    // not to challenge the SRT's contiguity checker.
    sort(inco.begin(), inco.end(), SortBySequence());

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

bool PacketFilter::packCorrectionPacket(ref_t<CPacket> r_packet, int32_t seq, int kflg)
{
    bool have = corrector->packControlPacket(*sndctl, seq);
    if (!have)
        return false;

    // Now this should be repacked back to CPacket.
    // The header must be copied, it's always part of CPacket.
    uint32_t* hdr = r_packet.get().getHeader();
    memcpy(hdr, sndctl->hdr, SRT_PH__SIZE * sizeof(*hdr));

    // The buffer can be assigned.
    r_packet.get().m_pcData = sndctl->buffer;
    r_packet.get().setLength(sndctl->length);

    // This sets only the Packet Boundary flags, while all other things:
    // - Order
    // - Rexmit
    // - Crypto
    // - Message Number
    // will be set to 0/false
    r_packet.get().m_iMsgNo = MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO);

    // ... and then fix only the Crypto flags
    r_packet.get().setMsgCryptoFlags(EncryptionKeySpec(kflg));

    // Don't set the ID, it will be later set for any kind of packet.
    // Write the timestamp clip into the timestamp field.
    return true;
}


void PacketFilter::InsertRebuilt(vector<CUnit*>& incoming, CUnitQueue* uq)
{
    if (provided.empty())
        return;

    for (vector<SrtPacket>::iterator i = provided.begin(); i != provided.end(); ++i)
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

        memcpy(packet.getHeader(), i->hdr, CPacket::HDR_SIZE);
        memcpy(packet.m_pcData, i->buffer, i->length);
        packet.setLength(i->length);

        HLOGC(mglog.Debug, log << "FILTER: PROVIDING rebuilt packet %" << packet.getSeqNo());

        incoming.push_back(u);
    }

    provided.clear();
}

bool PacketFilter::IsBuiltin(const string& s)
{
    return builtin_correctors.count(s);
}

std::set<std::string> PacketFilter::builtin_correctors;
PacketFilter::correctors_map_t PacketFilter::correctors;

void PacketFilter::globalInit()
{
    // Add the builtin correctors to the global map.
    // Users may add their correctors after that.
    // This function is called from CUDTUnited::startup,
    // which is guaranteed to run the initializing
    // procedures only once per process.

    correctors["fec"] = &Creator<FECFilterBuiltin>::Create;
    builtin_correctors.insert("fec");
}

bool PacketFilter::configure(CUDT* parent, CUnitQueue* uq, const std::string& confstr)
{
    FilterConfig cfg;
    if (!ParseCorrectorConfig(confstr, cfg))
        return false;

    // Extract the "type" key from parameters, or use
    // builtin if lacking.
    string type = cfg.parameters["type"];

    correctors_map_t::iterator selector;
    if ( type == "")
        selector = correctors.begin();
    else
    {
        selector = correctors.find(type);
        if (selector == correctors.end())
            return false;
    }

    // Found a corrector, so call the creation function
    corrector = (*selector->second)(provided, confstr);
    if (!corrector)
        return false;

    sndctl = new SrtPacket(0); // This only sets the 'length'.

    unitq = uq;

    corrector->m_socket_id = parent->socketID();
    corrector->m_snd_isn = parent->sndSeqNo();
    corrector->m_rcv_isn = parent->rcvSeqNo();
    corrector->m_payload_size = parent->OPT_PayloadSize();

    // The corrector should have pinned in all events
    // that are of its interest. It's stated that
    // it's ready after creation.
    return true;
}

bool PacketFilter::correctConfig(const FilterConfig& conf)
{
    const string* pname = map_getp(conf.parameters, "type");

    if (!pname)
        return true; // default, parameters ignored

    if (*pname == "adaptive")
        return true;

    correctors_map_t::iterator x = correctors.find(*pname);
    if (x == correctors.end())
        return false;

    return true;
}

PacketFilter::~PacketFilter()
{
    delete sndctl;
    delete corrector;
}

