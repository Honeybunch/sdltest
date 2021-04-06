#pragma once

#include "gpubuffer.h"

#include <stdint.h>

typedef struct VkDevice_T *VkDevice;
typedef struct VkFence_T *VkFence;
typedef struct VkImage_T *VkImage;
typedef struct VkImageView_T *VkImageView;
typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaAllocation_T *VmaAllocation;

typedef struct gpuimage {
  VkImage image;
  VmaAllocation alloc;
} gpuimage;

typedef struct gputexture {
  VkFence uploaded;
  gpubuffer host;
  gpuimage device;
  VkImageView view;
  uint32_t width;
  uint32_t height;
  uint32_t mip_levels;
  uint32_t format;
} gputexture;

void load_texture(VkDevice device, VmaAllocator alloc, const char *filename,
                  gputexture *t);

void destroy_texture(VkDevice device, VmaAllocator alloc, const gputexture *t);