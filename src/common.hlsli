typedef struct FullscreenPushConstants {
  float4 time;
  float2 resolution;
} FullscreenPushConstants;

typedef struct PushConstants {
  float4x4 mvp;
  float4x4 m;

  float3 view_pos;

  float3 light_dir;
} PushConstants;

#define TAU 6.283185307179586