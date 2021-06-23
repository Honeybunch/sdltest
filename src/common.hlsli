struct PushConstants
{
    float4 time;
    float2 resolution;
    
    float4x4 mvp;
    float4x4 m;

    float3 view_pos;

    float3 light_dir;
};

#define TAU 6.283185307179586

[[vk::push_constant]]
ConstantBuffer<PushConstants> consts : register(b0);