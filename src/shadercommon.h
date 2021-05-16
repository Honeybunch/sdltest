#pragma once

#include "simd.h"

#include <stdalign.h>

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
  alignas(4) float sun_size;
  alignas(4) float turbidity;
  alignas(4) float elevation;
  alignas(4) float overcast;

  alignas(16) float3 albedo_rgb;
  alignas(16) float3 sun_dir;
  alignas(16) float3 sun_color;
  alignas(16) float3 sun_tint_rgb;
  alignas(16) float3 radiance_xyz;

  alignas(4) float hosek_coeffs_x[12];
  alignas(4) float hosek_coeffs_y[12];
  alignas(4) float hosek_coeffs_z[12];
} SkyData;