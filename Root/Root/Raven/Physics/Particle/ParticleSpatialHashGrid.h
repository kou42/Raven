#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <utility>
#include <vector>

#include "Raven/Math/MathVector.h"

namespace Raven
{
namespace ph
{

// ============================================================================
// Particle Spatial Hash Pair
// ============================================================================
// Particle系Simulationで共通利用するBroad Phase候補Pairです。
// ParticleA < ParticleB を常に保証し、同じPairが逆順で現れないようにします。
struct ParticleSpatialHashPair
{
    uint32_t ParticleA = 0u;
    uint32_t ParticleB = 0u;
};

// ============================================================================
// Particle Spatial Hash / Uniform Grid Core
// ============================================================================
// SoftBody / Cloth / Fluidなど、Positionを持つParticle群で共通利用するSpatial Hash Coreです。
// Particle固有型には依存せず、BeginBuild() + AddParticle()でIndexとPositionだけを受け取ります。
//
// Pair候補生成ではOccupied Cellを基準に、同一Cell + 重複しない13方向のNeighbor Cellだけを処理します。
// Fluid/SPH向けNeighbor Queryでは検索半径から必要なCell範囲を計算し、候補Particle Indexを列挙します。
// 正確な距離判定はPositionを保持する各Simulation側で行い、CoreはSpatial Queryだけを担当します。
//
// Cell TableはGeneration付きFlat Hashとして保持します。BuildごとにNodeを破棄せず、Bucketと
// Particle Index Bufferのcapacityをiterationおよびframe間で再利用します。
class ParticleSpatialHashGrid
{
public:
    explicit ParticleSpatialHashGrid(float cellSize = 0.05f);

    void SetCellSize(float cellSize);
    float GetCellSize() const { return m_CellSize; }

    // 登録内容だけを消します。CellSizeは維持します。
    void Clear();

    // 新しいParticle集合の登録を開始します。
    // particleCountはBucket容量の事前確保と統計値に利用します。
    void BeginBuild(std::size_t particleCount);

    // Particle固有型をCoreへ持ち込まないため、IndexとPositionだけを登録します。
    // 同一particleIndexを複数回登録しないことは呼び出し側の責務です。
    void AddParticle(uint32_t particleIndex, const math::Vec3& position);

    // Allocatorに依存しないCandidate Pair生成です。
    // std::vector<ParticleSpatialHashPair>だけでなく、SolverTemporaryAllocatorを持つvectorも
    // 同じ走査をそのまま利用できるため、SoftBody側に重複した13方向走査を持たせません。
    template <typename PairContainer>
    void GenerateCandidatePairs(PairContainer& outPairs) const
    {
        outPairs.clear();

        for (std::size_t activeBucketIndex : m_ActiveBucketIndices)
        {
            const CellBucket& centerBucket = m_Buckets[activeBucketIndex];
            const CellCoord& centerCell = centerBucket.Coord;
            const ParticleIndexBuffer& centerParticles = centerBucket.ParticleIndices;

            // 同一Cell内はi<jだけを生成します。
            for (std::size_t firstIndex = 0u; firstIndex < centerParticles.Count; ++firstIndex)
            {
                for (std::size_t secondIndex = firstIndex + 1u;
                     secondIndex < centerParticles.Count;
                     ++secondIndex)
                {
                    AppendNormalizedPair(
                        centerParticles.Storage.data()[firstIndex],
                        centerParticles.Storage.data()[secondIndex],
                        outPairs);
                }
            }

            // 全26方向ではなく13方向だけを見ることで、隣接Cell Pairを厳密に1回だけ処理します。
            for (const NeighborOffset& offset : UniqueNeighborOffsets)
            {
                CellCoord neighborCell{};
                neighborCell.X = centerCell.X + offset.X;
                neighborCell.Y = centerCell.Y + offset.Y;
                neighborCell.Z = centerCell.Z + offset.Z;

                const CellBucket* neighborBucket = FindActiveBucket(neighborCell);
                if (neighborBucket == nullptr)
                {
                    continue;
                }

                const ParticleIndexBuffer& neighborParticles = neighborBucket->ParticleIndices;
                for (std::size_t centerIndex = 0u; centerIndex < centerParticles.Count; ++centerIndex)
                {
                    const uint32_t centerParticle = centerParticles.Storage.data()[centerIndex];
                    for (std::size_t neighborIndex = 0u;
                         neighborIndex < neighborParticles.Count;
                         ++neighborIndex)
                    {
                        const uint32_t neighborParticle =
                            neighborParticles.Storage.data()[neighborIndex];
                        AppendNormalizedPair(centerParticle, neighborParticle, outPairs);
                    }
                }
            }
        }
    }

    // positionを中心にsearchRadiusへ到達し得るCellを列挙し、登録Particle Indexをvisitorへ渡します。
    // Coreは各ParticleのPositionを保持しないため、球半径による厳密な距離判定は呼び出し側で行います。
    // searchRadius > CellSizeの場合もceil(radius / cellSize)だけCell範囲を広げるため取りこぼしません。
    template <typename Visitor>
    void ForEachNeighborParticle(
        const math::Vec3& position,
        float searchRadius,
        Visitor&& visitor) const
    {
        if (m_Buckets.empty())
        {
            return;
        }

        const float clampedSearchRadius = std::max(0.0f, searchRadius);
        const int32_t cellRange = static_cast<int32_t>(
            std::ceil(clampedSearchRadius * m_InverseCellSize));
        const CellCoord centerCell = ComputeCellCoord(position);

        for (int32_t z = -cellRange; z <= cellRange; ++z)
        {
            for (int32_t y = -cellRange; y <= cellRange; ++y)
            {
                for (int32_t x = -cellRange; x <= cellRange; ++x)
                {
                    CellCoord neighborCell{};
                    neighborCell.X = centerCell.X + x;
                    neighborCell.Y = centerCell.Y + y;
                    neighborCell.Z = centerCell.Z + z;

                    const CellBucket* bucket = FindActiveBucket(neighborCell);
                    if (bucket == nullptr)
                    {
                        continue;
                    }

                    const ParticleIndexBuffer& particleIndices = bucket->ParticleIndices;
                    for (std::size_t particleIndex = 0u;
                         particleIndex < particleIndices.Count;
                         ++particleIndex)
                    {
                        visitor(particleIndices.Storage.data()[particleIndex]);
                    }
                }
            }
        }
    }

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

    struct NeighborOffset
    {
        int32_t X = 0;
        int32_t Y = 0;
        int32_t Z = 0;
    };

    // 3x3x3近傍のうち辞書順で正方向となる13 Cellだけを走査し、
    // Cell A -> B と Cell B -> A の二重処理を防ぎます。
    inline static constexpr std::array<NeighborOffset, 13u> UniqueNeighborOffsets =
    {
        NeighborOffset{  1,  0,  0 },

        NeighborOffset{ -1,  1,  0 },
        NeighborOffset{  0,  1,  0 },
        NeighborOffset{  1,  1,  0 },

        NeighborOffset{ -1, -1,  1 },
        NeighborOffset{  0, -1,  1 },
        NeighborOffset{  1, -1,  1 },
        NeighborOffset{ -1,  0,  1 },
        NeighborOffset{  0,  0,  1 },
        NeighborOffset{  1,  0,  1 },
        NeighborOffset{ -1,  1,  1 },
        NeighborOffset{  0,  1,  1 },
        NeighborOffset{  1,  1,  1 }
    };

    CellCoord ComputeCellCoord(const math::Vec3& position) const;
    std::size_t HashCell(const CellCoord& cell) const;

    void EnsureBucketCapacity(std::size_t particleCount);
    void BeginBuildGeneration();
    CellBucket& GetOrActivateBucket(const CellCoord& cell);
    const CellBucket* FindActiveBucket(const CellCoord& cell) const;

    template <typename PairContainer>
    static void AppendNormalizedPair(
        uint32_t particleA,
        uint32_t particleB,
        PairContainer& outPairs)
    {
        if (particleA == particleB)
        {
            return;
        }

        ParticleSpatialHashPair pair{};
        pair.ParticleA = std::min(particleA, particleB);
        pair.ParticleB = std::max(particleA, particleB);
        outPairs.push_back(pair);
    }

private:
    float m_CellSize = 0.05f;
    float m_InverseCellSize = 20.0f;

    std::vector<CellBucket> m_Buckets;
    std::vector<std::size_t> m_ActiveBucketIndices;
    std::size_t m_BucketMask = 0u;
    uint32_t m_CurrentGeneration = 0u;
    std::size_t m_ParticleCount = 0u;
};

} // namespace ph
} // namespace Raven
