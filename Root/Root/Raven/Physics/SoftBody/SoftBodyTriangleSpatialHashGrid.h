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
// Spatial Hash Debug Snapshot
// ============================================================================
// Browser Debug Viewerなど、Broad Phaseの内部状態を「読み取り専用の値」として外へ渡すための型です。
// TriangleCellBucketそのものを公開するとOpen Addressing / Generation管理まで外部APIになってしまうため、
// Debug側が必要とするCell座標と登録Triangle数だけをコピーします。
struct SoftBodyTriangleSpatialHashCellDebugInfo
{
    int32_t X = 0;
    int32_t Y = 0;
    int32_t Z = 0;
    uint32_t TriangleCount = 0u;
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
    // Cell一致後はBuild時にキャッシュした情報を使い、
    // Expanded AABB -> Topology -> Triangle Plane Distance -> Edge Half-Space の順でcheap rejectします。
    // Solver側にもTopology検証は安全網として残しますが、通常はここで自己Triangleを除外します。
    void GenerateParticleTriangleCandidates(
        const std::vector<SoftBodyParticle>& particles,
        std::vector<SoftBodyParticleTrianglePair>& outPairs) const;

    // Phase ② Temporary allocation計測用Overloadです。
    // Broad Phase候補を通常Heapの中間vectorへ生成せず、Counter付きvectorへ直接格納します。
    // これによりCandidate vectorのgrow allocationもHash/Mapと同じStep統計へ含められます。
    void GenerateParticleTriangleCandidates(
        const std::vector<SoftBodyParticle>& particles,
        std::vector<
            SoftBodyParticleTrianglePair,
            SolverTemporaryAllocator<SoftBodyParticleTrianglePair>>& outPairs) const;

    std::size_t GetOccupiedCellCount() const { return m_ActiveCellCount; }

    // 現在Generationで実際に使用されているCellだけをDebug用Snapshotへコピーします。
    // outCellsは呼び出し側所有なので、通常のBroad Phase処理に追加の永続allocationや依存を持ち込みません。
    // Browser Debug Viewerは低頻度でこのAPIを呼び、Simulation hot pathから切り離して利用します。
    void CollectActiveCellDebugInfo(
        std::vector<SoftBodyTriangleSpatialHashCellDebugInfo>& outCells) const;

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

        // Candidate生成時にParticle自身を構成頂点として含むTriangleをPlane計算より先に
        // 除外できるよう、Build時のTopology Indexをそのままキャッシュします。
        uint32_t ParticleA = 0u;
        uint32_t ParticleB = 0u;
        uint32_t ParticleC = 0u;

        // Plane Normalは正規化しません。
        // Candidate生成時は
        //   dot(n, p) - d
        // の2乗と Thickness^2 * |n|^2 を比較するため、sqrt / Normalizeなしで
        // 「無限平面までの距離 > Thickness」を判定できます。
        math::Vec3 PlaneNormal{};
        float PlaneOffset = 0.0f;
        float PlaneDistanceThresholdSq = 0.0f;
        bool PlaneValid = false;

        // ====================================================================
        // Edge Half-Space Cache
        // ====================================================================
        // Triangle Plane上で各Edgeから「Triangle内部側」を向く非正規化法線を保持します。
        // 例えばAB Edgeでは cross(PlaneNormal, B - A) がC側を向きます。
        // Candidate生成時に
        //   signedScaled = dot(edgeNormal, p) - edgeOffset
        // を評価し、signedScaled < 0 かつ
        //   signedScaled^2 > Thickness^2 * |edgeNormal|^2
        // なら、ParticleはTriangleの外側Half-SpaceへThicknessより遠く離れています。
        // Triangle全体は内部Half-Space側に含まれるため、この候補はClosest Point計算前に安全にRejectできます。
        math::Vec3 EdgeABNormal{};
        math::Vec3 EdgeBCNormal{};
        math::Vec3 EdgeCANormal{};
        float EdgeABOffset = 0.0f;
        float EdgeBCOffset = 0.0f;
        float EdgeCAOffset = 0.0f;
        float EdgeABDistanceThresholdSq = 0.0f;
        float EdgeBCDistanceThresholdSq = 0.0f;
        float EdgeCADistanceThresholdSq = 0.0f;
        bool EdgeHalfSpaceValid = false;

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
    struct TriangleIndexBuffer
    {
        std::vector<uint32_t> Storage;
        std::size_t Count = 0u;

        void Reset() { Count = 0u; }

        void EnsureMinimumSize(std::size_t minimumSize)
        {
            if (Storage.size() < minimumSize)
            {
                Storage.resize(minimumSize);
            }
        }

        bool Append(uint32_t triangleIndex)
        {
            bool grew = false;
            if (Count == Storage.size())
            {
                const std::size_t newSize = Storage.empty() ? 1u : Storage.size() * 2u;
                Storage.resize(newSize);
                grew = true;
            }

            // size()ではなくCountを論理要素数として扱うため、通常経路は確保済み領域への
            // 直接書き込みだけになります。
            Storage.data()[Count] = triangleIndex;
            ++Count;
            return grew;
        }
    };

    struct TriangleCellBucket
    {
        // Hashが同じCellを区別するための正確な座標です。
        CellCoord Coord{};

        // このCellへ登録されたTriangle Indexを保持します。
        // 確保済みStorageと論理Countを分け、iteration間で確保容量を再利用します。
        TriangleIndexBuffer TriangleIndices;

        // 現在のBuild Generationと一致するBucketだけをActiveとして扱います。
        // Bucket配列を毎iterationクリアせず、Generation更新だけで再利用するための印です。
        uint32_t Generation = 0u;
    };

    // CellRegistrationの各処理は1登録の中で交互に現れるため、Scopeを直接ネストできません。
    // 一部Registrationだけを時刻計測し、Build終了時に全登録相当へ換算します。
    struct CellRegistrationTimingSample
    {
        double HashMilliseconds = 0.0;
        double ProbeMilliseconds = 0.0;
        double BucketActivateMilliseconds = 0.0;
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
    TriangleCellBucket& GetOrActivateBucket(
        const CellCoord& cell,
        CellRegistrationTimingSample* timingSample = nullptr);

    // Candidate生成用の読み取り検索です。
    const TriangleCellBucket* FindActiveBucket(const CellCoord& cell) const;

    // CellRegistrationのHot loopで集計した値をBuild終了時にProfilerへまとめて送ります。
    // これにより数万回のCell登録へTimer/Mutexを持ち込まず、ボトルネック原因だけを可視化できます。
    void SubmitCellRegistrationCounters(std::size_t validTriangleCount) const;
    void SubmitCellRegistrationTimings(double totalMilliseconds) const;

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

    // 64登録に1回だけ詳細時刻を読むことで、Hot loopへの計測汚染を抑えます。
    uint64_t m_BuildTimingSampleCount = 0u;
    double m_BuildSampledHashMilliseconds = 0.0;
    double m_BuildSampledProbeMilliseconds = 0.0;
    double m_BuildSampledBucketActivateMilliseconds = 0.0;
    double m_BuildSampledTrianglePushBackMilliseconds = 0.0;
};

} // namespace ph
} // namespace Raven
