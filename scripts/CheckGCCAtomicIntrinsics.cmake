#
# SRT - Secure, Reliable, Transport
# Copyright (c) 2021 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

# Check for GCC Atomic Intrinsics and whether libatomic is required.
#
# Sets:
#     HAVE_LIBATOMIC
#     HAVE_GCCATOMIC_INTRINSICS
#     HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC
#     HAVE_GCCATOMIC_INTRINSICS_STATIC
#     HAVE_GCCATOMIC_INTRINSICS_STATIC_REQUIRES_LIBATOMIC
#
# See
#  https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
#  https://gcc.gnu.org/wiki/Atomic/GCCMM/AtomicSync

include(CheckCSourceCompiles)
include(CheckLibraryExists)
include(UnSetVariableFull)

function(CheckGCCAtomicIntrinsics)

   UnSetVariableFull(HAVE_LIBATOMIC)
   UnSetVariableFull(HAVE_GCCATOMIC_INTRINSICS)
   UnSetVariableFull(HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC)
   UnSetVariableFull(HAVE_GCCATOMIC_INTRINSICS_STATIC)
   UnSetVariableFull(HAVE_GCCATOMIC_INTRINSICS_STATIC_REQUIRES_LIBATOMIC)

   unset(CMAKE_REQUIRED_FLAGS)
   unset(CMAKE_REQUIRED_LIBRARIES)
   unset(CMAKE_REQUIRED_LINK_OPTIONS)

   set(CheckGCCAtomicIntrinsics_CODE
      "
      #include<stddef.h>
      #include<stdint.h>
      int main(void)
      {
         ptrdiff_t x = 0;
         intmax_t y = 0;
         __atomic_add_fetch(&x, 1, __ATOMIC_SEQ_CST);
         __atomic_add_fetch(&y, 1, __ATOMIC_SEQ_CST);
         return __atomic_sub_fetch(&x, 1, __ATOMIC_SEQ_CST)
               + __atomic_sub_fetch(&y, 1, __ATOMIC_SEQ_CST);
      }
      ")

   check_library_exists(
      atomic __atomic_fetch_add_8 "" HAVE_LIBATOMIC)

   check_c_source_compiles(
      "${CheckGCCAtomicIntrinsics_CODE}"
      HAVE_GCCATOMIC_INTRINSICS)

   if (NOT HAVE_GCCATOMIC_INTRINSICS
      AND HAVE_LIBATOMIC)
      set(CMAKE_REQUIRED_LIBRARIES "atomic")
      check_c_source_compiles(
         "${CheckGCCAtomicIntrinsics_CODE}"
         HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC)
      if (HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC)
         set(HAVE_GCCATOMIC_INTRINSICS TRUE PARENT_SCOPE)
      endif()
   endif()

   unset(CMAKE_REQUIRED_FLAGS)
   unset(CMAKE_REQUIRED_LIBRARIES)
   unset(CMAKE_REQUIRED_LINK_OPTIONS)

   if (HAVE_GCCATOMIC_INTRINSICS)
      set(CMAKE_REQUIRED_LINK_OPTIONS "-static")
      check_c_source_compiles(
         "${CheckGCCAtomicIntrinsics_CODE}"
         HAVE_GCCATOMIC_INTRINSICS_STATIC)
      if (NOT HAVE_GCCATOMIC_INTRINSICS_STATIC
         AND HAVE_LIBATOMIC)
         set(CMAKE_REQUIRED_LIBRARIES "atomic")
         check_c_source_compiles(
            "${CheckGCCAtomicIntrinsics_CODE}"
            HAVE_GCCATOMIC_INTRINSICS_STATIC)
         if (HAVE_GCCATOMIC_INTRINSICS_STATIC)
            set(HAVE_GCCATOMIC_INTRINSICS_STATIC_REQUIRES_LIBATOMIC TRUE PARENT_SCOPE)
         endif()
      endif()
   endif()

   unset(CMAKE_REQUIRED_FLAGS)
   unset(CMAKE_REQUIRED_LIBRARIES)
   unset(CMAKE_REQUIRED_LINK_OPTIONS)

endfunction(CheckGCCAtomicIntrinsics)
