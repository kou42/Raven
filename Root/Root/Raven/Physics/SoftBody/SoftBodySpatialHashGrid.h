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
// Soft Body Spatial Hash Pair
// ============================================================================
// Broad Phaseが返すParticle候補Pairです。
// ParticleA < ParticleB を常に保証することで、同じPairが逆順で現れないようにします。
struct SoftBodySpatialHashPair
{
    uint32_t ParticleA = 0u;
    uint32_t ParticleB = 0u;
};

// ============================================================================
// Soft Body Spatial Hash / Uniform Grid
// ============================================================================
// SoftBody / Clothの自己衝突候補をO(N^2)全探索せず絞り込むためのBroad Phaseです。
//
// ParticleはPositionが属する1セルだけへ登録します。
// 候補生成では旧実装の「Particleごとに周囲27セル検索」をやめ、Occupied Cellを基準に
// 同一Cell + 重複しない13方向のNeighbor Cellだけを処理します。
//
// これにより候補集合は同じまま、同じNeighbor Cellに対するunordered_map::find()の
// 重複呼び出しを大幅に削減できます。
class SoftBodySpatialHashGrid
{
public:
    explicit SoftBodySpatialHashGrid(float cellSize = 0.05f);

    void SetCellSize(float cellSize);
    float GetCellSize() const { return m_CellSize; }

    // 登録内容だけを消します。CellSizeは維持します。
    void Clear();

    // 現在のParticle PositionからGridを再構築します。
    // XPBDではConstraint反復中にPositionが変化するため、Self Collisionを解く直前に
    // Build()し直すことを想定しています。
    void Build(const std::vector<SoftBodyParticle>& particles);

    // ========================================================================
    // Candidate Pair Generation
    // ========================================================================
    // 旧実装:
    //   Particleごとに周囲3x3x3=27 Cellを検索
    //
    // 新実装:
    //   Occupied Cellごとに
    //     1. 同一Cell内のPair
    //     2. 13方向だけのNeighbor CellとのCross Pair
    //   を生成します。
    //
    // 全26Neighborの半分だけを見ることで、Cell A -> B と Cell B -> A の二重処理を防ぎます。
    // Narrow Phaseの距離判定はここでは行いません。
    void GenerateCandidatePairs(std::vector<SoftBodySpatialHashPair>& outPairs) const;

    std::size_t GetOccupiedCellCount() const { return m_Cells.size(); }
    std::size_t GetParticleCount() const { return m_ParticleCount; }

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

    // Pairの格納順は常にParticleA < ParticleBへ正規化します。
    // Cell基準走査ではIndex順とCell順は一致しないため、ここで明示的に保証します。
    static void AppendNormalizedPair(
        uint32_t particleA,
        uint32_t particleB,
        std::vector<SoftBodySpatialHashPair>& outPairs);

private:
    float m_CellSize = 0.05f;
    float m_InverseCellSize = 20.0f;

    // Particleは必ず1 Cellだけへ登録されます。
    std::unordered_map<CellCoord, std::vector<uint32_t>, CellCoordHasher> m_Cells;

    // Candidate生成時はPositionを再利用しなくなったため、Particle snapshotは保持しません。
    // 数だけ保持することで余分なVec3 copy / memory trafficを削減します。
    std::size_t m_ParticleCount = 0u;
};

} // namespace ph
} // namespace Raven
