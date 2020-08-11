// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_TEST_INPUT_H_
#define SRC_LIB_FUZZING_FIDL_TEST_INPUT_H_

#include <lib/fxl/macros.h>
#include <lib/zx/vmo.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include "shared-memory.h"

namespace fuzzing {

// This class is a specialization of |SharedMemory| that has a fixed size matching the largest input
// that can be generated by libFuzzer, and that has utility methods to read and write its data.
class TestInput final : public SharedMemory {
 public:
  static const size_t kVmoSize = 1 << 20;  // Matches libFuzzer's "kMaxSaneLen".
  static const size_t kMaxInputSize;

  TestInput();
  ~TestInput() override;

  const uint8_t *data() const { return data_; }
  size_t size() const { return size_ ? *size_ : 0; }

  // Creates a new VMO, maps it, and returns a duplicate handle to it via |out|. If |len| is
  // provided, it MUST be kVmoSize.
  zx_status_t Create(size_t len = kVmoSize) override;

  // Maps |vmo| as the backing shared memory for this test input. If |len| is provided, it MUST be
  // kVmoSize.
  zx_status_t Link(const zx::vmo &vmo, size_t len = kVmoSize) override;

  // If a VMO is mapped, appends |size| bytes of |data| to it.
  zx_status_t Write(const void *data, size_t size) const;

  // If a VMO is mapped, sets the input size to 0.
  zx_status_t Clear() const;

 private:
  uint8_t *data_ = nullptr;
  uint64_t *size_ = nullptr;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(TestInput);
};

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_TEST_INPUT_H_
