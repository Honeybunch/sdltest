struct Interpolators
{
    float4 pos : SV_POSITION;
    float2 uv0 : TEXCOORD0;
};

struct PushConstants
{
    float4 time;
    float2 resolution;
};

#define TAU 6.283185307179586

[[vk::push_constant]]
ConstantBuffer<PushConstants> consts : register(b0);

Interpolators vert(uint i : SV_VERTEXID)
{
    Interpolators o;
    o.uv0 = float2((i << 1) & 2, i & 2);
    o.pos = float4(o.uv0 * 2.0f + -1.0f, 0.5f, 1.0f);
    return o;
}