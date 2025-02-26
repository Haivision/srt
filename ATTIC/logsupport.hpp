/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_LOGSUPPORT_HPP
#define INC_SRT_LOGSUPPORT_HPP

#include <string>
#include <map>
#include <vector>
#include "logging_api.h"

namespace hvu
{

hvu::logging::LogLevel::type ParseLogLevel(std::string level);
std::set<hvu::logging::LogFA> ParseLogFA(std::string fa, std::set<std::string>* punknown = nullptr);
void ParseLogFASpec(const std::vector<std::string>& speclist, std::string& w_on, std::string& w_off);


}

#endif
