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
#include "utilities.h"

class CUDT;
class CorrectorBase;

struct CorrectorConfig
{
    int rows;
    int cols;
    std::map<std::string, std::string> parameters;
};

bool ParseCorrectorConfig(std::string s, CorrectorConfig& out);

class Corrector
{
    friend class CorrectorBase;

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
        static CorrectorBase* Create(CUDT* parent, const std::string confstr)
        { return new Target(parent, confstr); }
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
    Corrector(): corrector() {}

    // Copy constructor - important when listener-spawning
    // Things being done:
    // 1. The corrector is individual, so don't copy it. Set NULL.
    // 2. This will be configued anyway basing on possibly a new rule set.
    Corrector(const Corrector& source): corrector() {}

    // This function will be called by the parent CUDT
    // in appropriate time. It should select appropriate
    // corrector basing on the value in selector, then
    // pin oneself in into CUDT for receiving event signals.
    bool configure(CUDT* parent, const std::string& confstr);

    static bool correctConfig(const CorrectorConfig& c);

    // Will delete the pinned in corrector object.
    // This must be defined in *.cpp file due to virtual
    // destruction.
    ~Corrector();

    enum State
    {
        FS_NEVER,   //< Never send LOSSREPORT (rely on FEC exclusively)
        FS_LATELY, //< Send LOSSREPORT when both horizontal AND vertical group is closed
        FS_EARLY,  //< Send LOSSREPORT when the horizontal group is closed
        FS_ALWAYS, //< Regardless of that we have FEC, always send LOSSREPORT when loss detected

        FS_LAST
    };
};

// Real interface
class CorrectorBase
{
protected:
    CUDT* m_parent;

    typedef Corrector::State State;

    // Configuration
    Corrector::State m_fallback_level;

    // Beside the size of the rows, special values:
    // 0: if you have 0 specified for rows, there are only columns
    // -1: Only during the handshake, use the value specified by peer.
    // -N: The N value still specifies the size, but in particular
    //     dimension there is no FEC control packet formed nor expected.

    int m_size_rows;
    int m_size_columns;

public:

   typedef std::vector< std::pair<int32_t, int32_t> > loss_seqs_t;

    CorrectorBase(CUDT* par): parent(par), m_fallback_level(FS_LAST)
    {
    }

    // Sender side

    // This function creates and stores the FEC control packet with
    // a prediction to be immediately sent. This is called in the function
    // that normally is prepared for extracting a data packet from the sender
    // buffer and send it over the channel.
    virtual bool packCorrectionPacket(ref_t<CPacket> r_packet, int32_t seq, int kflg) = 0;

    // This is called at the moment when the sender queue decided to pick up
    // a new packet from the scheduled packets. This should be then used to
    // continue filling the group, possibly followed by final calculating the
    // FEC control packet ready to send.
    virtual void feedSource(ref_t<CPacket> r_packet) = 0;


    // Receiver side

    // This function is called at the moment when a new data packet has
    // arrived (no matter if subsequent or recovered). The 'state' value
    // defines the configured level of loss state required to send the
    // loss report.
    virtual bool receive(CUnit* unit, ref_t< vector<CUnit*> > r_incoming, ref_t<loss_seqs_t> r_loss_seqs) = 0;

};

#endif
