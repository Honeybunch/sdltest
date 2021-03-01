#pragma vertex vert
#pragma fragment frag

// Constants & Textures

struct Interpolators
{
    float4 pos : SV_POSITION;
    float2 uv0 : TEXCOORD0;
};

struct PushConstants
{
    float4 time;
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

float4 frag(Interpolators i) : SV_TARGET
{
    float seconds = consts.time.x;

    float x = cos((i.uv0.x + (seconds / 3)) * TAU * 5) * 0.5 + 0.5;
    float4 o = float4(x, x, x, 1);
    return o;
}

