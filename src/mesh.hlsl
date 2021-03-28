#include "common.hlsli"

struct VertexIn
{
    float3 local_pos : SV_POSITION;
    float3 color : COLOR0;
    float3 normal : NORMAL0;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 color : COLOR0;
    float3 normal : NORMAL0;
};

Interpolators vert(VertexIn i)
{
    Interpolators o;
    o.clip_pos = mul(float4(i.local_pos, 1.0), consts.mvp);
    o.color = i.color;
    o.normal = i.normal;
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    float3 color;
    color = i.color;
    return float4(color, 1);
}