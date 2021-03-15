#pragma once

#include "simd.h"

typedef struct cpumesh {
  uint64_t index_size;
  uint64_t geom_size;
  uint32_t index_count;
  uint32_t vertex_count;
  const uint16_t *indices;
  const float3 *positions;
  const float3 *colors;
  const float3 *normals;
} cpumesh;
