// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides macros for Fuchsia handle annotations to better facilitate
// Clang static analysis.

#ifndef SYSROOT_ZIRCON_ANALYZER_H_
#define SYSROOT_ZIRCON_ANALYZER_H_

#if defined(__clang_analyzer__)
// The argument that has this annotation would be generated by callee
// and handled back to the caller. Caller is responsible for freeing the handle.
#define ZX_HANDLE_ACQUIRE __attribute__((acquire_handle("Fuchsia")))
// The argument that has this annotation would be released by callee.
// Caller must not use the handle again after calling the function.
#define ZX_HANDLE_RELEASE __attribute__((release_handle("Fuchsia")))
// The argument that has this annotation would be used by callee. Caller still
// owns this handle and is responsible for freeing the handle.
#define ZX_HANDLE_USE __attribute__((use_handle("Fuchsia")))
#else
#define ZX_HANDLE_ACQUIRE
#define ZX_HANDLE_RELEASE
#define ZX_HANDLE_USE
#endif  // __clang_analyzer__

#endif  // SYSROOT_ZIRCON_ANALYZER_H_
