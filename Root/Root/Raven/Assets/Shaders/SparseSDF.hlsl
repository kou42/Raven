RWByteAddressBuffer parameters : register(u0);
RWByteAddressBuffer tiles : register(u1);
#define RAVEN_SPARSE
#define VEC3 float3
#define UVEC3 uint3
#define UVEC4 uint4
#define AS_FLOAT asfloat
#define AS_FLOAT3 asfloat
#define ATAN2 atan2

uint4 ReadWord(uint index) { return parameters.Load4(index * 16u); }
void WriteDistance(uint index, float distance) { tiles.Store(index * 4u, asuint(distance)); }

#include "DenseSDF.inc"
#include "SparseSDF.inc"

[numthreads(4, 4, 4)]
void CSMain(uint3 group : SV_GroupID, uint3 cell : SV_GroupThreadID) { EvaluateTile(group.x, cell); }
