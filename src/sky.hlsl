#include "common.hlsli"

struct SkyData
{
    float3 ground_albedo;
    float3 sun_dir;
    float3 sun_color;
    float sun_size;
    float turbidity;
    float elevation;
    float3 sky_tint;
    float3 sun_tint;
};
ConstantBuffer<SkyData> sky_data : register(b1, space0); // Fragment Stage Only

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

float angle_of_dot(float dot)
{
    return acos(max(dot, 0.0000001f));
}

float angle_between(float3 lhs, float3 rhs)
{
    return acos(max(dot(lhs, rhs), 0.0000001f));
}

float4 frag(Interpolators i) : SV_TARGET
{
    float3 sample_dir = i.view_pos;
    float3 sun_dir = sky_data.sun_dir;

    float cos_gamma = dot(sample_dir, sun_dir);

    float gamma = angle_of_dot(cos_gamma);
    float theta = angle_between(sample_dir, float3(0, 1, 0));

    return float4(i.view_pos, 1);
}