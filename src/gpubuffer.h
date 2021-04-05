#pragma once

typedef struct VkBuffer_T *VkBuffer;
typedef struct VmaAllocation_T *VmaAllocation;

typedef struct gpubuffer {
  VkBuffer buffer;
  VmaAllocation alloc;
} gpubuffer;