#include "common.hlsli"

// Per-material data - Fragment Stage Only (Maybe vertex stage too later?)
Texture2D albedo_map : register(t0, space0); // Fragment Stage Only
Texture2D normal_map : register(t1, space0); // Fragment Stage Only
Texture2D roughness_map : register(t2, space0); // Fragment Stage Only
sampler static_sampler : register(s3, space0); // Immutable sampler

// Per-object data - Vertex Stage Only
ConstantBuffer<CommonObjectData> object_data: register(b0, space1);

// Per-view data - Fragment Stage Only
ConstantBuffer<CommonCameraData> camera_data: register(b0, space2);
ConstantBuffer<CommonLightData> light_data : register(b1, space2);

#define GLTF_PERM_NORMAL_MAP 0x00000001
#define GLTF_PERM_PBR_METALLIC_ROUGHNESS 0x00000002
#define GLTF_PERM_PBR_SPECULAR_GLOSSINESS 0x00000004
#define GLTF_PERM_CLEARCOAT 0x00000008
#define GLTF_PERM_TRANSMISSION 0x00000010
#define GLTF_PERM_IOR 0x00000020
#define GLTF_PERM_SPECULAR 0x00000040
#define GLTF_PERM_UNLIT 0x00000080

[[vk::constant_id(0)]] const uint32_t PermutationFlags = 0;

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

    float3x3 orientation = (float3x3)object_data.m;

    Interpolators o;
    o.clip_pos = mul(float4(pos, 1.0), object_data.mvp);
    o.world_pos = mul(float4(pos, 1.0), object_data.m).xyz;
    o.normal = mul(i.normal, orientation); // convert to world-space normal
    o.uv = i.uv;
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    // Sample textures up-front
    float3 albedo = albedo_map.Sample(static_sampler, i.uv).rgb;
    float roughness = 0.5;
    
    if(PermutationFlags & GLTF_PERM_PBR_METALLIC_ROUGHNESS)
    {
        roughness = roughness_map.Sample(static_sampler, i.uv).x;
    }

    float gloss = 1 - roughness;

    float3 N = normalize(i.normal);
    if(PermutationFlags & GLTF_PERM_NORMAL_MAP)
    {
        N = normal_map.Sample(static_sampler, i.uv).xyz;
        N = normalize(N * 2 - 1); // Must unpack normal
    }

    // TODO: Use tangents and bitangents to create and apply
    // tangent space to world space transformation matrix.

    float3 lightColor = float3(1, 1, 1);

    // Lighting calcs
    float3 L = normalize(light_data.light_dir);
    
    // Calc ambient light
    float3 ambient = float3(0.01, 0.01, 0.01);

    // Calc diffuse Light
    float lambert = saturate(dot(N, L));
    float3 diffuse = lightColor * lambert;

    // Calc specular light
    float3 V = normalize(camera_data.view_pos - i.world_pos);
    float3 H = normalize(L + V);

    float3 specular_exponent = exp2(gloss * 11) + 2;
    float3 specular = saturate(dot(H, N)) * (lambert > 0); // Blinn-Phong
    specular = pow(specular, specular_exponent) * gloss;
    specular *= lightColor;

    // Compose final lighting color
    float3 color = (albedo * ambient) + (albedo * diffuse) + specular;

    return float4(color, 1);
}