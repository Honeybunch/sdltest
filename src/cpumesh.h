#pragma once

#include "simd.h"

typedef struct cpumesh {
  uint64_t size;
  uint32_t vertex_count;
  const float3 *positions;
  const float3 *colors;
  const float3 *normals;
} cpumesh;
