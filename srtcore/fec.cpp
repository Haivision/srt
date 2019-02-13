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

#include "fec.h"
#include "core.h"
#include "logging.h"

using namespace std;

CorrectorBase::CorrectorBase(CUDT* parent)
{
    m_parent = parent;
}

bool ParseCorrectorConfig(std::string s, CorrectorConfig& out)
{
    vector<string> parts;
    Split(s, ',', back_inserter(parts));

    // Minimum arguments are: rows,cols.
    if (parts.size() < 2)
        return false;

    out.rows = atoi(parts[0].c_str());
    out.cols = atoi(parts[1].c_str());

    if (out.rows == 0 && parts[0] != "0")
        return false;

    if (out.cols == 0 && parts[1] != "0")
        return false;

    for (vector<string>::iterator i = parts.begin()+2; i != parts.end(); ++i)
    {
        vector<string> keyval;
        Split(*i, ':', back_inserter(keyval));
        if (keyval.size() != 2)
            return false;
        out.parameters[keyval[0]] = keyval[1];
    }

    return true;
}

class DefaultCorrector: public CorrectorBase
{
    CorrectorConfig cfg;
public:

    DefaultCorrector(CUDT* parent, const std::string& confstr);

    // Sender side

    // This function creates and stores the FEC control packet with
    // a prediction to be immediately sent. This is called in the function
    // that normally is prepared for extracting a data packet from the sender
    // buffer and send it over the channel.
    virtual bool packCorrectionPacket(ref_t<CPacket> r_packet, int32_t seq) ATR_OVERRIDE;

    // This is called at the moment when the sender queue decided to pick up
    // a new packet from the scheduled packets. This should be then used to
    // continue filling the group, possibly followed by final calculating the
    // FEC control packet ready to send.
    virtual void feedSource(ref_t<CPacket> r_packet) ATR_OVERRIDE;


    // Receiver side

    // This function is called at the moment when a new data packet has
    // arrived (no matter if subsequent or recovered). The 'state' value
    // defines the configured level of loss state required to send the
    // loss report.
    virtual bool receive(CUnit* unit, ref_t< vector<CUnit*> > r_incoming, ref_t<loss_seqs_t> r_loss_seqs) ATR_OVERRIDE;
};

DefaultCorrector::DefaultCorrector(CUDT* parent, const std::string& confstr): CorrectorBase(parent)
{
    if (!ParseCorrectorConfig(confstr, cfg))
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    // Alright, now we need to get the ISN from parent
    // to extract the sequence number allowing qualification to the group.
    // The base values must be prepared so that feedSource can qualify them.
    // Obtain by parent->getSndSeqNo() and parent->getRcvSeqNo()

    // SEPARATE FOR SENDING AND RECEIVING!
}


void DefaultCorrector::feedSource(ref_t<CPacket> r_packet)
{
    // Hang on the matrix. Find by r_packet.get()->getSeqNo().
    // Check if the group got closed by this packet so that the
    // FEC control packet can be now prepared. Remember this
    // fact in the internal state.
}

bool DefaultCorrector::packCorrectionPacket(ref_t<CPacket> r_packet, int32_t seq)
{
    // If the FEC packet is not yet ready for extraction, do nothing and return false.
    // If it's ready for extraction, extract it, and write into the packet.
    //
    // NOTE: seq is the sequence number that WOULD BE placed into the packet,
    // should this be a packet freshly extracted from the sender buffer.
}

bool DefaultCorrector::receive(CUnit* unit, ref_t< vector<CUnit*> > r_incoming, ref_t<loss_seqs_t> r_loss_seqs)
{
    // Get the packet from the incoming stream, already recognized
    // as data packet, and then:
    //
    // (Note that the default builtin FEC mechanism uses such rules:
    //  - allows SRT to get the packet, even if it follows the loss
    //  - depending on m_fallback_level, confirms or denies the need that SRT handle the loss
    //  - in loss_seqs we return those that are not recoverable at the current level
    //  - FEC has no extra header provided, so regular data are passed as is
    //)
    // So, the needs to implement:
    // 
    // 1. If this is a FEC packet, close the group, check for lost packets, try to recover.
    // Check if there is recovery possible, if so, request a new unit and pack the recovered packet there.
    // Report the loss to be reported by SRT according to m_fallback_level:
    // - FS_ALWAYS: N/A for a FEC packet
    // - FS_EARLY: When Horizontal group is closed and the packet is not recoverable, report this in loss_seqs
    // - FS_LATELY: When Horizontal and Vertical group is closed and the packet is not recoverable, report it.
    // - FS_NEVER: Always return empty loss_seqs
    //
    // 2. If this is a regular packet, use it for building the FEC group.
    // - FS_ALWAYS: always return true and leave loss_seqs empty.
    // - others: return false and return nothing in loss_seqs
}


Corrector::NamePtr Corrector::builtin_correctors[] =
{
    {"default", Creator<DefaultCorrector>::Create },
};

bool Corrector::IsBuiltin(const string& s)
{
    size_t size = sizeof builtin_correctors/sizeof(builtin_correctors[0]);
    for (size_t i = 0; i < size; ++i)
        if (s == builtin_correctors[i].first)
            return true;

    return false;
}

Corrector::correctors_map_t Corrector::correctors;

void Corrector::globalInit()
{
    // Add the builtin correctors to the global map.
    // Users may add their correctors after that.
    // This function is called from CUDTUnited::startup,
    // which is guaranteed to run the initializing
    // procedures only once per process.

    for (size_t i = 0; i < sizeof builtin_correctors/sizeof (builtin_correctors[0]); ++i)
        correctors[builtin_correctors[i].first] = builtin_correctors[i].second;

    // Actually there's no problem with calling this function
    // multiple times, at worst it will overwrite existing correctors
    // with the same builtin.
}


bool Corrector::configure(CUDT* parent, const std::string& confstr)
{
    CorrectorConfig cfg;
    if (!ParseCorrectorConfig(confstr, cfg))
        return false;

    // Extract the "type" key from parameters, or use
    // builtin if lacking.
    string type = cfg["type"];

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
    corrector = (*selector->second)(parent, confstr);

    // The corrector should have pinned in all events
    // that are of its interest. It's stated that
    // it's ready after creation.
    return !!corrector;
}

bool Corrector::correctConfig(const CorrectorConfig& conf)
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

Corrector::~Corrector()
{
    delete corrector;
    corrector = 0;
}
