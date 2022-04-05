// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_THIRD_PARTY_CHROMIUM_MEDIA_CHROMIUM_UTILS_H_
#define SRC_MEDIA_THIRD_PARTY_CHROMIUM_MEDIA_CHROMIUM_UTILS_H_

#include <algorithm>
#include <deque>
#include <memory>
#include <optional>

#include <lib/fit/function.h>
#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>
#include <safemath/safe_math.h>
#include <src/lib/fxl/memory/weak_ptr.h>
#include <zircon/compiler.h>
#include "safemath/safe_conversions.h"
#include "time_delta.h"

#define MEDIA_EXPORT
#define MEDIA_GPU_EXPORT

#define DCHECK FX_DCHECK
#define DCHECK_GE(a, b) FX_DCHECK((a) >= (b))
#define DCHECK_GT(a, b) FX_DCHECK((a) > (b))
#define DCHECK_LT(a, b) FX_DCHECK((a) < (b))
#define DCHECK_LE(a, b) FX_DCHECK((a) <= (b))
#define DCHECK_EQ(a, b) FX_DCHECK((a) == (b))
#define DCHECK_NE(a, b) FX_DCHECK((a) != (b))

#define CHECK FX_CHECK
#ifndef DLOG
#define DLOG FX_DLOGS
#endif

#ifndef VLOG
#define VLOG FX_VLOGS
#endif

#define FORCE_ALL_LOGS 0
#if !FORCE_ALL_LOGS
#define DVLOG FX_DVLOGS
#define DVLOG_IF(verbose_level, condition)               \
  FX_LAZY_STREAM(FX_VLOG_STREAM(verbose_level, nullptr), \
                 FX_VLOG_IS_ON(verbose_level) && (condition))
#else
// These force logging to be enabled:
#define DVLOG(verbosity) \
  FX_LAZY_STREAM(FX_LOG_STREAM(ERROR, ""), (verbosity) <= 4)
#define DVLOG_IF(verbose_level, condition) \
  FX_LAZY_STREAM(FX_LOG_STREAM(ERROR, ""), (condition))
#endif

#define NOTREACHED FX_NOTREACHED

#define WARN_UNUSED_RESULT __WARN_UNUSED_RESULT
#define FALLTHROUGH __FALLTHROUGH

#define SEQUENCE_CHECKER(name) static_assert(true, "")
#define DCHECK_CALLED_ON_VALID_SEQUENCE(name, ...)
#define DETACH_FROM_SEQUENCE(name)

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  TypeName& operator=(const TypeName&) = delete

// The main difference between scoped_refptr and shared_ptr is that
// scoped_refptr is intrusive, so you can make a new refptr from a raw pointer.
// That isn't used much in this codebase, so ignore it.
template <typename T>
using scoped_refptr = std::shared_ptr<T>;

// Fuchsia supports C++17, so use std::optional for base::Optional.
namespace base {
template <typename T>
using Optional = std::optional<T>;
inline constexpr std::nullopt_t nullopt = std::nullopt;

template <typename T, size_t N>
constexpr size_t size(const T (&array)[N]) noexcept {
  return N;
}

// base/span.h
template <typename T>
using span = cpp20::span<T>;

// base/numerics/checked_math.h
template <typename T>
using CheckedNumeric = safemath::internal::CheckedNumeric<T>;
using safemath::checked_cast;
using safemath::IsValueInRangeForNumericType;

template <typename Dst, typename Src>
constexpr Dst strict_cast(Src value) {
  return static_cast<Dst>(value);
}

// base/callback_forward.h
using OnceClosure = fit::callback<void()>;

// base/containers/circular_deque.h
template <typename T>
using circular_deque = std::deque<T>;

// base/memory/weak_ptr.h
template <typename T>
using WeakPtr = std::weak_ptr<T>;

template <typename T>
using WeakPtrFactory = fxl::WeakPtrFactory<T>;

// base/cxx17_backports.h
using std::clamp;

// base/sys_byteorder.h
inline uint16_t NetToHost16(uint16_t x) {
  return __builtin_bswap16(x);
}
inline uint32_t NetToHost32(uint32_t x) {
  return __builtin_bswap32(x);
}
inline uint64_t NetToHost64(uint64_t x) {
  return __builtin_bswap64(x);
}

// Converts the bytes in |x| from host to network order (endianness), and
// returns the result.
inline uint16_t HostToNet16(uint16_t x) {
  return __builtin_bswap16(x);
}
inline uint32_t HostToNet32(uint32_t x) {
  return __builtin_bswap32(x);
}
inline uint64_t HostToNet64(uint64_t x) {
  return __builtin_bswap64(x);
}

}  // namespace base

namespace media {

namespace limits {
enum {
  // Clients take care of their own frame requirements
  kMaxVideoFrames = 0,
};

}  // namespace limits

}  // namespace media

#endif  // SRC_MEDIA_THIRD_PARTY_CHROMIUM_MEDIA_CHROMIUM_UTILS_H_
