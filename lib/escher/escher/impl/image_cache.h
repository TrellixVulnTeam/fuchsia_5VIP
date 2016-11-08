// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/forward_declarations.h"
#include "escher/impl/gpu_mem.h"
#include "escher/renderer/image.h"
#include "ftl/macros.h"
#include "ftl/memory/ref_counted.h"

namespace escher {
namespace impl {

class CommandBufferPool;

// Allow client to obtain new or recycled Images.  All Images obtained from an
// ImageCache must be destroyed before the ImageCache is destroyed.
// TODO: cache returned images so that we don't need to reallocate new ones.
class ImageCache {
 public:
  // The allocator is used to allocate memory for newly-created images.  The
  // queue and CommandBufferPool are used to schedule image layout transitions.
  ImageCache(vk::Device device,
             vk::PhysicalDevice physical_device,
             CommandBufferPool* main_pool,
             CommandBufferPool* transfer_pool,
             GpuAllocator* allocator);
  ~ImageCache();
  ImagePtr NewImage(const vk::ImageCreateInfo& info,
                    vk::MemoryPropertyFlags memory_flags);

  ImagePtr GetDepthImage(vk::Format format, uint32_t width, uint32_t height);

  // Return new Image containing the provided pixels.  Use transfer queue to
  // efficiently transfer image data to GPU.
  ImagePtr NewRgbaImage(uint32_t width, uint32_t height, uint8_t* bytes);

 private:
  // TODO: merge this with base Image class.  I now think that the correct
  // approach is not to reuse "high-level" objects such as images and buffers,
  // but instead to intelligently manage the underlying memory.
  class Image : public escher::Image {
   public:
    Image(vk::Image image,
          vk::Format format,
          uint32_t width,
          uint32_t height,
          GpuMemPtr memory,
          ImageCache* cache);
    ~Image();

    uint8_t* Map() override;
    void Unmap() override;

   private:
    ImageCache* cache_;
    GpuMemPtr mem_;
    void* mapped_ = nullptr;

    FTL_DISALLOW_COPY_AND_ASSIGN(Image);
  };

  void DestroyImage(vk::Image image, vk::Format format);

  vk::Device device_;
  vk::PhysicalDevice physical_device_;
  vk::Queue main_queue_;
  vk::Queue transfer_queue_;
  CommandBufferPool* main_command_buffer_pool_;
  CommandBufferPool* transfer_command_buffer_pool_;
  GpuAllocator* allocator_;
  uint32_t image_count_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(ImageCache);
};

}  // namespace impl
}  // namespace escher
