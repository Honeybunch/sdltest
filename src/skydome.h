#pragma once

#include "simd.h"

typedef struct cpumesh cpumesh;
typedef struct allocator allocator;

typedef struct sky_data {
  float3 ground_albedo;
  float3 sun_dir;
  float3 sun_color;
  float sun_size;
  float turbidity;
  float elevation;
  float3 sky_tint;
  float3 sun_tint;
} sky_data;

cpumesh *create_skydome(allocator *a);