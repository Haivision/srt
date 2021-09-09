#
# SRT - Secure, Reliable, Transport
# Copyright (c) 2021 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

# Check for pthread_getname_np(3) and pthread_setname_np(3)
#    used in srtcore/threadname.h.
#
# Some BSD distros need to include <pthread_np.h> for pthread_getname_np().
#
# TODO: Some BSD distros have pthread_get_name_np() and pthread_set_name_np()
#     instead of pthread_getname_np() and pthread_setname_np().
#
# Sets:
#     HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H
#     HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H
#     HAVE_PTHREAD_GETNAME_NP
#     HAVE_PTHREAD_SETNAME_NP
# Sets as appropriate:
#     add_definitions(-DHAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H=1)
#     add_definitions(-DHAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H=1)
#     add_definitions(-DHAVE_PTHREAD_GETNAME_NP=1)
#     add_definitions(-DHAVE_PTHREAD_SETNAME_NP=1)

include(CheckSymbolExists)

function(FindPThreadGetSetName)

   # unset(.... PARENT_SCOPE) was introduced in cmake-3.0.2.
   if ("${CMAKE_VERSION}" VERSION_LESS "3.0.2")
      unset(HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H)
      set(HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H "" PARENT_SCOPE)
      unset(HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H CACHE)
      unset(HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H)
      set(HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H "" PARENT_SCOPE)
      unset(HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H CACHE)

      unset(HAVE_PTHREAD_GETNAME_NP)
      set(HAVE_PTHREAD_GETNAME_NP "" PARENT_SCOPE)
      unset(HAVE_PTHREAD_GETNAME_NP CACHE)
      unset(HAVE_PTHREAD_SETNAME_NP)
      set(HAVE_PTHREAD_SETNAME_NP "" PARENT_SCOPE)
      unset(HAVE_PTHREAD_SETNAME_NP CACHE)
   else()
      unset(HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H)
      unset(HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H PARENT_SCOPE)
      unset(HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H CACHE)
      unset(HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H)
      unset(HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H PARENT_SCOPE)
      unset(HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H CACHE)

      unset(HAVE_PTHREAD_GETNAME_NP)
      unset(HAVE_PTHREAD_GETNAME_NP PARENT_SCOPE)
      unset(HAVE_PTHREAD_GETNAME_NP CACHE)
      unset(HAVE_PTHREAD_SETNAME_NP)
      unset(HAVE_PTHREAD_SETNAME_NP PARENT_SCOPE)
      unset(HAVE_PTHREAD_SETNAME_NP CACHE)
   endif()

   set(CMAKE_REQUIRED_DEFINITIONS
      -D_GNU_SOURCE -D_DARWIN_C_SOURCE -D_POSIX_SOURCE=1)
   set(CMAKE_REQUIRED_FLAGS "-pthread")

   message(STATUS "Checking for pthread_(g/s)etname_np in 'pthread_np.h':")
   check_symbol_exists(
      pthread_getname_np "pthread_np.h" HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H)
   if (HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H)
      add_definitions(-DHAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H=1)
   endif()
   check_symbol_exists(
      pthread_setname_np "pthread_np.h" HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H)
   if (HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H)
      add_definitions(-DHAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H=1)
   endif()

   message(STATUS "Checking for pthread_(g/s)etname_np in 'pthread.h':")
   check_symbol_exists(pthread_getname_np "pthread.h" HAVE_PTHREAD_GETNAME_NP)
   if (HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H)
      set(HAVE_PTHREAD_GETNAME_NP TRUE PARENT_SCOPE)
   endif()
   check_symbol_exists(pthread_setname_np "pthread.h" HAVE_PTHREAD_SETNAME_NP)
   if (HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H)
      set(HAVE_PTHREAD_SETNAME_NP TRUE PARENT_SCOPE)
   endif()
   if (HAVE_PTHREAD_GETNAME_NP)
      add_definitions(-DHAVE_PTHREAD_GETNAME_NP=1)
   endif()
   if (HAVE_PTHREAD_SETNAME_NP)
      add_definitions(-DHAVE_PTHREAD_SETNAME_NP=1)
   endif()

   unset(CMAKE_REQUIRED_DEFINITIONS)
   unset(CMAKE_REQUIRED_FLAGS)

endfunction(FindPThreadGetSetName)
