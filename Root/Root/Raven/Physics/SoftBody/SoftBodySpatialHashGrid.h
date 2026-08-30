#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Solver/SolverTemporaryAllocationCounter.h"
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
// Cell TableはGeneration付きFlat Hashとして保持します。BuildごとにNodeを破棄せず、Bucketと
// Particle Index BufferのcapacityをXPBD iterationおよびframe間で再利用します。
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

    // Phase ② Temporary allocation計測用Overloadです。
    // Candidate生成アルゴリズム自体は通常vector版と同一に保ち、格納先だけを
    // SolverTemporaryAllocator付きvectorへ変更します。
    //
    // 重要:
    // 通常vectorへ生成してから計測vectorへcopyすると、その通常vector側のHeap allocationが
    // Counterから漏れてBefore値を小さく見せてしまいます。そのためBroad Phaseから直接
    // 計測Allocator付きvectorへpush_backする専用経路を用意します。
    void GenerateCandidatePairs(
        std::vector<
            SoftBodySpatialHashPair,
            SolverTemporaryAllocator<SoftBodySpatialHashPair>>& outPairs) const;

    std::size_t GetOccupiedCellCount() const { return m_ActiveBucketIndices.size(); }
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

    struct ParticleIndexBuffer
    {
        std::vector<uint32_t> Storage;
        std::size_t Count = 0u;

        void Reset() { Count = 0u; }

        void Append(uint32_t particleIndex)
        {
            if (Count == Storage.size())
            {
                const std::size_t newSize = Storage.empty() ? 1u : Storage.size() * 2u;
                Storage.resize(newSize);
            }

            Storage.data()[Count] = particleIndex;
            ++Count;
        }
    };

    struct CellBucket
    {
        CellCoord Coord{};
        ParticleIndexBuffer ParticleIndices;
        uint32_t Generation = 0u;
    };

    CellCoord ComputeCellCoord(const math::Vec3& position) const;
    std::size_t HashCell(const CellCoord& cell) const;

    // Particle数の2倍以上となる2冪Tableを確保し、Load Factorを0.5以下へ保ちます。
    // 容量変更はParticle数が既存上限を超えた場合だけ発生します。
    void EnsureBucketCapacity(std::size_t particleCount);
    void BeginBuildGeneration();
    CellBucket& GetOrActivateBucket(const CellCoord& cell);
    const CellBucket* FindActiveBucket(const CellCoord& cell) const;

    // Pairの格納順は常にParticleA < ParticleBへ正規化します。
    // Cell基準走査ではIndex順とCell順は一致しないため、ここで明示的に保証します。
    static void AppendNormalizedPair(
        uint32_t particleA,
        uint32_t particleB,
        std::vector<SoftBodySpatialHashPair>& outPairs);

private:
    float m_CellSize = 0.05f;
    float m_InverseCellSize = 20.0f;

    // Particleは必ず1 Cellだけへ登録されます。Generationが現在値と一致するBucketだけが有効です。
    // Active Index一覧により、Candidate生成ではTable全体を走査せずOccupied Cellだけを辿ります。
    std::vector<CellBucket> m_Buckets;
    std::vector<std::size_t> m_ActiveBucketIndices;
    std::size_t m_BucketMask = 0u;
    uint32_t m_CurrentGeneration = 0u;

    // Candidate生成時はPositionを再利用しなくなったため、Particle snapshotは保持しません。
    // 数だけ保持することで余分なVec3 copy / memory trafficを削減します。
    std::size_t m_ParticleCount = 0u;
};

} // namespace ph
} // namespace Raven
