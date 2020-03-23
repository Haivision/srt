/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <memory>

#include "apputil.hpp"
#include "netinet_any.h"

using namespace std;


// NOTE: MINGW currently does not include support for inet_pton(). See
//    http://mingw.5.n7.nabble.com/Win32API-request-for-new-functions-td22029.html
//    Even if it did support inet_pton(), it is only available on Windows Vista
//    and later. Since we need to support WindowsXP and later in ORTHRUS. Many
//    customers still use it, we will need to implement using something like
//    WSAStringToAddress() which is available on Windows95 and later.
//    Support for IPv6 was added on WindowsXP SP1.
// Header: winsock2.h
// Implementation: ws2_32.dll
// See:
//    https://msdn.microsoft.com/en-us/library/windows/desktop/ms742214(v=vs.85).aspx
//    http://www.winsocketdotnetworkprogramming.com/winsock2programming/winsock2advancedInternet3b.html
#if defined(_WIN32) && !defined(HAVE_INET_PTON)
namespace // Prevent conflict in case when still defined
{
int inet_pton(int af, const char * src, void * dst)
{
   struct sockaddr_storage ss;
   int ssSize = sizeof(ss);
   char srcCopy[INET6_ADDRSTRLEN + 1];

   ZeroMemory(&ss, sizeof(ss));

   // work around non-const API
   strncpy(srcCopy, src, INET6_ADDRSTRLEN + 1);
   srcCopy[INET6_ADDRSTRLEN] = '\0';

   if (WSAStringToAddress(
      srcCopy, af, NULL, (struct sockaddr *)&ss, &ssSize) != 0)
   {
      return 0;
   }

   switch (af)
   {
      case AF_INET :
      {
         *(struct in_addr *)dst = ((struct sockaddr_in *)&ss)->sin_addr;
         return 1;
      }
      case AF_INET6 :
      {
         *(struct in6_addr *)dst = ((struct sockaddr_in6 *)&ss)->sin6_addr;
         return 1;
      }
      default :
      {
         // No-Op
      }
   }

   return 0;
}
}
#endif // _WIN32 && !HAVE_INET_PTON

#define NI_MAXNUMERICHOST 64

sockaddr_any CreateAddrInet(const string& name, const string& port)
{
    int error = 0;
    const char *description;
    struct addrinfo *destination;
    struct addrinfo hints;
    size_t length;
    char ni[NI_MAXNUMERICHOST];
    sockaddr_any sa;
    sa.reset();
    // sa.sin6_family = AF_INET6;
    // sa.sin6_port = htons(port);

    if ( name != "" )
    {
        memset (&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
        hints.ai_socktype = SOCK_DGRAM; // Datagram socket
        hints.ai_flags = 0;
        hints.ai_protocol = 0;          // Any protocol

        error = getaddrinfo(name.c_str(), port.c_str(), &hints, &destination);
        if (error < 0)
        {
            description = gai_strerror(error);
            throw ("CreateAddrInet: get address info " + name + " " + description);
        }

        length = destination->ai_addrlen ;
        if (length <= sizeof sa.sin6)
        {
            memcpy(&sa.sin6, destination->ai_addr, destination->ai_addrlen);
        }

        if (length == 0 || length > sizeof sa.sin6)
        {
            freeaddrinfo(destination);
            throw ("CreateAddrInet: address length = " + length);
        }

        error = getnameinfo(&sa.sa, length, ni, sizeof ni, NULL, 0, NI_NUMERICHOST);
        if (error)
        {
            description = gai_strerror(error);
            cout << "getnameinfo() returned " << description << endl;
            throw ("CreateAddrInet: name info");
            freeaddrinfo(destination) ;
        }

        switch (destination->ai_family)
        {
            case AF_INET:
            {
                cout << "IPv4: " << destination->ai_canonname << endl ;
                break;
            }

            case AF_INET6:
            {
                if (IN6_IS_ADDR_MULTICAST (&sa.sin6.sin6_addr))
                {
                    cout << "IPv6 Multicast " << ni << " " << endl;
                }
                else
                {
                    cout << "IPv6 Unicast " << ni << " " << endl;
                }
                break;
            }

            default:
            {
                throw ("CreateAddrInet: protocol family " + destination->ai_family);
            }
        }
        freeaddrinfo (destination) ;
    }
    return sa;
}

string Join(const vector<string>& in, string sep)
{
    if ( in.empty() )
        return "";

    ostringstream os;

    os << in[0];
    for (auto i = in.begin()+1; i != in.end(); ++i)
        os << sep << *i;
    return os.str();
}

// OPTION LIBRARY

OptionScheme::Args OptionName::DetermineTypeFromHelpText(const std::string& helptext)
{
    if (helptext.empty())
        return OptionScheme::ARG_NONE;

    if (helptext[0] == '<')
    {
        // If the argument is <one-argument>, then it's ARG_NONE.
        // If it's <multiple-arguments...>, then it's ARG_VAR.
        // When closing angle bracket isn't found, fallback to ARG_ONE.
        size_t pos = helptext.find('>');
        if (pos == std::string::npos)
            return OptionScheme::ARG_ONE; // mistake, but acceptable

        if (pos >= 4 && helptext.substr(pos-3, 4) == "...>")
            return OptionScheme::ARG_VAR;

        // We have < and > without ..., simply one argument
        return OptionScheme::ARG_ONE;
    }

    if (helptext[0] == '[')
    {
        // Argument in [] means it is optional; in this case
        // you should state that the argument can be given or not.
        return OptionScheme::ARG_VAR;
    }

    // Also as fallback
    return OptionScheme::ARG_NONE;
}

options_t ProcessOptions(char* const* argv, int argc, std::vector<OptionScheme> scheme)
{
    using namespace std;

    string current_key;
    string extra_arg;
    size_t vals = 0;
    OptionScheme::Args type = OptionScheme::ARG_VAR; // This is for no-option-yet or consumed
    map<string, vector<string>> params;
    bool moreoptions = true;

    for (char* const* p = argv+1; p != argv+argc; ++p)
    {
        const char* a = *p;
        // cout << "*D ARG: '" << a << "'\n";
        if (moreoptions && a[0] == '-')
        {
            bool arg_specified = false;
            size_t seppos; // (see goto, it would jump over initialization)
            current_key = a+1;
            if ( current_key == "-" )
            {
                // The -- argument terminates the options.
                // The default key is restored to empty so that
                // it collects now all arguments under the empty key
                // (not-option-assigned argument).
                moreoptions = false;
                goto EndOfArgs;
            }

            // Maintain the backward compatibility with argument specified after :
            // or with one string separated by space inside.
            seppos = current_key.find(':');
            if (seppos == string::npos)
                seppos = current_key.find(' ');
            if (seppos != string::npos)
            {
                // Old option specification.
                extra_arg = current_key.substr(seppos + 1);
                current_key = current_key.substr(0, 0 + seppos);
                arg_specified = true; // Prevent eating args from option list
            }

            params[current_key].clear();
            vals = 0;

            if (extra_arg != "")
            {
                params[current_key].push_back(extra_arg);
                ++vals;
                extra_arg.clear();
            }

            // Find the key in the scheme. If not found, treat it as ARG_NONE.
            for (auto s: scheme)
            {
                if (s.names().count(current_key))
                {
                    // cout << "*D found '" << current_key << "' in scheme type=" << int(s.type) << endl;
                    // If argument was specified using the old way, like
                    // -v:0 or "-v 0", then consider the argument specified and
                    // treat further arguments as either no-option arguments or
                    // new options.
                    if (s.type == OptionScheme::ARG_NONE || arg_specified)
                    {
                        // Anyway, consider it already processed.
                        break;
                    }
                    type = s.type;

                    if ( vals == 1 && type == OptionScheme::ARG_ONE )
                    {
                        // Argument for one-arg option already consumed,
                        // so set to free args.
                        goto EndOfArgs;
                    }
                    goto Found;
                }

            }
            // Not found: set ARG_NONE.
            // cout << "*D KEY '" << current_key << "' assumed type NONE\n";
EndOfArgs:
            type = OptionScheme::ARG_VAR;
            current_key = "";
Found:
            continue;
        }

        // Collected a value - check if full
        // cout << "*D COLLECTING '" << a << "' for key '" << current_key << "' (" << vals << " so far)\n";
        params[current_key].push_back(a);
        ++vals;
        if ( vals == 1 && type == OptionScheme::ARG_ONE )
        {
            // cout << "*D KEY TYPE ONE - resetting to empty key\n";
            // Reset the key to "default one".
            current_key = "";
            vals = 0;
            type = OptionScheme::ARG_VAR;
        }
        else
        {
            // cout << "*D KEY type VAR - still collecting until the end of options or next option.\n";
        }
    }

    return params;
}

string OptionHelpItem(const OptionName& o)
{
    string out = "\t-" + o.main_name;
    string hlp = o.helptext;
    string prefix;

    if (hlp == "")
    {
        hlp = " (Undocumented)";
    }
    else if (hlp[0] != ' ')
    {
        size_t end = string::npos;
        if (hlp[0] == '<')
        {
            end = hlp.find('>');
        }
        else if (hlp[0] == '[')
        {
            end = hlp.find(']');
        }

        if (end != string::npos)
        {
            ++end;
        }
        else
        {
            end = hlp.find(' ');
        }

        if (end != string::npos)
        {
            prefix = hlp.substr(0, end);
            //while (hlp[end] == ' ')
            //    ++end;
            hlp = hlp.substr(end);
            out += " " + prefix;
        }
    }

    out += " -" + hlp;
    return out;
}

// Stats module

class SrtStatsJson : public SrtStatsWriter
{
public: 
    string WriteStats(int sid, const CBytePerfMon& mon) override 
    { 
        std::ostringstream output;
        output << "{";
        output << "\"sid\":" << sid << ",";
        output << "\"time\":" << mon.msTimeStamp << ",";
        output << "\"window\":{";
        output << "\"flow\":" << mon.pktFlowWindow << ",";
        output << "\"congestion\":" << mon.pktCongestionWindow << ",";    
        output << "\"flight\":" << mon.pktFlightSize;    
        output << "},";
        output << "\"link\":{";
        output << "\"rtt\":" << mon.msRTT << ",";
        output << "\"bandwidth\":" << mon.mbpsBandwidth << ",";
        output << "\"maxBandwidth\":" << mon.mbpsMaxBW;
        output << "},";
        output << "\"send\":{";
        output << "\"packets\":" << mon.pktSent << ",";
        output << "\"packetsLost\":" << mon.pktSndLoss << ",";
        output << "\"packetsDropped\":" << mon.pktSndDrop << ",";
        output << "\"packetsRetransmitted\":" << mon.pktRetrans << ",";
        output << "\"packetsFilterExtra\":" << mon.pktSndFilterExtra << ",";
        output << "\"bytes\":" << mon.byteSent << ",";
        output << "\"bytesDropped\":" << mon.byteSndDrop << ",";
        output << "\"mbitRate\":" << mon.mbpsSendRate;
        output << "},";
        output << "\"recv\": {";
        output << "\"packets\":" << mon.pktRecv << ",";
        output << "\"packetsLost\":" << mon.pktRcvLoss << ",";
        output << "\"packetsDropped\":" << mon.pktRcvDrop << ",";
        output << "\"packetsRetransmitted\":" << mon.pktRcvRetrans << ",";
        output << "\"packetsBelated\":" << mon.pktRcvBelated << ",";
        output << "\"packetsFilterExtra\":" << mon.pktRcvFilterExtra << ",";
        output << "\"packetsFilterSupply\":" << mon.pktRcvFilterSupply << ",";
        output << "\"packetsFilterLoss\":" << mon.pktRcvFilterLoss << ",";
        output << "\"bytes\":" << mon.byteRecv << ",";
        output << "\"bytesLost\":" << mon.byteRcvLoss << ",";
        output << "\"bytesDropped\":" << mon.byteRcvDrop << ",";
        output << "\"mbitRate\":" << mon.mbpsRecvRate;
        output << "}";
        output << "}" << endl;
        return output.str();
    } 

    string WriteBandwidth(double mbpsBandwidth) override 
    {
        std::ostringstream output;
        output << "{\"bandwidth\":" << mbpsBandwidth << '}' << endl;
        return output.str();
    }
};

class SrtStatsCsv : public SrtStatsWriter
{
private:
    bool first_line_printed;

public: 
    SrtStatsCsv() : first_line_printed(false) {}

    string WriteStats(int sid, const CBytePerfMon& mon) override 
    { 
        std::ostringstream output;
        if (!first_line_printed)
        {
            output << "Time,SocketID,pktFlowWindow,pktCongestionWindow,pktFlightSize,";
            output << "msRTT,mbpsBandwidth,mbpsMaxBW,pktSent,pktSndLoss,pktSndDrop,";
            output << "pktRetrans,byteSent,byteSndDrop,mbpsSendRate,usPktSndPeriod,";
            output << "pktRecv,pktRcvLoss,pktRcvDrop,pktRcvRetrans,pktRcvBelated,";
            output << "byteRecv,byteRcvLoss,byteRcvDrop,mbpsRecvRate,RCVLATENCYms,";
            // Filter stats
            output << "pktSndFilterExtra,pktRcvFilterExtra,pktRcvFilterSupply,pktRcvFilterLoss";
            output << endl;
            first_line_printed = true;
        }
        int rcv_latency = 0;
        int int_len = sizeof rcv_latency;
        srt_getsockopt(sid, 0, SRTO_RCVLATENCY, &rcv_latency, &int_len);

        output << mon.msTimeStamp << ",";
        output << sid << ",";
        output << mon.pktFlowWindow << ",";
        output << mon.pktCongestionWindow << ",";
        output << mon.pktFlightSize << ",";
        output << mon.msRTT << ",";
        output << mon.mbpsBandwidth << ",";
        output << mon.mbpsMaxBW << ",";
        output << mon.pktSent << ",";
        output << mon.pktSndLoss << ",";
        output << mon.pktSndDrop << ",";
        output << mon.pktRetrans << ",";
        output << mon.byteSent << ",";
        output << mon.byteSndDrop << ",";
        output << mon.mbpsSendRate << ",";
        output << mon.usPktSndPeriod << ",";
        output << mon.pktRecv << ",";
        output << mon.pktRcvLoss << ",";
        output << mon.pktRcvDrop << ",";
        output << mon.pktRcvRetrans << ",";
        output << mon.pktRcvBelated << ",";
        output << mon.byteRecv << ",";
        output << mon.byteRcvLoss << ",";
        output << mon.byteRcvDrop << ",";
        output << mon.mbpsRecvRate << ",";
        output << rcv_latency << ",";
        // Filter stats
        output << mon.pktSndFilterExtra << ",";
        output << mon.pktRcvFilterExtra << ",";
        output << mon.pktRcvFilterSupply << ",";
        output << mon.pktRcvFilterLoss; //<< ",";
        output << endl;
        return output.str();
    }

    string WriteBandwidth(double mbpsBandwidth) override 
    {
        std::ostringstream output;
        output << "+++/+++SRT BANDWIDTH: " << mbpsBandwidth << endl;
        return output.str();
    }
};

class SrtStatsCols : public SrtStatsWriter
{
public: 
    string WriteStats(int sid, const CBytePerfMon& mon) override 
    { 
        std::ostringstream output;
        output << "======= SRT STATS: sid=" << sid << endl;
        output << "PACKETS     SENT: " << setw(11) << mon.pktSent            << "  RECEIVED:   " << setw(11) << mon.pktRecv              << endl;
        output << "LOST PKT    SENT: " << setw(11) << mon.pktSndLoss         << "  RECEIVED:   " << setw(11) << mon.pktRcvLoss           << endl;
        output << "REXMIT      SENT: " << setw(11) << mon.pktRetrans         << "  RECEIVED:   " << setw(11) << mon.pktRcvRetrans        << endl;
        output << "DROP PKT    SENT: " << setw(11) << mon.pktSndDrop         << "  RECEIVED:   " << setw(11) << mon.pktRcvDrop           << endl;
        output << "FILTER EXTRA  TX: " << setw(11) << mon.pktSndFilterExtra  << "        RX:   " << setw(11) << mon.pktRcvFilterExtra    << endl;
        output << "FILTER RX  SUPPL: " << setw(11) << mon.pktRcvFilterSupply << "  RX  LOSS:   " << setw(11) << mon.pktRcvFilterLoss     << endl;
        output << "RATE     SENDING: " << setw(11) << mon.mbpsSendRate       << "  RECEIVING:  " << setw(11) << mon.mbpsRecvRate         << endl;
        output << "BELATED RECEIVED: " << setw(11) << mon.pktRcvBelated      << "  AVG TIME:   " << setw(11) << mon.pktRcvAvgBelatedTime << endl;
        output << "REORDER DISTANCE: " << setw(11) << mon.pktReorderDistance << endl;
        output << "WINDOW      FLOW: " << setw(11) << mon.pktFlowWindow      << "  CONGESTION: " << setw(11) << mon.pktCongestionWindow  << "  FLIGHT: " << setw(11) << mon.pktFlightSize << endl;
        output << "LINK         RTT: " << setw(9)  << mon.msRTT            << "ms  BANDWIDTH:  " << setw(7)  << mon.mbpsBandwidth    << "Mb/s " << endl;
        output << "BUFFERLEFT:  SND: " << setw(11) << mon.byteAvailSndBuf    << "  RCV:        " << setw(11) << mon.byteAvailRcvBuf      << endl;
        return output.str();
    } 

    string WriteBandwidth(double mbpsBandwidth) override 
    {
        std::ostringstream output;
        output << "+++/+++SRT BANDWIDTH: " << mbpsBandwidth << endl;
        return output.str();
    }
};

shared_ptr<SrtStatsWriter> SrtStatsWriterFactory(SrtStatsPrintFormat printformat)
{
    switch (printformat)
    {
    case SRTSTATS_PROFMAT_JSON:
        return make_shared<SrtStatsJson>();
    case SRTSTATS_PROFMAT_CSV:
        return make_shared<SrtStatsCsv>();
    case SRTSTATS_PROFMAT_2COLS:
        return make_shared<SrtStatsCols>();
    default:
        break;
    }
    return nullptr;
}

SrtStatsPrintFormat ParsePrintFormat(string pf)
{
    if (pf == "default")
        return SRTSTATS_PROFMAT_2COLS;

    if (pf == "json")
        return SRTSTATS_PROFMAT_JSON;

    if (pf == "csv")
        return SRTSTATS_PROFMAT_CSV;

    return SRTSTATS_PROFMAT_INVALID;
}


