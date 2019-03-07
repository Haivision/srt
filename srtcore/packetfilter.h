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

class Corrector
{
    friend class CorrectorBase;

public:

    typedef std::vector< std::pair<int32_t, int32_t> > loss_seqs_t;

    typedef CorrectorBase* corrector_create_t(std::vector<SrtPacket>&, const std::string& config);

private:
    // Temporarily changed to linear searching, until this is exposed
    // for a user-defined corrector.
    // Note that this is a pointer to function :)

    // The list of builtin names that are reserved.
    static std::set<std::string> builtin_correctors;
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
        static CorrectorBase* Create(std::vector<SrtPacket>& provided, const std::string& confstr)
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

    static bool exists(const std::string& type)
    {
        return correctors.count(type);
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
    SrtPacket* sndctl;

    // Receiver part
    CUnitQueue* unitq;
    std::vector<SrtPacket> provided;
};


inline size_t Corrector::extraSize() { return corrector->extraSize(); }
inline void Corrector::feedSource(ref_t<CPacket> r_packet) { return corrector->feedSource(*r_packet); }
inline SRT_ARQLevel Corrector::arqLevel() { return corrector->arqLevel(); }

#endif
