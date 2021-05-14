#pragma once

#include "simd.h"

#define PUSH_CONSTANT_BYTES 256

typedef struct PushConstants {
  float4 time;
  float2 resolution;
  float4x4 mvp;
  float4x4 m;
  float3 view_pos;
} PushConstants;
_Static_assert(sizeof(PushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");

typedef struct SkyData {
  float3 ground_albedo;
  float3 sun_dir;
  float3 sun_color;
  float sun_size;
  float turbidity;
  float elevation;
  float3 sky_tint;
  float3 sun_tint;
} SkyData;