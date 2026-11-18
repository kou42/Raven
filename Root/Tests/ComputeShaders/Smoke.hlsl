RWByteAddressBuffer values : register(u0);
RWTexture3D<float> volume : register(u1);

[numthreads(8, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    uint bytes;
    values.GetDimensions(bytes);
    if (i < bytes / 4)
        values.Store(4 * i, values.Load(4 * i) * 3 + 7);
    if (i < 8)
        volume[uint3(i & 1, (i >> 1) & 1, i >> 2)] = float(i) / 8.0;
}

float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    const float2 vertices[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    return float4(vertices[id], 0, 1);
}

Texture3D<float> sampled : register(t0);
SamplerState volumeSampler : register(s0);

float4 PSMain(float4 position : SV_Position) : SV_Target
{
    const int i = int(position.x);
    const float3 uvw = (float3(i & 1, (i >> 1) & 1, i >> 2) + 0.5) / 2.0;
    return float4(sampled.SampleLevel(volumeSampler, uvw, 0), 0, 0, 1);
}
