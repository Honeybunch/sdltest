#pragma once

#include "simd.h"

#include "common.hlsli"

#include <stdalign.h>

#define PUSH_CONSTANT_BYTES 256

_Static_assert(sizeof(FullscreenPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
_Static_assert(sizeof(PushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");

typedef struct SkyData {
  float turbidity;
  float albedo;
  float3 sun_dir;
} SkyData;