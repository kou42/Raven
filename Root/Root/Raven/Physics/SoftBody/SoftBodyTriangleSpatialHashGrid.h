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
    // Cell一致後にBuild時のExpanded AABBとTriangle Plane情報も再利用してcheap rejectを行います。
    // Particle自身を含むTriangleなどTopology依存の除外は呼び出し側の責務です。
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
    // Hash Build Scratch Data
    // ========================================================================
    // HashBuild内部をProfilerで正確に切り分けるため、TriangleごとのAABB計算と
    // Cell Range計算を別Passへ分離します。
    // Scratch vectorはGridのmemberとして保持し、12回のXPBD iterationごとに
    // allocationし直さないようcapacityを再利用します。
    struct TriangleBuildBounds
    {
        math::Vec3 Minimum{};
        math::Vec3 Maximum{};

        // Plane Normalは正規化しません。
        // Candidate生成時は
        //   dot(n, p) - d
        // の2乗と Thickness^2 * |n|^2 を比較するため、sqrt / Normalizeなしで
        // 「無限平面までの距離 > Thickness」を判定できます。
        math::Vec3 PlaneNormal{};
        float PlaneOffset = 0.0f;
        float PlaneDistanceThresholdSq = 0.0f;
        bool PlaneValid = false;

        bool Valid = false;
    };

    struct TriangleBuildCellRange
    {
        CellCoord Minimum{};
        CellCoord Maximum{};
        bool Valid = false;
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
    // Probe回数はBuild統計へ加算し、Hot loop内ではProfiler APIを直接呼びません。
    TriangleCellBucket& GetOrActivateBucket(const CellCoord& cell);

    // Candidate生成用の読み取り検索です。
    const TriangleCellBucket* FindActiveBucket(const CellCoord& cell) const;

    // CellRegistrationのHot loopで集計した値をBuild終了時にProfilerへまとめて送ります。
    // これにより数万回のCell登録へTimer/Mutexを持ち込まず、ボトルネック原因だけを可視化できます。
    void SubmitCellRegistrationCounters(std::size_t validTriangleCount) const;

private:
    float m_CellSize = 0.05f;
    float m_InverseCellSize = 20.0f;

    std::vector<TriangleCellBucket> m_Buckets;
    std::size_t m_BucketMask = 0u;

    // BuildTriangles()をAABB / CellRange / CellRegistrationへ分離するための再利用Scratchです。
    std::vector<TriangleBuildBounds> m_BuildBounds;
    std::vector<TriangleBuildCellRange> m_BuildCellRanges;

    uint32_t m_CurrentGeneration = 0u;
    std::size_t m_ActiveCellCount = 0u;

    // ========================================================================
    // Cell Registration Counters
    // ========================================================================
    // すべてBuildTriangles() 1回分のローカル統計です。
    // XPBD iterationごとに0へ戻し、終了時にCPUProfilerへ1回ずつ記録します。
    uint64_t m_BuildRegistrationCount = 0u;
    uint64_t m_BuildProbeCount = 0u;
    uint64_t m_BuildMaxProbeCount = 0u;
    uint64_t m_BuildVectorGrowCount = 0u;
    uint64_t m_BuildTableGrowCount = 0u;
    uint64_t m_BuildMaxCellSpanX = 0u;
    uint64_t m_BuildMaxCellSpanY = 0u;
    uint64_t m_BuildMaxCellSpanZ = 0u;
};

} // namespace ph
} // namespace Raven
