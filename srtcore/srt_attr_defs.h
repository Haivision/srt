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
// SRT_ATR_UNUSED: declare an entity ALLOWED to be unused (prevents warnings)
// ATR_DEPRECATED: declare an entity deprecated (compiler should warn when used)
// ATR_NOEXCEPT: The true `noexcept` from C++11, or nothing if compiling in pre-C++11 mode
// ATR_NOTHROW: In C++11: `noexcept`. In pre-C++11: `throw()`. Required for GNU libstdc++.
// ATR_CONSTEXPR: In C++11: `constexpr`. Otherwise empty.
// ATR_OVERRIDE: In C++11: `override`. Otherwise empty.
// ATR_FINAL: In C++11: `final`. Otherwise empty.

#ifdef __GNUG__
#define ATR_DEPRECATED __attribute__((deprecated))
#else
#define ATR_DEPRECATED
#endif

#if (defined(__cplusplus) && __cplusplus > 199711L) \
 || (defined(_MSVC_LANG) && _MSVC_LANG > 199711L) // Some earlier versions get this wrong
#define HAVE_CXX11 1
// For gcc 4.7, claim C++11 is supported, as long as experimental C++0x is on,
// however it's only the "most required C++11 support".
#if defined(__GXX_EXPERIMENTAL_CXX0X__) && __GNUC__ == 4 && __GNUC_MINOR__ >= 7 // 4.7 only!
#define ATR_NOEXCEPT
#define ATR_NOTHROW throw()
#define ATR_CONSTEXPR
#define ATR_OVERRIDE
#define ATR_FINAL
#else
#define HAVE_FULL_CXX11 1
#define ATR_NOEXCEPT noexcept
#define ATR_NOTHROW noexcept
#define ATR_CONSTEXPR constexpr
#define ATR_OVERRIDE override
#define ATR_FINAL final
#endif
#elif defined(_MSC_VER) && _MSC_VER >= 1800
// Microsoft Visual Studio supports C++11, but not fully,
// and still did not change the value of __cplusplus. Treat
// this special way.
// _MSC_VER == 1800  means Microsoft Visual Studio 2013.
#define HAVE_CXX11 1
#if defined(_MSC_FULL_VER) && _MSC_FULL_VER >= 190023026
#define HAVE_FULL_CXX11 1
#define ATR_NOEXCEPT noexcept
#define ATR_NOTHROW noexcept
#define ATR_CONSTEXPR constexpr
#define ATR_OVERRIDE override
#define ATR_FINAL final
#else
#define ATR_NOEXCEPT
#define ATR_NOTHROW throw()
#define ATR_CONSTEXPR
#define ATR_OVERRIDE
#define ATR_FINAL
#endif
#else
#define HAVE_CXX11 0
#define ATR_NOEXCEPT
#define ATR_NOTHROW throw()
#define ATR_CONSTEXPR
#define ATR_OVERRIDE
#define ATR_FINAL
#endif // __cplusplus


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
#if defined(__clang__) && defined(__clang_major__) && (__clang_major__ > 5)
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
#endif // not _MSC_VER

#endif // INC_SRT_ATTR_DEFS_H
