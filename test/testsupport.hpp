#ifndef SRT_TESTSUPPORT_H
#define SRT_TESTSUPPORT_H

#include "netinet_any.h"

srt::sockaddr_any CreateAddr(const std::string& name, unsigned short port, int pref_family = AF_INET);


#endif
