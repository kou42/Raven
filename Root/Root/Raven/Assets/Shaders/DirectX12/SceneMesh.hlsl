// DX12SceneGraphicsPipelineのRoot Signature契約:
// b0 = column-major Clip Transform、b1 = RGBA Tint、t0 = Texture、s0 = Sampler。
// Vertex BufferはPosition(float3), Color(float3), TexCoord(float2), Normal(float3)。
cbuffer ClipConstants : register(b0)
{
    column_major float4x4 u_ClipTransform;
};

cbuffer MaterialConstants : register(b1)
{
    float4 u_Tint;
};

Texture2D u_Texture : register(t0);
SamplerState u_Sampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float3 Color : COLOR;
    float2 TexCoord : TEXCOORD;
    float3 Normal : NORMAL;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.Position = mul(u_ClipTransform, float4(input.Position, 1.0f));
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(input.Color, 1.0f) *
        u_Texture.Sample(u_Sampler, input.TexCoord) * u_Tint;
}

// DXCによるコンパイル例（実行環境で生成してください）:
// dxc -T vs_6_0 -E VSMain -Fo SceneMesh.vs.dxil SceneMesh.hlsl
// dxc -T ps_6_0 -E PSMain -Fo SceneMesh.ps.dxil SceneMesh.hlsl
