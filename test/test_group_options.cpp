/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Written by:
 *             Haivision Systems Inc.
 */

#include <array>
#include <future>
#include <thread>
#include <string>
#include <gtest/gtest.h>
#include "test_env.h"

// SRT includes
#include "any.hpp"
#include "socketconfig.h"
#include "srt.h"

using namespace std;
using namespace srt;

