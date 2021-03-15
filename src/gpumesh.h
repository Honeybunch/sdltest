#pragma once

#include <stdint.h>

typedef struct VkFence_T *VkFence;
typedef struct VkBuffer_T *VkBuffer;
typedef struct VmaAllocation_T *VmaAllocation;

typedef struct gpubuffer {
  VkBuffer buffer;
  VmaAllocation alloc;
} gpubuffer;

typedef struct gpumesh {
  VkFence uploaded;
  size_t idx_count;
  VkIndexType idx_type;
  size_t size;
  gpubuffer host;
  gpubuffer gpu;
} gpumesh;