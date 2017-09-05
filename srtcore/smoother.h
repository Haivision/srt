#ifndef INC__SMOOTHER_H
#define INC__SMOOTHER_H

#include <map>
#include <string>

class CUDT;
class SmootherBase;

typedef SmootherBase* smoother_create_t(CUDT* parent);

class Smoother
{
    // Note that this is a pointer to function :)
    static std::map<std::string, smoother_create_t*> registerred_smoothers;

    // Required for portable and problemless global initialization of
    // the above registerred_smoothers with the "builtin" smoothers.
    friend class SmootherBaseInitializer;

    // This is a smoother container.
    SmootherBase* smoother;
    std::map<std::string, smoother_create_t*>::iterator selector;

    void Check();

public:

    // If you predict to allow something to be done on smoother also
    // before it is configured, call this first. If you need it configured,
    // you can rely on Check().
    bool ready() { return smoother; }
    SmootherBase* operator->() { Check(); return smoother; }

    // In the beginning it's uninitialized
    Smoother(): smoother(), selector(registerred_smoothers.end()) {}

    // You can call select() multiple times, until finally
    // the 'configure' method is called.
    bool select(const std::string& name)
    {
        std::map<std::string, smoother_create_t*>::iterator try_selector = registerred_smoothers.find(name);
        if (try_selector == registerred_smoothers.end())
            return false;
        selector = try_selector;
        return true;
    }

    std::string selected_name()
    {
        if (selector == registerred_smoothers.end())
            return "";
        return selector->first;
    }

    // Copy constructor - important when listener-spawning
    // Things being done:
    // 1. The smoother is individual, so don't copy it. Set NULL.
    // 2. The selected name is copied so that it's configured correctly.
    Smoother(const Smoother& source): smoother(), selector(source.selector) {}

    // This function will be called by the parent CUDT
    // in appropriate time. It should select appropriate
    // smoother basing on the value in selector, then
    // pin oneself in into CUDT for receiving event signals.
    bool configure(CUDT* parent);

    // Will delete the pinned in smoother object.
    // This must be defined in *.cpp file due to virtual
    // destruction.
    ~Smoother();


    enum TransAPI
    {
        STA_MESSAGE = 0x1, // sendmsg/recvmsg functions
        STA_BUFFER  = 0x2,  // send/recv functions
        STA_FILE    = 0x3, // sendfile/recvfile functions
    };

    enum TransDir
    {
        STAD_RECV = 0,
        STAD_SEND = 1
    };
};


class SmootherBase
{
protected:
    // Here can be some common fields
    CUDT* m_parent;

    double m_dPktSndPeriod;
    double m_dCWndSize;

    //int m_iBandwidth; // NOT REQUIRED. Use m_parent->bandwidth() instead.
    double m_dMaxCWndSize;

    //int m_iMSS;              // NOT REQUIRED. Use m_parent->MSS() instead.
    //int32_t m_iSndCurrSeqNo; // NOT REQUIRED. Use m_parent->sndSeqNo().
    //int m_iRcvRate;          // NOT REQUIRED. Use m_parent->deliveryRate() instead.
    //int m_RTT;               // NOT REQUIRED. Use m_parent->RTT() instead.
    //char* m_pcParam;         // Used to access m_llMaxBw. Use m_parent->maxBandwidth() instead.

    // Constructor in protected section so that this class is semi-abstract.
    SmootherBase(CUDT* parent);
public:

    // This could be also made abstract, but this causes a linkage
    // problem in C++: this would constitute the first virtual method,
    // and C++ compiler uses the location of the first virtual method as the
    // file to which it also emits the virtual call table. When this is
    // abstract, there would have to be simultaneously either defined
    // an empty method in smoother.cpp file (obviously never called),
    // or simply left empty body here.
    virtual ~SmootherBase() { }

    // All these functions that return values interesting for processing
    // by CUDT can be overridden. Normally they should refer to the fields
    // and these fields should keep the values as a state.
    virtual double pktSndPeriod_us() { return m_dPktSndPeriod; }
    virtual double cgWindowSize() { return m_dCWndSize; }
    virtual double cgWindowMaxSize() { return m_dMaxCWndSize; }

    virtual int64_t sndBandwidth() { return 0; }

    // If user-defined, will return nonzero value.
    // If not, it will be internally calculated.
    virtual int RTO() { return 0; }

    // How many packets to send one ACK, in packets.
    // If user-defined, will return nonzero value.  It can enforce extra ACKs
    // beside those calculated by ACK, sent only when the number of packets
    // received since the last EXP that fired "fat" ACK does not exceed this
    // value.
    virtual int ACKInterval() { return 0; }

    // Periodical timer to send an ACK, in microseconds.
    // If user-defined, this value in microseconds will be used to calculate
    // the next ACK time every time ACK is considered to be sent (see CUDT::checkTimers).
    // Otherwise this will be calculated internally in CUDT, normally taken
    // from CPacket::SYN_INTERVAL.
    virtual int ACKPeriod() { return 0; }

    // Called when the settings concerning m_llMaxBW were changed.
    // Arg 1: value of CUDT::m_llMaxBW
    // Arg 2: value calculated out of CUDT::m_llInputBW and CUDT::m_iOverheadBW.
    virtual void updateBandwidth(int64_t, int64_t) {}

    virtual bool needsQuickACK(const CPacket&)
    {
        return false;
    }

    // A smoother is allowed to agree or disagree on the use of particular API.
    virtual bool checkTransArgs(int /*flags*/, const char* /*buffer*/, size_t /*size*/, int /*ttl*/, bool /*inorder*/)
    {
        return true;
    }

protected:
    typedef Bits<2> TRANS_DIR;
    typedef Bits<1, 0> TRANS_API;
};




#endif
