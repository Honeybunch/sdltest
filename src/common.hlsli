#pragma once

// Push Constants Data for a fullscreen pass
typedef struct FullscreenPushConstants {
  float4 time;
  float2 resolution;
} FullscreenPushConstants;

// TODO: Slim this down
typedef struct PushConstants {
  float4x4 mvp;
  float4x4 m;
  float3 view_pos;
  float3 light_dir;
} PushConstants;

// Constant per-view Camera Data
typedef struct CommonCameraData {
  float4x4 vp;
  float4x4 inv_vp;
  float3 view_pos;
} CommonCameraData;

// Constant per-view Light Data
typedef struct CommonLightData {
  float3 light_dir;
} CommonLightData;

// Constant per-object Object Data for common objects
typedef struct CommonObjectData {
  float4x4 mvp;
  float4x4 m;
} CommonObjectData;

#define TAU 6.283185307179586