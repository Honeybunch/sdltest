#pragma once

#include "simd.h"

#define PUSH_CONSTANT_BYTES 256

typedef struct PushConstants {
  float4 time;
  float2 resolution;
  float3x4 wvp;
  float3x4 w;
} PushConstants;
static_assert(sizeof(PushConstants) <= PUSH_CONSTANT_BYTES,
              "Too Many Push Constants");