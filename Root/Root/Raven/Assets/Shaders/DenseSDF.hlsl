RWByteAddressBuffer parameters : register(u0);
RWByteAddressBuffer distances : register(u1);
RWTexture3D<float> volume : register(u2);

#define VEC3 float3
#define VEC4 float4
#define UVEC3 uint3
#define AS_UINT asuint
#define AS_UINT3 asuint
#define ATAN2 atan2

float4 ReadWord(uint index) { return asfloat(parameters.Load4(index * 16u)); }
void WriteDistance(uint3 cell, uint index, float distance)
{
    distances.Store(index * 4u, asuint(distance));
    volume[cell] = distance;
}

#include "DenseSDF.inc"

[numthreads(4, 4, 4)]
void CSMain(uint3 cell : SV_DispatchThreadID) { EvaluateCell(cell); }
