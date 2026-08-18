#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodyParticle.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// Soft Body Triangle
// ============================================================================
// Clothの固定TopologyをParticle Index 3点で表します。
// Positionそのものは保持せず、Broad / Narrow Phase実行時にParticle配列から最新位置を参照します。
struct SoftBodyTriangle
{
    uint32_t ParticleA = 0u;
    uint32_t ParticleB = 0u;
    uint32_t ParticleC = 0u;
};

// ParticleとTriangleのBroad Phase候補です。
struct SoftBodyParticleTrianglePair
{
    uint32_t ParticleIndex = 0u;
    uint32_t TriangleIndex = 0u;
};

// ============================================================================
// Particle-Triangle Spatial Hash / Uniform Grid
// ============================================================================
// Particle-Triangle自己衝突専用Broad Phaseです。
//
// ParticleはPositionが属する1セルだけを問い合わせます。一方Triangleは3頂点から作ったAABBを
// CollisionThicknessだけ膨張させ、そのAABBが跨ぐ全セルへ登録します。
// この構成により「ParticleがTriangle表面からThickness以内」にある場合、そのParticleのセルには
// 必ず対象Triangleが登録されるため、全Particle x 全Triangle探索を避けられます。
class SoftBodyTriangleSpatialHashGrid
{
public:
    explicit SoftBodyTriangleSpatialHashGrid(float cellSize = 0.05f);

    void SetCellSize(float cellSize);
    float GetCellSize() const { return m_CellSize; }

    void Clear();

    // Triangle AABBを複数セルへ登録します。
    // expansionは自己衝突Thicknessとして使用し、0未満は0へclampします。
    void BuildTriangles(
        const std::vector<SoftBodyParticle>& particles,
        const std::vector<SoftBodyTriangle>& triangles,
        float expansion);

    // 各Particleが属する1セルだけを参照し、そこへ登録されたTriangleとの候補Pairを返します。
    // Narrow Phase距離判定やTopology除外は呼び出し側の責務です。
    void GenerateParticleTriangleCandidates(
        const std::vector<SoftBodyParticle>& particles,
        std::vector<SoftBodyParticleTrianglePair>& outPairs) const;

    std::size_t GetOccupiedCellCount() const { return m_TriangleCells.size(); }

private:
    struct CellCoord
    {
        int32_t X = 0;
        int32_t Y = 0;
        int32_t Z = 0;

        bool operator==(const CellCoord& rhs) const
        {
            return X == rhs.X && Y == rhs.Y && Z == rhs.Z;
        }
    };

    struct CellCoordHasher
    {
        std::size_t operator()(const CellCoord& coord) const;
    };

    CellCoord ComputeCellCoord(const math::Vec3& position) const;

private:
    float m_CellSize = 0.05f;
    float m_InverseCellSize = 20.0f;
    std::unordered_map<CellCoord, std::vector<uint32_t>, CellCoordHasher> m_TriangleCells;
};

} // namespace ph
} // namespace Raven
