#pragma once

#include <stdint.h>

typedef struct VkBuffer_T *VkBuffer;
typedef struct VmaAllocation_T *VmaAllocation;

typedef struct gpubuffer {
  VkBuffer buffer;
  VmaAllocation alloc;
} gpubuffer;

typedef struct gpumesh {
  gpubuffer geom_host;
  gpubuffer geom_gpu;
} gpumesh;