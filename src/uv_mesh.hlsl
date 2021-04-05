#include "common.hlsli"

Texture2D albedo_map : register(t0, space0);
Texture2D displacement_map : register(t1, space0);
Texture2D normal_map : register(t2, space0);
Texture2D roughness_map : register(t3, space0);

struct VertexIn
{
    float3 local_pos : SV_POSITION;
    float3 normal : NORMAL0;
    float2 uv: TEXCOORD0;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 normal : NORMAL0;
    float2 uv: TEXCOORD0;
};

Interpolators vert(VertexIn i)
{
    Interpolators o;
    o.clip_pos = mul(float4(i.local_pos, 1.0), consts.mvp);
    o.normal = i.normal;
    o.uv = i.uv;
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    float3 normal = normalize(i.normal);

    return float4(1, 0, 0, 1);
}