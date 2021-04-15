#pragma once
#ifndef SRT_ATOMIC_H
#define SRT_ATOMIC_H

#include "utilities.h"

#if HAVE_CXX11
#include <atomic>
#elif defined(__GNUC__)
#include "atomic_gcc.h"
#elif defined(_WIN32)
#include "atomic_msvc.h"
#else
#error "platform doesn't support atomic"
#endif

#endif // SRT_ATOMIC_H
