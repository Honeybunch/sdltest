#include "common.hlsli"

[[vk::push_constant]]
ConstantBuffer<PushConstants> consts : register(b0);

TextureCube cubemap : register(t0, space0); // Fragment Stage Only

// Immutable sampler
sampler static_sampler : register(s1, space0);

struct VertexIn
{
    float3 local_pos : SV_POSITION;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 view_pos: POSITION0;
};

Interpolators vert(VertexIn i)
{
    Interpolators o;
    o.view_pos = i.local_pos;
    o.view_pos.xy *= -1.0;
    o.clip_pos = mul(float4(i.local_pos, 1.0), consts.mvp);
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    float3 dir = normalize(i.view_pos.xyz);
    return cubemap.Sample(static_sampler, dir);
}