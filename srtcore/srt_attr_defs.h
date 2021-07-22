/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
/*****************************************************************************
The file contains various planform and compiler dependent attribute definitions
used by SRT library internally.

1. Attributes for thread safety analysis
   - Clang (https://clang.llvm.org/docs/ThreadSafetyAnalysis.html#mutexheader).
   - Other compilers: none.

 *****************************************************************************/

#ifndef INC_SRT_ATTR_DEFS_H
#define INC_SRT_ATTR_DEFS_H

#if _MSC_VER >= 1920
// In case of MSVC these attributes have to preceed the attributed objects (variable, function).
// E.g. SRT_ATTR_GUARDED_BY(mtx) int object;
// It is tricky to annotate e.g. the following function, as clang complaints it does not know 'm'.
// SRT_ATTR_EXCLUDES(m) SRT_ATTR_ACQUIRE(m)
// inline void enterCS(Mutex& m) { m.lock(); }
#define SRT_ATTR_CAPABILITY(expr)
#define SRT_ATTR_SCOPED_CAPABILITY
#define SRT_ATTR_GUARDED_BY(expr) _Guarded_by_(expr)
#define SRT_ATTR_PT_GUARDED_BY(expr)
#define SRT_ATTR_ACQUIRED_BEFORE(...)
#define SRT_ATTR_ACQUIRED_AFTER(...)
#define SRT_ATTR_REQUIRES(expr) _Requires_lock_held_(expr)
#define SRT_ATTR_REQUIRES_SHARED(...)
#define SRT_ATTR_ACQUIRE(expr) _Acquires_nonreentrant_lock_(expr)
#define SRT_ATTR_ACQUIRE_SHARED(...)
#define SRT_ATTR_RELEASE(expr) _Releases_lock_(expr)
#define SRT_ATTR_RELEASE_SHARED(...)
#define SRT_ATTR_RELEASE_GENERIC(...)
#define SRT_ATTR_TRY_ACQUIRE(...) _Acquires_nonreentrant_lock_(expr)
#define SRT_ATTR_TRY_ACQUIRE_SHARED(...)
#define SRT_ATTR_EXCLUDES(...)
#define SRT_ATTR_ASSERT_CAPABILITY(expr)
#define SRT_ATTR_ASSERT_SHARED_CAPABILITY(x)
#define SRT_ATTR_RETURN_CAPABILITY(x)
#define SRT_ATTR_NO_THREAD_SAFETY_ANALYSIS
#else

#if defined(__clang__)
#define THREAD_ANNOTATION_ATTRIBUTE__(x)   __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x)   // no-op
#endif

#define SRT_ATTR_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(capability(x))

#define SRT_ATTR_SCOPED_CAPABILITY \
  THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

#define SRT_ATTR_GUARDED_BY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

#define SRT_ATTR_PT_GUARDED_BY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

#define SRT_ATTR_ACQUIRED_BEFORE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))

#define SRT_ATTR_ACQUIRED_AFTER(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))

#define SRT_ATTR_REQUIRES(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

#define SRT_ATTR_REQUIRES_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

#define SRT_ATTR_ACQUIRE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

#define SRT_ATTR_ACQUIRE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquire_shared_capability(__VA_ARGS__))

#define SRT_ATTR_RELEASE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

#define SRT_ATTR_RELEASE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_shared_capability(__VA_ARGS__))

#define SRT_ATTR_RELEASE_GENERIC(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_generic_capability(__VA_ARGS__))

#define SRT_ATTR_TRY_ACQUIRE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_capability(__VA_ARGS__))

#define SRT_ATTR_TRY_ACQUIRE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_shared_capability(__VA_ARGS__))

#define SRT_ATTR_EXCLUDES(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

#define SRT_ATTR_ASSERT_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_capability(x))

#define SRT_ATTR_ASSERT_SHARED_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_capability(x))

#define SRT_ATTR_RETURN_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

#define SRT_ATTR_NO_THREAD_SAFETY_ANALYSIS \
  THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

#endif // not _MSC_VER

#endif // INC_SRT_ATTR_DEFS_H
