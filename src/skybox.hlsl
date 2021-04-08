#include "common.hlsli"

TextureCube cubemap : register(t0, space0); // Fragment Stage Only

// Immutable sampler
sampler static_sampler : register(s1, space0);

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 view_pos: POSITION0;
};

Interpolators vert(uint id : SV_VertexID)
{
    Interpolators o;
    o.clip_pos = float4(id & 1 ? 1 : -1, id & 2 ? -1 : 1, 1, 1);
    o.view_pos = mul(o.clip_pos, consts.m).xyz;
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    float3 dir = normalize(i.view_pos.xyz);
    return cubemap.Sample(static_sampler, dir);
}