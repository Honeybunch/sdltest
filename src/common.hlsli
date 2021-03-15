struct PushConstants
{
    float4 time;
    float2 resolution;
    float3x4 wvp;
    float3x4 w;
};

#define TAU 6.283185307179586

[[vk::push_constant]]
ConstantBuffer<PushConstants> consts : register(b0);