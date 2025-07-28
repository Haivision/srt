/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v.2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
/*****************************************************************************
The file contains various planform and compiler dependent attribute definitions
used by SRT library internally.
 *****************************************************************************/

#ifndef INC_SRT_ATTR_DEFS_H
#define INC_SRT_ATTR_DEFS_H

// ATTRIBUTES:
//
// ATR_NOEXCEPT: The true `noexcept` from C++11, or nothing if compiling in pre-C++11 mode
// ATR_NOTHROW: In C++11: `noexcept`. In pre-C++11: `throw()`. Required for GNU libstdc++.
// ATR_CONSTEXPR: In C++11: `constexpr`. Otherwise empty.
// ATR_OVERRIDE: In C++11: `override`. Otherwise empty.
// ATR_FINAL: In C++11: `final`. Otherwise empty.


#ifdef __GNUG__
#define HAVE_GCC 1
#else
#define HAVE_GCC 0
#endif

#if defined(__cplusplus) && __cplusplus > 199711L \
 || (defined(_MSVC_LANG) && _MSVC_LANG > 199711L) // Some earlier versions get this wrong

#define HAVE_CXX11 1
// For gcc 4.7, claim C++11 is supported, as long as experimental C++0x is on,
// however it's only the "most required C++11 support".
#if defined(__GXX_EXPERIMENTAL_CXX0X__) && __GNUC__ == 4 && __GNUC_MINOR__ >= 7 // 4.7 only!
#else
#define HAVE_FULL_CXX11 1

#if __cplusplus >= 201703L
#define HAVE_CXX17 1
#else
#define HAVE_CXX17 0
#endif

#endif
#elif defined(_MSC_VER) && _MSC_VER >= 1800
// Microsoft Visual Studio supports C++11, but not fully,
// and still did not change the value of __cplusplus. Treat
// this special way.
// _MSC_VER == 1800  means Microsoft Visual Studio 2013.
#define HAVE_CXX11 1
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER >= 190023026
#define HAVE_FULL_CXX11 1

#if __cplusplus >= 201703L
#define HAVE_CXX17 1
#else
#define HAVE_CXX17 0
#endif

#endif
#else
#define HAVE_CXX11 0
#define HAVE_CXX17 0
#endif // __cplusplus

#if HAVE_FULL_CXX11
#define ATR_NOEXCEPT noexcept
#define ATR_NOTHROW noexcept
#define ATR_CONSTEXPR constexpr
#define ATR_OVERRIDE override
#define ATR_FINAL final
#else
// These are both for HAVE_CXX11 == 1 and 0.
#define ATR_NOEXCEPT
#define ATR_NOTHROW throw()
#define ATR_CONSTEXPR
#define ATR_OVERRIDE
#define ATR_FINAL
#endif

#if HAVE_CXX11
#define SRT_ATR_ALIGNAS(n) alignas(n)
#elif HAVE_GCC
#define SRT_ATR_ALIGNAS(n) __attribute__((aligned(n)))
#else
#define SRT_ATR_ALIGNAS(n)
#endif


#if HAVE_CXX11
#define SRT_ATR_ALIGNAS(n) alignas(n)
#elif HAVE_GCC
#define SRT_ATR_ALIGNAS(n) __attribute__((aligned(n)))
#else
#define SRT_ATR_ALIGNAS(n)
#endif

#if !HAVE_CXX11 && defined(REQUIRE_CXX11) && REQUIRE_CXX11 == 1
#error "The currently compiled application required C++11, but your compiler doesn't support it."
#endif

///////////////////////////////////////////////////////////////////////////////
// Attributes for thread safety analysis
// - Clang TSA (https://clang.llvm.org/docs/ThreadSafetyAnalysis.html#mutexheader).
// - MSVC SAL (partially).
// - Other compilers: none.
///////////////////////////////////////////////////////////////////////////////

// TSA SYMBOLS available:
//
// * SRT_TSA_CAPABILITY(x)
// The defined C++ class type has a lockable object capability.
//
// * SRT_TSA_SCOPED_CAPABILITY
// The defined C++ class type has a scoped locking capability.
//
// * SRT_TSA_GUARDED_BY(x)
// Accessing THIS object requires locking x for access.
//
// * SRT_TSA_PT_GUARDED_BY(x)
// The pointer-type field points to an object that should be guarded access by x
//
// * SRT_TSA_LOCK_ORDERS_BEFORE(...)
// THIS mutex must be locked prior to locking given mutex objects
//
// * SRT_TSA_LOCK_ORDERS_AFTER(...)
// THIS mutex must be locked next to locking given mutex objects
// 
// * SRT_TSA_NEEDS_LOCKED(...)
// This function requires that given mutexes be locked prior to calling it
//
// * SRT_TSA_NEEDS_LOCKED2(...)
// Same as SRT_TSA_NEEDS_LOCKED, provided for portability with MSVC
//
// * SRT_TSA_NEEDS_LOCKED_SHARED(...)
// Same as SRT_TSA_NEEDS_LOCKED, but requires a shared lock.
//
// * SRT_TSA_WILL_LOCK(...)
// Declares that after this function has been called, it will leave given mutexes locked.
//
// * SRT_TSA_WILL_LOCK_SHARED(...)
// Like SRT_TSA_WILL_LOCK, but applies to a shared lock
//
// * SRT_TSA_WILL_UNLOCK(...)
// Declares that this function's call will leave given mutexes unlocked.
//
// * SRT_TSA_WILL_UNLOCK_SHARED(...)
// Like SRT_TSA_WILL_UNLOCK, but a shared lock.
//
// * SRT_TSA_WILL_UNLOCK_GENERIC(...)
// Like SRT_TSA_WILL_UNLOCK, but any kind of lock.
//
// * SRT_TSA_WILL_TRY_LOCK(...)
// * SRT_TSA_WILL_TRY_LOCK_SHARED(...)
// This function will try to lock and leave with locked if succeeded
//
// * SRT_TSA_NEEDS_NONLOCKED(...)
// Requires that to call this function the given mutexes must not be locked.
//
// * SRT_TSA_ASSERT_CAPABILITY(x)
// * SRT_TSA_ASSERT_SHARED_CAPABILITY(x)
// Will assert that the mutex is locked

// * SRT_TSA_RETURN_CAPABILITY(x)
// This function will return an access to an object that is a mutex.

// * SRT_TSA_DISABLED
// For this function the TSA will not be done.

#if _MSC_VER >= 1920
// In case of MSVC these attributes have to precede the attributed objects (variable, function).
// E.g. SRT_TSA_GUARDED_BY(mtx) int object;
// It is tricky to annotate e.g. the following function, as clang complaints it does not know 'm'.
// SRT_TSA_NEEDS_NONLOCKED(m) SRT_TSA_WILL_LOCK(m)
// inline void enterCS(Mutex& m) { m.lock(); }
#define SRT_TSA_CAPABILITY(expr)
#define SRT_TSA_SCOPED_CAPABILITY
#define SRT_TSA_GUARDED_BY(expr) _Guarded_by_(expr)
#define SRT_TSA_PT_GUARDED_BY(expr)
#define SRT_TSA_LOCK_ORDERS_BEFORE(...)
#define SRT_TSA_LOCK_ORDERS_AFTER(...)
#define SRT_TSA_NEEDS_LOCKED(expr) _Requires_lock_held_(expr)
#define SRT_TSA_NEEDS_LOCKED2(expr1, expr2) _Requires_lock_held_(expr1) _Requires_lock_held_(expr2)
#define SRT_TSA_NEEDS_LOCKED_SHARED(...)
#define SRT_TSA_WILL_LOCK(expr) _Acquires_nonreentrant_lock_(expr)
#define SRT_TSA_WILL_LOCK_SHARED(...)
#define SRT_TSA_WILL_UNLOCK(expr) _Releases_lock_(expr)
#define SRT_TSA_WILL_UNLOCK_SHARED(...)
#define SRT_TSA_WILL_UNLOCK_GENERIC(...)
#define SRT_TSA_WILL_TRY_LOCK(...) _Acquires_nonreentrant_lock_(expr)
#define SRT_TSA_WILL_TRY_LOCK_SHARED(...)
#define SRT_TSA_NEEDS_NONLOCKED(...)
#define SRT_TSA_ASSERT_CAPABILITY(expr)
#define SRT_TSA_ASSERT_SHARED_CAPABILITY(x)
#define SRT_TSA_RETURN_CAPABILITY(x)
#define SRT_TSA_DISABLED
#else

// Common for clang supporting TCA and unsupported.
#if defined(SRT_ENABLE_CLANG_TSA) && defined(__clang__) && defined(__clang_major__) && (__clang_major__ > 5)
#define SRT_TSA_EXPR(x)   __attribute__((x))
#else
#define SRT_TSA_EXPR(x)   // no-op
#endif

#define SRT_TSA_CAPABILITY(x)   SRT_TSA_EXPR(capability(x))
#define SRT_TSA_SCOPED_CAPABILITY   SRT_TSA_EXPR(scoped_lockable)
#define SRT_TSA_GUARDED_BY(x)   SRT_TSA_EXPR(guarded_by(x))
#define SRT_TSA_PT_GUARDED_BY(x)   SRT_TSA_EXPR(pt_guarded_by(x))
#define SRT_TSA_LOCK_ORDERS_BEFORE(...)   SRT_TSA_EXPR(acquired_before(__VA_ARGS__))
#define SRT_TSA_LOCK_ORDERS_AFTER(...)   SRT_TSA_EXPR(acquired_after(__VA_ARGS__))
#define SRT_TSA_NEEDS_LOCKED(...)   SRT_TSA_EXPR(requires_capability(__VA_ARGS__))
#define SRT_TSA_NEEDS_LOCKED2(...)   SRT_TSA_EXPR(requires_capability(__VA_ARGS__))
#define SRT_TSA_NEEDS_LOCKED_SHARED(...)   SRT_TSA_EXPR(requires_shared_capability(__VA_ARGS__))
#define SRT_TSA_WILL_LOCK(...)   SRT_TSA_EXPR(acquire_capability(__VA_ARGS__))
#define SRT_TSA_WILL_LOCK_SHARED(...)   SRT_TSA_EXPR(acquire_shared_capability(__VA_ARGS__))
#define SRT_TSA_WILL_UNLOCK(...)   SRT_TSA_EXPR(release_capability(__VA_ARGS__))
#define SRT_TSA_WILL_UNLOCK_SHARED(...)   SRT_TSA_EXPR(release_shared_capability(__VA_ARGS__))
#define SRT_TSA_WILL_UNLOCK_GENERIC(...)   SRT_TSA_EXPR(release_generic_capability(__VA_ARGS__))
#define SRT_TSA_WILL_TRY_LOCK(...)   SRT_TSA_EXPR(try_acquire_capability(__VA_ARGS__))
#define SRT_TSA_WILL_TRY_LOCK_SHARED(...)   SRT_TSA_EXPR(try_acquire_shared_capability(__VA_ARGS__))
#define SRT_TSA_NEEDS_NONLOCKED(...)   SRT_TSA_EXPR(locks_excluded(__VA_ARGS__))
#define SRT_TSA_ASSERT_CAPABILITY(x)   SRT_TSA_EXPR(assert_capability(x))
#define SRT_TSA_ASSERT_SHARED_CAPABILITY(x)   SRT_TSA_EXPR(assert_shared_capability(x))
#define SRT_TSA_RETURN_CAPABILITY(x)   SRT_TSA_EXPR(lock_returned(x))
#define SRT_TSA_DISABLED   SRT_TSA_EXPR(no_thread_safety_analysis)
// The caller must not hold the given capabilities.
#endif // not _MSC_VER

#endif // INC_SRT_ATTR_DEFS_H
