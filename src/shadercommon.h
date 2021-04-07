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