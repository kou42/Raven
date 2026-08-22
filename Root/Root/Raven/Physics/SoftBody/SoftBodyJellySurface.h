#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Physics/SoftBody/SoftBodyJelly.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// Soft Body Jelly Surface Triangle
// ============================================================================
// Jelly内部のTetrahedron Faceのうち、他のTetrahedronと共有されないFaceだけを
// Surface Triangleとして保持します。
//
// ParticleA/B/Cの順序は外向き法線になるように統一します。
// そのためRenderer側では
//
//   cross(B - A, C - A)
//
// をそのまま面法線として利用できます。
struct SoftBodyJellySurfaceTriangle
{
    uint32_t ParticleA = 0u;
    uint32_t ParticleB = 0u;
    uint32_t ParticleC = 0u;
};

// ============================================================================
// Soft Body Jelly Surface
// ============================================================================
// Tetrahedral Jellyの外表面Topologyだけを切り出した軽量データです。
//
// SurfaceParticleIndices:
//   外表面で参照されるParticleだけを重複なく保持します。
//
// Triangles:
//   Solver Particle Indexを直接参照する外表面Triangleです。
//
// Renderer側ではSurfaceParticleIndicesをMesh Vertexへ1対1対応させ、Trianglesから
// Index Bufferを構築できます。内部Particleは描画頂点へ含めないため、格子解像度を上げても
// 不要な内部頂点をGPUへ送らずに済みます。
struct SoftBodyJellySurface
{
    std::vector<uint32_t> SurfaceParticleIndices;
    std::vector<SoftBodyJellySurfaceTriangle> Triangles;
};

class SoftBodyJellySurfaceBuilder
{
public:
    // JellyのTetrahedron Topologyから外表面Faceを抽出します。
    // 同じ3 Particleを持つFaceが2回現れた場合は内部共有Faceとして除外し、
    // 1回しか現れないFaceだけをSurfaceとして残します。
    // SoftBodyJellyBuilderが生成する正の符号付き体積orientationを前提に、
    // 残ったTriangleのwindingは外向きへ揃えられます。
    static SoftBodyJellySurface Build(const SoftBodyJelly& jelly);
};

} // namespace ph
} // namespace Raven
