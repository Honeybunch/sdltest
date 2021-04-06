#include "common.hlsli"

Texture2D albedo_map : register(t0, space0); // Fragment Stage Only
Texture2D displacement_map : register(t1, space0); // Vertex Stage Only
Texture2D normal_map : register(t2, space0); // Fragment Stage Only
Texture2D roughness_map : register(t3, space0); // Fragment Stage Only

// Immutable sampler
sampler static_sampler : register(s4, space0);

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
    float displacement_strength = 0.1;
    float3 pos = i.local_pos;

    // Apply displacement map
    float height = displacement_map.SampleLevel(static_sampler, i.uv, 0).x * 2 - 1;
    pos += i.normal * (height * displacement_strength);

    Interpolators o;
    o.clip_pos = mul(float4(pos, 1.0), consts.mvp);
    o.normal = i.normal;
    o.uv = i.uv;
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    float3 normal = normalize(i.normal);

    float3 albedo = albedo_map.Sample(static_sampler, i.uv).rgb;

    return float4(albedo, 1);
}