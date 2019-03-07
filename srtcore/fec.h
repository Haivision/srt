/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC__FEC_H
#define INC__FEC_H

#include <cstdlib>
#include <map>
#include <string>

#include "packet.h"
#include "queue.h"
#include "utilities.h"
#include "packetfilter_api.h"

class CUDT;

enum SRT_ARQLevel
{
    SRT_ARQ_NEVER,   //< Never send LOSSREPORT (rely on FEC exclusively)
    SRT_ARQ_ONREQ,
    SRT_ARQ_ALWAYS, //< Regardless of that we have FEC, always send LOSSREPORT when loss detected
};


struct CorrectorConfig
{
    int rows;
    int cols;
    SRT_ARQLevel level;
    std::map<std::string, std::string> parameters;
};

class CorrectorBase;

bool ParseCorrectorConfig(std::string s, CorrectorConfig& out);

class Corrector
{
    friend class CorrectorBase;

public:

    typedef std::vector< std::pair<int32_t, int32_t> > loss_seqs_t;

    struct Packet
    {
        uint32_t hdr[CPacket::PH_SIZE];
        char buffer[CPacket::SRT_MAX_PAYLOAD_SIZE];
        size_t length;

        Packet(size_t size): length(size)
        {
            memset(hdr, 0, sizeof(hdr));
        }
    };
    typedef CorrectorBase* corrector_create_t(std::vector<Packet>&, const std::string& config);

private:
    // Temporarily changed to linear searching, until this is exposed
    // for a user-defined corrector.
    // Note that this is a pointer to function :)

    // The first/second is to mimic the map.
    typedef struct { const char* first; corrector_create_t* second; } NamePtr;
    static NamePtr builtin_correctors[];
    typedef std::map<std::string, corrector_create_t*> correctors_map_t;
    static correctors_map_t correctors;

    // This is a corrector container.
    CorrectorBase* corrector;
    void Check()
    {
#if ENABLE_DEBUG
        if (!corrector)
            abort();
#endif
        // Don't do any check for now.
    }

public:

    static void globalInit();

    template <class Target>
    struct Creator
    {
        static CorrectorBase* Create(std::vector<Corrector::Packet>& provided, const std::string& confstr)
        { return new Target(provided, confstr); }
    };

    static bool IsBuiltin(const std::string&);

    template <class NewCorrector>
    static bool add(const std::string& name)
    {
        if (IsBuiltin(name))
            return false;

        correctors[name] = Creator<NewCorrector>::Create;
        return true;
    }

    // Corrector is optional, so this check should be done always
    // manually.
    bool installed() { return corrector; }
    operator bool() { return installed(); }

    CorrectorBase* operator->() { Check(); return corrector; }

    // In the beginning it's initialized as first, builtin default.
    // Still, it will be created only when requested.
    Corrector(): corrector(), sndctl(), unitq() {}

    // Copy constructor - important when listener-spawning
    // Things being done:
    // 1. The corrector is individual, so don't copy it. Set NULL.
    // 2. This will be configued anyway basing on possibly a new rule set.
    Corrector(const Corrector& source SRT_ATR_UNUSED): corrector(), unitq() {}

    // This function will be called by the parent CUDT
    // in appropriate time. It should select appropriate
    // corrector basing on the value in selector, then
    // pin oneself in into CUDT for receiving event signals.
    bool configure(CUDT* parent, CUnitQueue* uq, const std::string& confstr);

    static bool correctConfig(const CorrectorConfig& c);

    // Will delete the pinned in corrector object.
    // This must be defined in *.cpp file due to virtual
    // destruction.
    ~Corrector();

    // Simple wrappers
    size_t extraSize();
    void feedSource(ref_t<CPacket> r_packet);
    SRT_ARQLevel arqLevel();

    bool packCorrectionPacket(ref_t<CPacket> r_packet, int32_t seq, int kflg);

    void receive(CUnit* unit, ref_t< std::vector<CUnit*> > r_incoming, ref_t<loss_seqs_t> r_loss_seqs);

protected:
    void InsertRebuilt(std::vector<CUnit*>& incoming, CUnitQueue* uq);

    // Sender part
    Packet* sndctl;

    // Receiver part
    CUnitQueue* unitq;
    std::vector<Packet> provided;
};

// Real interface
class CorrectorBase
{
    SRTSOCKET m_socket_id;
    int32_t m_snd_isn;
    int32_t m_rcv_isn;
    size_t m_payload_size;

protected:

    SRTSOCKET socketID() { return m_socket_id; }
    int32_t sndISN() { return m_snd_isn; }
    int32_t rcvISN() { return m_rcv_isn; }
    size_t payloadSize() { return m_payload_size; }

    friend class Corrector;

    // Beside the size of the rows, special values:
    // 0: if you have 0 specified for rows, there are only columns
    // -1: Only during the handshake, use the value specified by peer.
    // -N: The N value still specifies the size, but in particular
    //     dimension there is no FEC control packet formed nor expected.

    typedef std::vector< std::pair<int32_t, int32_t> > loss_seqs_t;

    CorrectorBase()
    {
    }

    // General configuration
    virtual size_t extraSize() = 0;

    // Sender side

    // This function creates and stores the FEC control packet with
    // a prediction to be immediately sent. This is called in the function
    // that normally is prepared for extracting a data packet from the sender
    // buffer and send it over the channel.
    virtual bool packCorrectionPacket(Corrector::Packet& packet, int32_t seq) = 0;

    // This is called at the moment when the sender queue decided to pick up
    // a new packet from the scheduled packets. This should be then used to
    // continue filling the group, possibly followed by final calculating the
    // FEC control packet ready to send.
    virtual void feedSource(CPacket& packet) = 0;


    // Receiver side

    // This function is called at the moment when a new data packet has
    // arrived (no matter if subsequent or recovered). The 'state' value
    // defines the configured level of loss state required to send the
    // loss report.
    virtual bool receive(const CPacket& pkt, loss_seqs_t& loss_seqs) = 0;

    // Backward configuration.
    // This should have some stable value after the configuration is parsed,
    // and it should be a stable value set ONCE, after the FEC module is ready.
    virtual SRT_ARQLevel arqLevel() = 0;

    virtual ~CorrectorBase()
    {
    }
};


inline size_t Corrector::extraSize() { return corrector->extraSize(); }
inline void Corrector::feedSource(ref_t<CPacket> r_packet) { return corrector->feedSource(*r_packet); }
inline SRT_ARQLevel Corrector::arqLevel() { return corrector->arqLevel(); }

#endif
