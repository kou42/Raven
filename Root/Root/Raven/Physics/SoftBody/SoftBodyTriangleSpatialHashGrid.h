#pragma once

#include <cstddef>
#include <cstdint>
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
// Particle-Triangle Flat Spatial Hash / Uniform Grid
// ============================================================================
// Particle-Triangle自己衝突専用Broad Phaseです。
//
// 旧実装は unordered_map<CellCoord, vector<uint32_t>> を使用していました。
// Triangle AABBが跨ぐ全Cellに対して unordered_map のHash検索・Node参照が発生するため、
// ProfilerではBuildTriangles()がParticle-Triangle自己衝突時間の大半を占めていました。
//
// 現実装では、2の累乗サイズの連続Bucket配列 + Open Addressingを使用します。
// Bucket本体がstd::vector上に連続配置されるため、Node allocationとpointer chasingを避け、
// XPBD iteration間ではBucket内TriangleIndicesのcapacityも再利用します。
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

    std::size_t GetOccupiedCellCount() const { return m_ActiveCellCount; }

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

    // ========================================================================
    // Flat Hash Bucket
    // ========================================================================
    // Generation != m_CurrentGeneration のBucketは「現在Buildでは空」として扱います。
    // Bucketを物理的に消去しないため、TriangleIndicesのcapacityを次iterationへ再利用できます。
    struct TriangleCellBucket
    {
        CellCoord Coord{};
        std::vector<uint32_t> TriangleIndices;
        uint32_t Generation = 0u;
    };

    CellCoord ComputeCellCoord(const math::Vec3& position) const;
    std::size_t HashCell(const CellCoord& cell) const;

    // Triangle数から初期Bucket容量を確保します。
    // Open Addressingは高Load FactorでProbeが急増するため、余裕を持った容量を使用します。
    void EnsureInitialBucketCapacity(std::size_t triangleCount);

    // 現GenerationのActive Bucketを保ったままTableを拡張します。
    // 通常のClothでは初期容量で足りる想定ですが、極端に広いTriangle AABBでも破綻しないための安全網です。
    void GrowBuckets();

    // 現Buildで指定Cellを取得します。存在しなければ最初のInactive Bucketを再利用します。
    TriangleCellBucket& GetOrActivateBucket(const CellCoord& cell);

    // Candidate生成用の読み取り検索です。
    const TriangleCellBucket* FindActiveBucket(const CellCoord& cell) const;

private:
    float m_CellSize = 0.05f;
    float m_InverseCellSize = 20.0f;

    std::vector<TriangleCellBucket> m_Buckets;
    std::size_t m_BucketMask = 0u;

    uint32_t m_CurrentGeneration = 0u;
    std::size_t m_ActiveCellCount = 0u;
};

} // namespace ph
} // namespace Raven
