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
// 現段階ではParticleを「Positionが属する1セル」へ登録し、そのParticleの周囲3x3x3セルを
// 検索して候補Pairを生成します。Collision Diameter以下の距離にあるParticleを取りこぼさないため、
// Cloth Self Collisionで使用する際は CellSize >= SelfCollisionDiameter としてください。
//
// 重要:
// - 負座標はstatic_cast<int>ではなくfloor()でセル化します。
//   例えば x=-0.1, CellSize=1.0 はセル-1であり、0へ切り捨ててはいけません。
// - Particleは1セルだけに入るため、候補生成時に ParticleB > ParticleA とするだけで
//   Pair重複を防げます。
// - 将来Particle-Triangleへ拡張する際は、Triangle AABBを複数セルへ登録する別経路を追加します。
//   Point登録とTriangle登録の責務を混ぜないことで、まずParticle Broad Phaseを検証しやすくします。
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

    // 周囲27セルからParticle候補Pairを生成します。
    // Narrow Phaseの距離判定はここでは行いません。
    void GenerateCandidatePairs(std::vector<SoftBodySpatialHashPair>& outPairs) const;

    std::size_t GetOccupiedCellCount() const { return m_Cells.size(); }
    std::size_t GetParticleCount() const { return m_ParticlePositions.size(); }

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

    // Keyそのものをhash値へ潰さず、整数3成分のCellCoordを保持します。
    // hash collisionが起きてもunordered_mapがoperator==で区別できるため安全です。
    std::unordered_map<CellCoord, std::vector<uint32_t>, CellCoordHasher> m_Cells;

    // Pair生成時に各Particleの基準セルを再計算できるようPositionを保持します。
    // Broad PhaseはSolverのParticleを所有せず、Build時点のsnapshotだけを扱います。
    std::vector<math::Vec3> m_ParticlePositions;
};

} // namespace ph
} // namespace Raven
