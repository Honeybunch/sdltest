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
  gpubuffer geom_host;
  gpubuffer geom_gpu;
  size_t size;
} gpumesh;