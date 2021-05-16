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
    float3 world_pos : POSITION0;
    float3 color : COLOR0;
    float3 normal : NORMAL0;
};

Interpolators vert(VertexIn i)
{
    float4 pos = float4(i.local_pos, 1.0);
    float3x3 orientation = (float3x3)consts.m;

    Interpolators o;
    o.clip_pos = mul(pos, consts.mvp);
    o.world_pos = mul(pos, consts.m).xyz;
    o.color = i.color;
    o.normal = normalize(mul(i.normal, orientation)); // convert to world-space normal
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    float gloss = 0.7; // Should be a material parameter...

    // Change light direction over time
    float seconds = consts.time[0];
    float y = -abs(cos(seconds));
    float z = sin(seconds);

    float3 lightColor = float3(1, 1, 1);

    // Lighting calcs
    float3 L = normalize(float3(0, y, z));
    float3 N = normalize(i.normal);

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
    float3 color = ambient + (i.color * diffuse) + specular;
    return float4(color, 1);
}