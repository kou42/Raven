// Explicit Raven UI shader. Solid commandはRuntimeの白Textureを使用するため分岐を持ちません。
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
    float2 Position : POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};
struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.Position = mul(u_ClipTransform, float4(input.Position, 0.0f, 1.0f));
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;
    return output;
}
float4 PSMain(VSOutput input) : SV_TARGET
{
    return input.Color * u_Texture.Sample(u_Sampler, input.TexCoord) * u_Tint;
}
