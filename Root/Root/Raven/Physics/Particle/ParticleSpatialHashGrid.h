#pragma once

#include <cstddef>
#include <cstdint>
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
// ParticleはPositionが属する1セルだけへ登録します。
// 候補生成ではOccupied Cellを基準に、同一Cell + 重複しない13方向のNeighbor Cellだけを処理します。
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

    // Occupied Cellごとに同一Cell内Pairと13方向Neighbor CellとのCross Pairを生成します。
    // Narrow Phaseの距離判定はここでは行いません。
    void GenerateCandidatePairs(std::vector<ParticleSpatialHashPair>& outPairs) const;

    std::size_t GetOccupiedCellCount() const { return m_ActiveBucketIndices.size(); }
    std::size_t GetParticleCount() const { return m_ParticleCount; }

protected:
    // SoftBodyの既存Temporary Allocation計測経路はBucketを直接走査しています。
    // 共通Core移行の第一段階では性能計測結果を変えないためprotected互換面を残します。
    // Fluid側まで共通化した後、Candidate出力をAllocator非依存Templateへまとめる予定です。
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

    void EnsureBucketCapacity(std::size_t particleCount);
    void BeginBuildGeneration();
    CellBucket& GetOrActivateBucket(const CellCoord& cell);
    const CellBucket* FindActiveBucket(const CellCoord& cell) const;

    static void AppendNormalizedPair(
        uint32_t particleA,
        uint32_t particleB,
        std::vector<ParticleSpatialHashPair>& outPairs);

protected:
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
