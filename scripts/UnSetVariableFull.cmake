#
# SRT - Secure, Reliable, Transport
# Copyright (c) 2021 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

# Notes:
#
#    Macro that UnSets a variable in Cache, Local Scope, and Parent Scope.
#
# Usage:
#
#     UnSetVariableFull(<variable>)

macro(UnSetVariableFull tVariable)
   unset(tVariable)
   unset(tVariable CACHE)
   # unset(.... PARENT_SCOPE) was introduced in cmake-3.0.2.
   if ("${CMAKE_VERSION}" VERSION_LESS "3.0.2")
      set(tVariable "" PARENT_SCOPE)
   else()
      unset(tVariable PARENT_SCOPE)
   endif()
endmacro()
