#pragma once

#include "gpubuffer.h"

#include <stdint.h>

typedef struct VkFence_T *VkFence;

typedef struct gpumesh {
  VkFence uploaded;
  size_t idx_count;
  size_t vtx_count;
  VkIndexType idx_type;
  size_t size;
  size_t idx_size;
  size_t vtx_size;
  gpubuffer host;
  gpubuffer gpu;
} gpumesh;