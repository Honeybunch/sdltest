#pragma once

#include "simd.h"

#include "common.hlsli"
#include "imgui.hlsli"

#define PUSH_CONSTANT_BYTES 128

_Static_assert(sizeof(FullscreenPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
_Static_assert(sizeof(SkyPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
_Static_assert(sizeof(ImGuiPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");

typedef struct SkyData {
  float turbidity;
  float albedo;
  float3 sun_dir;
} SkyData;