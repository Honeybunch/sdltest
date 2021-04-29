#include "common.hlsli"

Texture2D albedo_map : register(t0, space0); // Fragment Stage Only
Texture2D normal_map : register(t1, space0); // Fragment Stage Only
Texture2D roughness_map : register(t2, space0); // Fragment Stage Only

// Immutable sampler
sampler static_sampler : register(s3, space0);

[[vk::constant_id(0)]] const bool UseNormalMap = false;
[[vk::constant_id(1)]] const bool UsePBRMetallicRoughness = false;

struct VertexIn
{
    float3 local_pos : SV_POSITION;
    float3 normal : NORMAL0;
    float2 uv: TEXCOORD0;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 world_pos: POSITION0;
    float3 normal : NORMAL0;
    float2 uv: TEXCOORD0;
};

Interpolators vert(VertexIn i)
{
    // Apply displacement map
    float3 pos = i.local_pos;

    float3x3 orientation = (float3x3)consts.m;

    Interpolators o;
    o.clip_pos = mul(float4(pos, 1.0), consts.mvp);
    o.world_pos = mul(float4(pos, 1.0), consts.m).xyz;
    o.normal = mul(i.normal, orientation); // convert to world-space normal
    o.uv = i.uv;
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    // Sample textures up-front
    float3 albedo = albedo_map.Sample(static_sampler, i.uv).rgb;
    float roughness = roughness_map.Sample(static_sampler, i.uv).x;
    float gloss = 1 - roughness;

    float3 N = normalize(i.normal);
    if(UseNormalMap)
    {
        N = normal_map.Sample(static_sampler, i.uv).xyz;
        N = normalize(N * 2 - 1); // Must unpack normal
    }

    // TODO: Use tangents and bitangents to create and apply
    // tangent space to world space transformation matrix.

    // Change light direction over time
    float seconds = consts.time[0];
    float x = (cos(seconds) + 0.5) * 0.5;
    float y = (sin(seconds) + 0.5) * 0.5;

    float3 lightColor = float3(1, 1, 1);

    // Lighting calcs
    float3 L = normalize(float3(x, y, 1));
    
    // Calc ambient light
    float3 ambient = float3(0.01, 0.01, 0.01);

    // Calc diffuse Light
    float lambert = saturate(dot(N, L));
    float3 diffuse = lightColor * lambert;

    // Calc specular light
    float3 V = normalize(consts.view_pos - i.world_pos);
    float3 H = normalize(L + V);

    float3 specular_exponent = exp2(gloss * 11) + 2;
    float3 specular = saturate(dot(H, N)) * (lambert > 0); // Blinn-Phong
    specular = pow(specular, specular_exponent) * gloss;
    specular *= lightColor;

    // Compose final lighting color
    float3 color = ambient + (albedo * diffuse) + specular;

    return float4(color, 1);
}