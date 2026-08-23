#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

#include "Raven/Core/CPUProfiler.h"

namespace Raven
{
namespace ph
{
namespace
{
constexpr float MinimumCellSize = 1.0e-4f;
constexpr std::size_t MinimumBucketCount = 256u;
constexpr std::size_t InitialBucketMultiplier = 16u;
constexpr std::size_t MaximumLoadNumerator = 7u;
constexpr std::size_t MaximumLoadDenominator = 10u;

// Counter計測では1 Active Cellあたり平均約7.5 Triangleが登録されていました。
// vectorの0 -> 1 -> 2 -> 4 -> 8という段階的な再allocationを避けるため、
// 初めて使用するBucketには平均値を少し上回る8要素分を事前確保します。
// 既存capacityが8以上あるBucketではreserve()を呼ばないため、XPBD iteration間で
// これまで確保した容量をそのまま再利用できます。
constexpr std::size_t InitialTriangleIndicesCapacity = 8u;
constexpr uint64_t CellRegistrationTimingSampleInterval = 64u;

std::size_t NextPowerOfTwo(std::size_t value)
{
    std::size_t result = 1u;
    while (result < value)
    {
        result <<= 1u;
    }
    return result;
}

float Distance(const math::Vec3& a, const math::Vec3& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
}

SoftBodyTriangleSpatialHashGrid::SoftBodyTriangleSpatialHashGrid(float cellSize)
{
    SetCellSize(cellSize);
}

void SoftBodyTriangleSpatialHashGrid::SetCellSize(float cellSize)
{
    m_CellSize = std::max(cellSize, MinimumCellSize);
    m_InverseCellSize = 1.0f / m_CellSize;
    Clear();
}

void SoftBodyTriangleSpatialHashGrid::Clear()
{
    // 明示的ClearではTable全体を破棄します。
    // 通常のBuildTriangles()ではGenerationだけを更新し、Bucketと各vector capacityを再利用します。
    m_Buckets.clear();
    m_BuildBounds.clear();
    m_BuildCellRanges.clear();
    m_BucketMask = 0u;
    m_CurrentGeneration = 0u;
    m_ActiveCellCount = 0u;

    // 計測用Counterも明示的Clear時に初期状態へ戻します。
    // BuildTriangles()の各Build開始時にもリセットするため、前回Buildの統計値は持ち越しません。
    m_BuildRegistrationCount = 0u;
    m_BuildProbeCount = 0u;
    m_BuildMaxProbeCount = 0u;
    m_BuildVectorGrowCount = 0u;
    m_BuildTableGrowCount = 0u;
    m_BuildMaxCellSpanX = 0u;
    m_BuildMaxCellSpanY = 0u;
    m_BuildMaxCellSpanZ = 0u;
    m_BuildTimingSampleCount = 0u;
    m_BuildSampledHashMilliseconds = 0.0;
    m_BuildSampledProbeMilliseconds = 0.0;
    m_BuildSampledBucketActivateMilliseconds = 0.0;
    m_BuildSampledTrianglePushBackMilliseconds = 0.0;
}

void SoftBodyTriangleSpatialHashGrid::BuildTriangles(
    const std::vector<SoftBodyParticle>& particles,
    const std::vector<SoftBodyTriangle>& triangles,
    float expansion)
{
    const float safeExpansion = std::max(0.0f, expansion);
    const float safeExpansionSq = safeExpansion * safeExpansion;
    std::size_t validTriangleCount = 0u;

    // CellSizeがTriangle実寸・Collision Thicknessに対して小さすぎる場合、
    // 1 TriangleのAABBが大量のCellを跨ぎ、CellRegistration回数が急増します。
    // その関係を定量化するため、Build単位でEdge長とCell Span Volumeを集計します。
    double triangleEdgeLengthSum = 0.0;
    double maximumTriangleEdgeLength = 0.0;
    uint64_t triangleEdgeSampleCount = 0u;
    double cellSpanVolumeSum = 0.0;
    uint64_t maximumCellSpanVolume = 0u;

    // ========================================================================
    // 1. Build preparation
    // ========================================================================
    // 親Scopeは過去Captureとの比較用に残し、Generation更新・Flat Hash初期容量確認・
    // Scratch resize・Counter resetを子Scopeへ分けます。
    // Scratchはmember vectorなので、2回目以降のXPBD iterationではcapacityを再利用できます。
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.Prepare");

        {
            RAVEN_PROFILE_SCOPE(
                "SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.Prepare.Generation");

            ++m_CurrentGeneration;
            if (m_CurrentGeneration == 0u)
            {
                // Generation wrap時は古い値との一致を避けるため全BucketをInactiveへ戻します。
                for (TriangleCellBucket& bucket : m_Buckets)
                {
                    bucket.Generation = 0u;
                }
                m_CurrentGeneration = 1u;
            }

            m_ActiveCellCount = 0u;
        }

        {
            // 通常iterationでは即returnし、初回Build時だけBucket配列の確保時間を含みます。
            RAVEN_PROFILE_SCOPE(
                "SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.Prepare.BucketCapacity");
            EnsureInitialBucketCapacity(triangles.size());
        }

        {
            RAVEN_PROFILE_SCOPE(
                "SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.Prepare.ScratchResize");
            m_BuildBounds.resize(triangles.size());
            m_BuildCellRanges.resize(triangles.size());
        }

        {
            RAVEN_PROFILE_SCOPE(
                "SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.Prepare.CounterReset");

            // CellRegistrationの統計はBuild単位でリセットします。
            // Hot loop内では整数加算だけを行い、Profiler APIはBuild終了後にまとめて呼びます。
            m_BuildRegistrationCount = 0u;
            m_BuildProbeCount = 0u;
            m_BuildMaxProbeCount = 0u;
            m_BuildVectorGrowCount = 0u;
            m_BuildTableGrowCount = 0u;
            m_BuildMaxCellSpanX = 0u;
            m_BuildMaxCellSpanY = 0u;
            m_BuildMaxCellSpanZ = 0u;
            m_BuildTimingSampleCount = 0u;
            m_BuildSampledHashMilliseconds = 0.0;
            m_BuildSampledProbeMilliseconds = 0.0;
            m_BuildSampledBucketActivateMilliseconds = 0.0;
            m_BuildSampledTrianglePushBackMilliseconds = 0.0;
        }
    }

    // ========================================================================
    // 2. Triangle AABB / Plane / Edge Half-Space / Metrics
    // ========================================================================
    // 以前はAABB生成・Plane Cache・Edge Half-Space Cache・Edge Length計測を1つのloopで行っていたため、
    // HashBuild.AABBが重くても、どの処理が支配しているか判断できませんでした。
    // 今回は同じTriangle scratchを4 passで走査し、各passを独立Scopeとして計測します。
    //
    // 親のHashBuild.AABB Scopeは過去Captureとの比較用に残します。
    // 子Scopeの合計との差にはloop分割による走査コストやProfiler overheadが含まれるため、
    // 最適化対象は子Scopeの相対値とHashBuild全体の実時間を併せて判断します。
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.AABB");

        // ====================================================================
        // 2-1. Bounds
        // ====================================================================
        // Topology検証とExpansion込みAABBだけを構築します。
        // 後段Cacheの状態もここで初期化し、無効Triangleに前iterationの値が残らないようにします。
        {
            RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.AABB.Bounds");

            for (std::size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
            {
                TriangleBuildBounds& bounds = m_BuildBounds[triangleIndex];

                // Bounds passでは、このTriangleそのものが有効かどうかだけを初期化します。
                //
                // Plane / Edge Half-Space Cacheは、それぞれ後段の専用passで
                // Valid flagをfalseへ戻してから再構築します。
                // Cache値そのものはValid == falseの間は参照されないため、
                // ここでVec3やfloatをすべて0初期化する必要はありません。
                //
                // Triangle数 × Solver Iteration回数だけ発生していた不要なメモリ書き込みを
                // BoundsのHot Pathから除去することが今回の最適化目的です。
                bounds.Valid = false;

                const SoftBodyTriangle& triangle = triangles[triangleIndex];
                if (triangle.ParticleA >= particles.size()
                    || triangle.ParticleB >= particles.size()
                    || triangle.ParticleC >= particles.size())
                {
                    continue;
                }

                const math::Vec3& a = particles[triangle.ParticleA].Position;
                const math::Vec3& b = particles[triangle.ParticleB].Position;
                const math::Vec3& c = particles[triangle.ParticleC].Position;

                bounds.Minimum.x = std::min({ a.x, b.x, c.x }) - safeExpansion;
                bounds.Minimum.y = std::min({ a.y, b.y, c.y }) - safeExpansion;
                bounds.Minimum.z = std::min({ a.z, b.z, c.z }) - safeExpansion;

                bounds.Maximum.x = std::max({ a.x, b.x, c.x }) + safeExpansion;
                bounds.Maximum.y = std::max({ a.y, b.y, c.y }) + safeExpansion;
                bounds.Maximum.z = std::max({ a.z, b.z, c.z }) + safeExpansion;

                // Topology IndexはCandidate生成のcheap rejectでも再利用します。
                bounds.ParticleA = triangle.ParticleA;
                bounds.ParticleB = triangle.ParticleB;
                bounds.ParticleC = triangle.ParticleC;
                bounds.Valid = true;
                ++validTriangleCount;
            }
        }

        // ====================================================================
        // 2-2. Plane Cache
        // ====================================================================
        // Plane Distance Early Rejectに必要な非正規化法線と閾値だけを構築します。
        // Normalize / sqrtは行わず、Candidate側でも平方比較のまま使用します。
        {
            RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.AABB.PlaneCache");

            for (std::size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
            {
                TriangleBuildBounds& bounds = m_BuildBounds[triangleIndex];

                // 前iterationのPlane Cacheが残っていても使用されないよう、
                // このpassの先頭で必ず無効化してから再構築します。
                // PlaneNormalなどの値自体をゼロクリアする必要はありません。
                bounds.PlaneValid = false;

                if (bounds.Valid == false)
                {
                    continue;
                }

                const math::Vec3& a = particles[bounds.ParticleA].Position;
                const math::Vec3& b = particles[bounds.ParticleB].Position;
                const math::Vec3& c = particles[bounds.ParticleC].Position;

                const math::Vec3 edgeABVector = b - a;
                const math::Vec3 edgeACVector = c - a;
                const math::Vec3 planeNormal = math::Vec3::Cross(edgeABVector, edgeACVector);
                const float planeNormalLengthSq = planeNormal.LengthSq();
                if (planeNormalLengthSq <= math::Epsilon * math::Epsilon)
                {
                    continue;
                }

                bounds.PlaneNormal = planeNormal;
                bounds.PlaneOffset = math::Vec3::Dot(planeNormal, a);
                bounds.PlaneDistanceThresholdSq = safeExpansionSq * planeNormalLengthSq;
                bounds.PlaneValid = true;
            }
        }

        // ====================================================================
        // 2-3. Edge Half-Space Cache
        // ====================================================================
        // Plane Cacheで得た法線を再利用し、Triangle内部側を向く3辺のHalf-Spaceを構築します。
        // 前回の最適化でClosestPoint候補を大幅削減できた処理なので、Build側の追加コストを独立計測します。
        {
            RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.AABB.EdgeHalfSpaceCache");

            for (std::size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
            {
                TriangleBuildBounds& bounds = m_BuildBounds[triangleIndex];

                // Plane Cacheと同様に、前iterationのEdge Cacheを最初に無効化します。
                // Edge Normal / Offset / ThresholdはValid == falseなら参照されないため、
                // Bounds passで毎回ゼロクリアする必要はありません。
                bounds.EdgeHalfSpaceValid = false;

                if (bounds.Valid == false || bounds.PlaneValid == false)
                {
                    continue;
                }

                const math::Vec3& a = particles[bounds.ParticleA].Position;
                const math::Vec3& b = particles[bounds.ParticleB].Position;
                const math::Vec3& c = particles[bounds.ParticleC].Position;

                const math::Vec3 edgeABVector = b - a;
                const math::Vec3 edgeBCVector = c - b;
                const math::Vec3 edgeCAVector = a - c;

                const math::Vec3 edgeABNormal = math::Vec3::Cross(bounds.PlaneNormal, edgeABVector);
                const math::Vec3 edgeBCNormal = math::Vec3::Cross(bounds.PlaneNormal, edgeBCVector);
                const math::Vec3 edgeCANormal = math::Vec3::Cross(bounds.PlaneNormal, edgeCAVector);

                const float edgeABNormalLengthSq = edgeABNormal.LengthSq();
                const float edgeBCNormalLengthSq = edgeBCNormal.LengthSq();
                const float edgeCANormalLengthSq = edgeCANormal.LengthSq();

                if (edgeABNormalLengthSq <= math::Epsilon * math::Epsilon
                    || edgeBCNormalLengthSq <= math::Epsilon * math::Epsilon
                    || edgeCANormalLengthSq <= math::Epsilon * math::Epsilon)
                {
                    continue;
                }

                bounds.EdgeABNormal = edgeABNormal;
                bounds.EdgeBCNormal = edgeBCNormal;
                bounds.EdgeCANormal = edgeCANormal;
                bounds.EdgeABOffset = math::Vec3::Dot(edgeABNormal, a);
                bounds.EdgeBCOffset = math::Vec3::Dot(edgeBCNormal, b);
                bounds.EdgeCAOffset = math::Vec3::Dot(edgeCANormal, c);
                bounds.EdgeABDistanceThresholdSq = safeExpansionSq * edgeABNormalLengthSq;
                bounds.EdgeBCDistanceThresholdSq = safeExpansionSq * edgeBCNormalLengthSq;
                bounds.EdgeCADistanceThresholdSq = safeExpansionSq * edgeCANormalLengthSq;
                bounds.EdgeHalfSpaceValid = true;
            }
        }

        // ====================================================================
        // 2-4. Metrics
        // ====================================================================
        // Cell Size比較用のEdge Length統計です。Collision判定そのものには使用しません。
        // 各TriangleでDistance()を3回呼ぶためsqrtも3回発生します。
        // このScopeが大きければ、計測用Metricsを常時計算から外すことが次の直接的な最適化候補です。
        {
            RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.AABB.Metrics");

            for (std::size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
            {
                const TriangleBuildBounds& bounds = m_BuildBounds[triangleIndex];
                if (bounds.Valid == false)
                {
                    continue;
                }

                const math::Vec3& a = particles[bounds.ParticleA].Position;
                const math::Vec3& b = particles[bounds.ParticleB].Position;
                const math::Vec3& c = particles[bounds.ParticleC].Position;

                const double edgeAB = static_cast<double>(Distance(a, b));
                const double edgeBC = static_cast<double>(Distance(b, c));
                const double edgeCA = static_cast<double>(Distance(c, a));
                triangleEdgeLengthSum += edgeAB + edgeBC + edgeCA;
                maximumTriangleEdgeLength = std::max(
                    maximumTriangleEdgeLength,
                    std::max(edgeAB, std::max(edgeBC, edgeCA)));
                triangleEdgeSampleCount += 3u;
            }
        }
    }

    // ========================================================================
    // 3. Cell Range conversion
    // ========================================================================
    // AABBのmin/maxをUniform Gridの整数Cell座標へ変換します。
    // std::floorを含む座標変換コストをCell登録ループから分離して確認できます。
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.CellRange");

        for (std::size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
        {
            TriangleBuildCellRange& cellRange = m_BuildCellRanges[triangleIndex];
            cellRange.Valid = false;

            const TriangleBuildBounds& bounds = m_BuildBounds[triangleIndex];
            if (bounds.Valid == false)
            {
                continue;
            }

            cellRange.Minimum = ComputeCellCoord(bounds.Minimum);
            cellRange.Maximum = ComputeCellCoord(bounds.Maximum);
            cellRange.Valid = true;

            // Cell SpanはTriangleが何セルへ広がっているかを見る指標です。
            // ここでは最大値だけを整数演算で集計し、登録ループへ追加コストを持ち込みません。
            const uint64_t spanX = static_cast<uint64_t>(
                static_cast<int64_t>(cellRange.Maximum.X)
                - static_cast<int64_t>(cellRange.Minimum.X) + 1ll);
            const uint64_t spanY = static_cast<uint64_t>(
                static_cast<int64_t>(cellRange.Maximum.Y)
                - static_cast<int64_t>(cellRange.Minimum.Y) + 1ll);
            const uint64_t spanZ = static_cast<uint64_t>(
                static_cast<int64_t>(cellRange.Maximum.Z)
                - static_cast<int64_t>(cellRange.Minimum.Z) + 1ll);

            m_BuildMaxCellSpanX = std::max(m_BuildMaxCellSpanX, spanX);
            m_BuildMaxCellSpanY = std::max(m_BuildMaxCellSpanY, spanY);
            m_BuildMaxCellSpanZ = std::max(m_BuildMaxCellSpanZ, spanZ);

            // 実際の登録回数はspanX * spanY * spanZに比例します。
            // AverageCellsPerTriangleだけでは最大値に引っ張られた局所的な巨大AABBを判別しづらいため、
            // 平均と最大のSpan Volumeを別Counterとして記録します。
            const uint64_t spanVolume = spanX * spanY * spanZ;
            cellSpanVolumeSum += static_cast<double>(spanVolume);
            maximumCellSpanVolume = std::max(maximumCellSpanVolume, spanVolume);
        }
    }

    // ========================================================================
    // 4. Cell registration
    // ========================================================================
    // AABBが跨ぐ全Cellを列挙し、Flat HashからBucketを取得してTriangle Indexを追加します。
    // ここが大きい場合は、Hash/Linear Probe、TriangleIndices push_back、
    // AABBが跨ぐCell数そのもののどれが支配的かをCounterで切り分けます。
    // Hot loopではTimer/Mutex/文字列生成を行わず、整数Counterだけを更新します。
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.CellRegistration");
        using TimingClock = std::chrono::steady_clock;
        const bool detailedTimingEnabled = CPUProfiler::Get().IsEnabled();
        const TimingClock::time_point registrationStart = detailedTimingEnabled
            ? TimingClock::now()
            : TimingClock::time_point{};

        for (std::size_t triangleIndex = 0u; triangleIndex < triangles.size(); ++triangleIndex)
        {
            const TriangleBuildCellRange& cellRange = m_BuildCellRanges[triangleIndex];
            if (cellRange.Valid == false)
            {
                continue;
            }

            // TriangleはAABBが跨ぐ全Cellへ登録します。
            // Pass分離後もBroad Phaseの候補条件は旧実装から変更していません。
            for (int32_t z = cellRange.Minimum.Z; z <= cellRange.Maximum.Z; ++z)
            {
                for (int32_t y = cellRange.Minimum.Y; y <= cellRange.Maximum.Y; ++y)
                {
                    for (int32_t x = cellRange.Minimum.X; x <= cellRange.Maximum.X; ++x)
                    {
                        CellCoord cell{};
                        cell.X = x;
                        cell.Y = y;
                        cell.Z = z;

                        const bool sampleTiming = detailedTimingEnabled
                            && (m_BuildRegistrationCount % CellRegistrationTimingSampleInterval) == 0u;
                        CellRegistrationTimingSample timingSample{};
                        TriangleCellBucket& bucket = GetOrActivateBucket(
                            cell,
                            sampleTiming ? &timingSample : nullptr);

                        // Bucket内の確保済み配列が実際に増えた回数を数えます。
                        // これが多ければ、Flat HashではなくCell内TriangleIndicesのallocationが
                        // CellRegistration時間を支配している可能性があります。
                        const TimingClock::time_point pushBackStart = sampleTiming
                            ? TimingClock::now()
                            : TimingClock::time_point{};
                        const bool triangleIndicesGrew = bucket.TriangleIndices.Append(
                            static_cast<uint32_t>(triangleIndex));
                        if (sampleTiming)
                        {
                            const TimingClock::time_point pushBackEnd = TimingClock::now();
                            m_BuildSampledHashMilliseconds += timingSample.HashMilliseconds;
                            m_BuildSampledProbeMilliseconds += timingSample.ProbeMilliseconds;
                            m_BuildSampledBucketActivateMilliseconds +=
                                timingSample.BucketActivateMilliseconds;
                            m_BuildSampledTrianglePushBackMilliseconds +=
                                std::chrono::duration<double, std::milli>(
                                    pushBackEnd - pushBackStart).count();
                            ++m_BuildTimingSampleCount;
                        }
                        if (triangleIndicesGrew)
                        {
                            ++m_BuildVectorGrowCount;
                        }

                        ++m_BuildRegistrationCount;
                    }
                }
            }
        }

        if (detailedTimingEnabled)
        {
            const double registrationMilliseconds =
                std::chrono::duration<double, std::milli>(
                    TimingClock::now() - registrationStart).count();
            SubmitCellRegistrationTimings(registrationMilliseconds);
        }
    }

    SubmitCellRegistrationCounters(validTriangleCount);

    // CellRegistration削減へ進む前に、CellSizeとTriangle実寸・Expansionの比率を確認します。
    // ここもBuild終了後にまとめてProfilerへ送るため、Hot loopにはProfiler APIを追加しません。
    CPUProfiler& profiler = CPUProfiler::Get();
    if (profiler.IsEnabled())
    {
        const double averageTriangleEdgeLength = triangleEdgeSampleCount > 0u
            ? triangleEdgeLengthSum / static_cast<double>(triangleEdgeSampleCount)
            : 0.0;
        const double averageCellSpanVolume = validTriangleCount > 0u
            ? cellSpanVolumeSum / static_cast<double>(validTriangleCount)
            : 0.0;
        const double cellSize = static_cast<double>(m_CellSize);
        const double expansionValue = static_cast<double>(safeExpansion);
        const double expansionToCellSize = m_CellSize > 0.0f
            ? expansionValue / cellSize
            : 0.0;
        const double averageEdgeToCellSize = m_CellSize > 0.0f
            ? averageTriangleEdgeLength / cellSize
            : 0.0;
        const double maximumEdgeToCellSize = m_CellSize > 0.0f
            ? maximumTriangleEdgeLength / cellSize
            : 0.0;

        profiler.AddCounter("SoftBody.TriangleHash.CellSize", cellSize);
        profiler.AddCounter("SoftBody.TriangleHash.Expansion", expansionValue);
        profiler.AddCounter("SoftBody.TriangleHash.ExpansionToCellSize", expansionToCellSize);
        profiler.AddCounter("SoftBody.TriangleHash.AverageTriangleEdgeLength", averageTriangleEdgeLength);
        profiler.AddCounter("SoftBody.TriangleHash.MaxTriangleEdgeLength", maximumTriangleEdgeLength);
        profiler.AddCounter("SoftBody.TriangleHash.AverageEdgeToCellSize", averageEdgeToCellSize);
        profiler.AddCounter("SoftBody.TriangleHash.MaxEdgeToCellSize", maximumEdgeToCellSize);
        profiler.AddCounter("SoftBody.TriangleHash.AverageCellSpanVolume", averageCellSpanVolume);
        profiler.AddCounter("SoftBody.TriangleHash.MaxCellSpanVolume", static_cast<double>(maximumCellSpanVolume));
    }
}

void SoftBodyTriangleSpatialHashGrid::GenerateParticleTriangleCandidates(
    const std::vector<SoftBodyParticle>& particles,
    std::vector<SoftBodyParticleTrianglePair>& outPairs) const
{
    outPairs.clear();

    uint64_t cellCandidateCount = 0u;
    uint64_t expandedAABBRejectCount = 0u;
    uint64_t topologyRejectCount = 0u;
    uint64_t planeTestCount = 0u;
    uint64_t planeRejectCount = 0u;
    uint64_t edgeHalfSpaceTestCount = 0u;
    uint64_t edgeHalfSpaceRejectCount = 0u;

    for (std::size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        const uint32_t particleIndex32 = static_cast<uint32_t>(particleIndex);
        const math::Vec3& particlePosition = particles[particleIndex].Position;
        const CellCoord cell = ComputeCellCoord(particlePosition);
        const TriangleCellBucket* bucket = FindActiveBucket(cell);
        if (bucket == nullptr)
        {
            continue;
        }

        for (std::size_t bucketTriangleIndex = 0u;
             bucketTriangleIndex < bucket->TriangleIndices.Count;
             ++bucketTriangleIndex)
        {
            const uint32_t triangleIndex =
                bucket->TriangleIndices.Storage.data()[bucketTriangleIndex];
            ++cellCandidateCount;

            // ====================================================================
            // Expanded Triangle AABB Early Reject
            // ====================================================================
            // TriangleはThicknessで膨張したAABBが跨ぐ「Cell」へ登録されています。
            // そのためParticleとTriangleが同じCellにいても、Particle自体が厳密な膨張AABB外に
            // 存在するケースがあります。この候補を最近傍点計算へ送る必要はありません。
            //
            // BuildTriangles()で計算済みのm_BuildBoundsを再利用するため、ここではmin/max再計算や
            // sqrtを一切行わず6回の比較だけでRejectできます。
            // Particle-Triangle距離がThickness未満なら必ずこの膨張AABB内に存在するため、
            // 接触候補を失わずにNarrow Phaseへの流入だけを減らせます。
            if (triangleIndex >= m_BuildBounds.size())
            {
                ++expandedAABBRejectCount;
                continue;
            }

            const TriangleBuildBounds& bounds = m_BuildBounds[triangleIndex];
            if (bounds.Valid == false
                || particlePosition.x < bounds.Minimum.x
                || particlePosition.x > bounds.Maximum.x
                || particlePosition.y < bounds.Minimum.y
                || particlePosition.y > bounds.Maximum.y
                || particlePosition.z < bounds.Minimum.z
                || particlePosition.z > bounds.Maximum.z)
            {
                ++expandedAABBRejectCount;
                continue;
            }

            // ====================================================================
            // Topology Early Reject
            // ====================================================================
            // Particle自身を頂点として含むTriangleは自己衝突ではありません。
            // 前回計測ではAABB通過候補からNarrowPhaseまでの削減量が大きかったため、
            // Plane Dotよりさらに安い整数3比較を先に行い、不要なPlane計算そのものを避けます。
            if (bounds.ParticleA == particleIndex32
                || bounds.ParticleB == particleIndex32
                || bounds.ParticleC == particleIndex32)
            {
                ++topologyRejectCount;
                continue;
            }

            // ====================================================================
            // Triangle Plane Distance Early Reject
            // ====================================================================
            // Triangleまでの距離は「Triangleを含む無限平面までの距離」以上です。
            // したがって平面までの距離が既にThicknessより大きい候補は、辺・頂点を最近傍点としても
            // 絶対に接触できないためComputeClosestPointOnTriangle()へ送る必要がありません。
            //
            // PlaneNormalはBuild時に非正規化のままキャッシュしています。
            //   |dot(n,p)-d| / |n| > Thickness
            // を平方して比較することでsqrt / Normalizeを完全に避けます。
            // 退化TriangleはPlaneValid=falseとしてここではRejectせず、従来の後段判定へ残します。
            if (bounds.PlaneValid)
            {
                ++planeTestCount;

                const float signedScaledDistance =
                    math::Vec3::Dot(bounds.PlaneNormal, particlePosition) - bounds.PlaneOffset;
                const float signedScaledDistanceSq =
                    signedScaledDistance * signedScaledDistance;

                if (signedScaledDistanceSq > bounds.PlaneDistanceThresholdSq)
                {
                    ++planeRejectCount;
                    continue;
                }
            }

            // ====================================================================
            // Triangle Edge Half-Space Early Reject
            // ====================================================================
            // Plane Distanceだけでは、Triangleと同じ平面付近にあるものの辺・頂点方向へ遠い候補を
            // 除外できません。今回の計測ではこの経路が大量にClosest Pointへ流入しているため、
            // Triangle内部を表す3つのHalf-Spaceを追加のcheap rejectとして利用します。
            //
            // 各Edge NormalはBuild時にTriangle内部側へ向くようキャッシュ済みです。
            // signedScaled < 0 はEdgeの外側を意味し、そのEdge直線までの平面内距離がThicknessより大きければ、
            // Triangle全体までの距離も必ずThicknessより大きいため安全にRejectできます。
            // ここでもNormalize / sqrtは行わず、平方値だけを比較します。
            if (bounds.EdgeHalfSpaceValid)
            {
                ++edgeHalfSpaceTestCount;

                bool edgeHalfSpaceRejected = false;

                const float edgeABSignedScaled =
                    math::Vec3::Dot(bounds.EdgeABNormal, particlePosition) - bounds.EdgeABOffset;
                if (edgeABSignedScaled < 0.0f
                    && edgeABSignedScaled * edgeABSignedScaled > bounds.EdgeABDistanceThresholdSq)
                {
                    edgeHalfSpaceRejected = true;
                }

                if (edgeHalfSpaceRejected == false)
                {
                    const float edgeBCSignedScaled =
                        math::Vec3::Dot(bounds.EdgeBCNormal, particlePosition) - bounds.EdgeBCOffset;
                    if (edgeBCSignedScaled < 0.0f
                        && edgeBCSignedScaled * edgeBCSignedScaled > bounds.EdgeBCDistanceThresholdSq)
                    {
                        edgeHalfSpaceRejected = true;
                    }
                }

                if (edgeHalfSpaceRejected == false)
                {
                    const float edgeCASignedScaled =
                        math::Vec3::Dot(bounds.EdgeCANormal, particlePosition) - bounds.EdgeCAOffset;
                    if (edgeCASignedScaled < 0.0f
                        && edgeCASignedScaled * edgeCASignedScaled > bounds.EdgeCADistanceThresholdSq)
                    {
                        edgeHalfSpaceRejected = true;
                    }
                }

                if (edgeHalfSpaceRejected)
                {
                    ++edgeHalfSpaceRejectCount;
                    continue;
                }
            }

            SoftBodyParticleTrianglePair pair{};
            pair.ParticleIndex = particleIndex32;
            pair.TriangleIndex = triangleIndex;
            outPairs.push_back(pair);
        }
    }

    // Candidate funnelを段階ごとに記録します。
    // NarrowPhaseCount（Solver側）と合わせると、
    //   Cell Candidate -> Expanded AABB -> Topology -> Plane Distance -> Edge Half-Space -> Closest Point
    // のどこで候補を削減できているか確認できます。
    CPUProfiler& profiler = CPUProfiler::Get();
    if (profiler.IsEnabled())
    {
        const double candidateCount = static_cast<double>(cellCandidateCount);
        const double aabbRejectCount = static_cast<double>(expandedAABBRejectCount);
        const double aabbRejectRatio = cellCandidateCount > 0u
            ? aabbRejectCount / candidateCount
            : 0.0;

        const uint64_t aabbPassCount = cellCandidateCount - expandedAABBRejectCount;
        const double topologyRejectCountValue = static_cast<double>(topologyRejectCount);
        const double topologyRejectRatio = aabbPassCount > 0u
            ? topologyRejectCountValue / static_cast<double>(aabbPassCount)
            : 0.0;

        const double planeTestCountValue = static_cast<double>(planeTestCount);
        const double planeRejectCountValue = static_cast<double>(planeRejectCount);
        const double planeRejectRatio = planeTestCount > 0u
            ? planeRejectCountValue / planeTestCountValue
            : 0.0;

        const double edgeHalfSpaceTestCountValue = static_cast<double>(edgeHalfSpaceTestCount);
        const double edgeHalfSpaceRejectCountValue = static_cast<double>(edgeHalfSpaceRejectCount);
        const double edgeHalfSpaceRejectRatio = edgeHalfSpaceTestCount > 0u
            ? edgeHalfSpaceRejectCountValue / edgeHalfSpaceTestCountValue
            : 0.0;
        const double closestPointCandidateCount = static_cast<double>(outPairs.size());

        profiler.AddCounter("SoftBody.TriangleHash.CellCandidateCount", candidateCount);
        profiler.AddCounter("SoftBody.TriangleHash.ExpandedAABBRejectCount", aabbRejectCount);
        profiler.AddCounter("SoftBody.TriangleHash.ExpandedAABBRejectRatio", aabbRejectRatio);
        profiler.AddCounter("SoftBody.TriangleHash.TopologyRejectCount", topologyRejectCountValue);
        profiler.AddCounter("SoftBody.TriangleHash.TopologyRejectRatio", topologyRejectRatio);
        profiler.AddCounter("SoftBody.TriangleHash.PlaneTestCount", planeTestCountValue);
        profiler.AddCounter("SoftBody.TriangleHash.PlaneRejectCount", planeRejectCountValue);
        profiler.AddCounter("SoftBody.TriangleHash.PlaneRejectRatio", planeRejectRatio);
        profiler.AddCounter("SoftBody.TriangleHash.EdgeHalfSpaceTestCount", edgeHalfSpaceTestCountValue);
        profiler.AddCounter("SoftBody.TriangleHash.EdgeHalfSpaceRejectCount", edgeHalfSpaceRejectCountValue);
        profiler.AddCounter("SoftBody.TriangleHash.EdgeHalfSpaceRejectRatio", edgeHalfSpaceRejectRatio);
        profiler.AddCounter("SoftBody.TriangleHash.ClosestPointCandidateCount", closestPointCandidateCount);
    }
}

SoftBodyTriangleSpatialHashGrid::CellCoord SoftBodyTriangleSpatialHashGrid::ComputeCellCoord(
    const math::Vec3& position) const
{
    CellCoord coord{};
    coord.X = static_cast<int32_t>(std::floor(position.x * m_InverseCellSize));
    coord.Y = static_cast<int32_t>(std::floor(position.y * m_InverseCellSize));
    coord.Z = static_cast<int32_t>(std::floor(position.z * m_InverseCellSize));
    return coord;
}

std::size_t SoftBodyTriangleSpatialHashGrid::HashCell(const CellCoord& cell) const
{
    // 3軸へ異なる大きな素数を掛けてXORします。
    // Cell座標は負値も取り得るためuint32_tへbit-patternとして変換してから混合します。
    const uint64_t x = static_cast<uint32_t>(cell.X);
    const uint64_t y = static_cast<uint32_t>(cell.Y);
    const uint64_t z = static_cast<uint32_t>(cell.Z);

    uint64_t hash = x * 73856093ull;
    hash ^= y * 19349663ull;
    hash ^= z * 83492791ull;

    // 上位bitも下位bitへ混ぜ、2の累乗Maskを使った場合の偏りを抑えます。
    hash ^= hash >> 33u;
    hash *= 0xff51afd7ed558ccdull;
    hash ^= hash >> 33u;

    return static_cast<std::size_t>(hash);
}

void SoftBodyTriangleSpatialHashGrid::EnsureInitialBucketCapacity(std::size_t triangleCount)
{
    if (m_Buckets.empty() == false)
    {
        return;
    }

    // Cloth TriangleはAABB expansionによって複数Cellへ登録されます。
    // Triangle数の16倍を初期目安にし、Load Factorを低く保ってLinear Probe回数を抑えます。
    const std::size_t requestedBucketCount = std::max(
        MinimumBucketCount,
        triangleCount * InitialBucketMultiplier);

    const std::size_t bucketCount = NextPowerOfTwo(requestedBucketCount);
    m_Buckets.resize(bucketCount);
    m_BucketMask = bucketCount - 1u;
}

void SoftBodyTriangleSpatialHashGrid::GrowBuckets()
{
    // Counterは「通常iterationでTable拡張が発生していないか」を確認するためのものです。
    // Grow自体のアルゴリズムは従来と同じで、現GenerationのActive Bucketだけを移します。
    ++m_BuildTableGrowCount;

    const std::size_t oldBucketCount = m_Buckets.size();
    const std::size_t newBucketCount = std::max(
        MinimumBucketCount,
        oldBucketCount == 0u ? MinimumBucketCount : oldBucketCount * 2u);

    std::vector<TriangleCellBucket> oldBuckets = std::move(m_Buckets);
    m_Buckets.clear();
    m_Buckets.resize(newBucketCount);
    m_BucketMask = newBucketCount - 1u;

    const std::size_t previousActiveCellCount = m_ActiveCellCount;
    m_ActiveCellCount = 0u;

    // 現GenerationのBucketだけを新Tableへ移します。
    // TriangleIndicesはmoveするため、緊急Grow時でも既存vector allocationを可能な限り保持します。
    for (TriangleCellBucket& oldBucket : oldBuckets)
    {
        if (oldBucket.Generation != m_CurrentGeneration)
        {
            continue;
        }

        std::size_t bucketIndex = HashCell(oldBucket.Coord) & m_BucketMask;
        while (true)
        {
            TriangleCellBucket& newBucket = m_Buckets[bucketIndex];
            if (newBucket.Generation != m_CurrentGeneration)
            {
                newBucket.Coord = oldBucket.Coord;
                newBucket.TriangleIndices = std::move(oldBucket.TriangleIndices);
                newBucket.Generation = m_CurrentGeneration;
                ++m_ActiveCellCount;
                break;
            }

            bucketIndex = (bucketIndex + 1u) & m_BucketMask;
        }
    }

    // Grow前後でActive Cell数が変わる場合は内部不整合です。
    // Release buildでも処理継続できるよう値を復元するような分岐は行わず、
    // 正常系では必ず一致する単純な構造にしています。
    static_cast<void>(previousActiveCellCount);
}

SoftBodyTriangleSpatialHashGrid::TriangleCellBucket&
SoftBodyTriangleSpatialHashGrid::GetOrActivateBucket(
    const CellCoord& cell,
    CellRegistrationTimingSample* timingSample)
{
    using TimingClock = std::chrono::steady_clock;

    if (m_Buckets.empty())
    {
        EnsureInitialBucketCapacity(0u);
    }

    const TimingClock::time_point hashStart = timingSample != nullptr
        ? TimingClock::now()
        : TimingClock::time_point{};
    std::size_t bucketIndex = HashCell(cell) & m_BucketMask;
    if (timingSample != nullptr)
    {
        timingSample->HashMilliseconds += std::chrono::duration<double, std::milli>(
            TimingClock::now() - hashStart).count();
    }
    uint64_t probeCount = 0u;

    TimingClock::time_point probeStart = timingSample != nullptr
        ? TimingClock::now()
        : TimingClock::time_point{};

    while (true)
    {
        ++probeCount;
        TriangleCellBucket& bucket = m_Buckets[bucketIndex];

        if (bucket.Generation != m_CurrentGeneration)
        {
            if (timingSample != nullptr)
            {
                timingSample->ProbeMilliseconds += std::chrono::duration<double, std::milli>(
                    TimingClock::now() - probeStart).count();
            }
            const TimingClock::time_point activationStart = timingSample != nullptr
                ? TimingClock::now()
                : TimingClock::time_point{};
            // 実測ではCellRegistrationの約9割が既存Bucket再利用でした。
            // 以前は全RegistrationでLoad Factor判定を行っていましたが、Table容量が必要になるのは
            // 新しいActive Cellを追加するときだけです。支配的な既存Bucket命中経路から
            // 乗算・比較・分岐を外すため、Inactive Bucketを発見した段階でだけ判定します。
            if ((m_ActiveCellCount + 1u) * MaximumLoadDenominator
                > m_Buckets.size() * MaximumLoadNumerator)
            {
                // GrowBuckets()はm_Bucketsを再構築するため、現在のbucket参照はこの呼び出し後に無効です。
                // 新TableではHash位置も変わるため、必ず新しいMaskで先頭Bucketから探索し直します。
                // probeCountは実際に行った探索回数として維持し、Profiler統計にもGrow前の仕事量を含めます。
                GrowBuckets();
                const TimingClock::time_point growHashStart = timingSample != nullptr
                    ? TimingClock::now()
                    : TimingClock::time_point{};
                bucketIndex = HashCell(cell) & m_BucketMask;
                if (timingSample != nullptr)
                {
                    timingSample->HashMilliseconds += std::chrono::duration<double, std::milli>(
                        TimingClock::now() - growHashStart).count();
                    timingSample->BucketActivateMilliseconds +=
                        std::chrono::duration<double, std::milli>(
                            growHashStart - activationStart).count();
                    probeStart = TimingClock::now();
                }
                continue;
            }

            // Inactive Bucketは現在Buildでは空なので、その場で再利用できます。
            // clear()はcapacityを保持するため、同じSlotが再利用された場合は既存allocationを維持します。
            bucket.Coord = cell;
            bucket.TriangleIndices.Reset();

            // 前回計測では1 Cellあたり平均約7.5 Triangleでした。
            // capacityが不足しているBucketだけ8要素分を事前確保し、push_back()時の
            // 0 -> 1 -> 2 -> 4 -> 8という複数回の再allocationを1回へまとめます。
            // すでに8以上確保済みならreserve()を呼ばないため、iteration間の再利用を妨げません。
            bucket.TriangleIndices.EnsureMinimumSize(InitialTriangleIndicesCapacity);

            bucket.Generation = m_CurrentGeneration;
            ++m_ActiveCellCount;

            if (timingSample != nullptr)
            {
                timingSample->BucketActivateMilliseconds +=
                    std::chrono::duration<double, std::milli>(
                        TimingClock::now() - activationStart).count();
            }

            // Probe統計はGetOrActivateBucket() 1回につき実際に参照したBucket数を加算します。
            // Timerを入れず整数加算だけにすることでHot loopへの計測影響を小さくしています。
            m_BuildProbeCount += probeCount;
            m_BuildMaxProbeCount = std::max(m_BuildMaxProbeCount, probeCount);
            return bucket;
        }

        if (bucket.Coord == cell)
        {
            if (timingSample != nullptr)
            {
                timingSample->ProbeMilliseconds += std::chrono::duration<double, std::milli>(
                    TimingClock::now() - probeStart).count();
            }
            m_BuildProbeCount += probeCount;
            m_BuildMaxProbeCount = std::max(m_BuildMaxProbeCount, probeCount);
            return bucket;
        }

        bucketIndex = (bucketIndex + 1u) & m_BucketMask;
    }
}

void SoftBodyTriangleSpatialHashGrid::SubmitCellRegistrationTimings(
    double totalMilliseconds) const
{
    CPUProfiler& profiler = CPUProfiler::Get();
    if (profiler.IsEnabled() == false || m_BuildTimingSampleCount == 0u)
    {
        return;
    }

    // Sample平均を全Registration数へ換算します。RangeIterationは親時間から
    // Hash / Probe / Activate / PushBackの推定exclusive時間を引いた残差です。
    const double scale = static_cast<double>(m_BuildRegistrationCount)
        / static_cast<double>(m_BuildTimingSampleCount);
    const double hashMilliseconds = m_BuildSampledHashMilliseconds * scale;
    const double probeMilliseconds = m_BuildSampledProbeMilliseconds * scale;
    const double activateMilliseconds = m_BuildSampledBucketActivateMilliseconds * scale;
    const double pushBackMilliseconds = m_BuildSampledTrianglePushBackMilliseconds * scale;
    const double rangeIterationMilliseconds = std::max(
        0.0,
        totalMilliseconds - hashMilliseconds - probeMilliseconds
            - activateMilliseconds - pushBackMilliseconds);

    const auto addTimingResult = [&profiler](const char* name, double milliseconds)
    {
        CPUProfileResult result{};
        result.Name = name;
        result.DurationMilliseconds = milliseconds;
        result.ThreadId = std::this_thread::get_id();
        result.Depth = 0u;
        profiler.AddResult(std::move(result));
    };

    addTimingResult(
        "SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.CellRegistration.RangeIteration",
        rangeIterationMilliseconds);
    addTimingResult(
        "SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.CellRegistration.Hash",
        hashMilliseconds);
    addTimingResult(
        "SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.CellRegistration.Probe",
        probeMilliseconds);
    addTimingResult(
        "SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.CellRegistration.BucketActivate",
        activateMilliseconds);
    addTimingResult(
        "SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild.CellRegistration.TrianglePushBack",
        pushBackMilliseconds);
}

const SoftBodyTriangleSpatialHashGrid::TriangleCellBucket*
SoftBodyTriangleSpatialHashGrid::FindActiveBucket(const CellCoord& cell) const
{
    if (m_Buckets.empty())
    {
        return nullptr;
    }

    std::size_t bucketIndex = HashCell(cell) & m_BucketMask;

    while (true)
    {
        const TriangleCellBucket& bucket = m_Buckets[bucketIndex];

        // 現Generationで未使用のSlotに到達した時点で、このProbe列には対象Cellが存在しません。
        if (bucket.Generation != m_CurrentGeneration)
        {
            return nullptr;
        }

        if (bucket.Coord == cell)
        {
            return &bucket;
        }

        bucketIndex = (bucketIndex + 1u) & m_BucketMask;
    }
}

void SoftBodyTriangleSpatialHashGrid::SubmitCellRegistrationCounters(
    std::size_t validTriangleCount) const
{
    CPUProfiler& profiler = CPUProfiler::Get();
    if (profiler.IsEnabled() == false)
    {
        return;
    }

    const double registrationCount = static_cast<double>(m_BuildRegistrationCount);
    const double activeCellCount = static_cast<double>(m_ActiveCellCount);
    const double triangleCount = static_cast<double>(validTriangleCount);

    // 登録回数をTriangle数・Active Cell数で正規化することで、
    // 「TriangleがCellを跨ぎすぎている」のか「1 CellへTriangleが集中している」のかを分離して判断できます。
    const double averageCellsPerTriangle = validTriangleCount > 0u
        ? registrationCount / triangleCount
        : 0.0;
    const double averageTrianglesPerCell = m_ActiveCellCount > 0u
        ? registrationCount / activeCellCount
        : 0.0;

    // Probe平均が1.0付近ならほぼ初回Bucketで命中しており、Flat Hash探索は良好です。
    // 値が大きい場合はHash衝突やLoad Factorを次の最適化対象として検討できます。
    const double averageProbeCount = m_BuildRegistrationCount > 0u
        ? static_cast<double>(m_BuildProbeCount) / registrationCount
        : 0.0;
    const double loadFactor = m_Buckets.empty() == false
        ? activeCellCount / static_cast<double>(m_Buckets.size())
        : 0.0;

    // ========================================================================
    // Cell Registration Breakdown
    // ========================================================================
    // CellRegistrationのHot loopへTimerを追加すると数万回の計測自体がボトルネックになります。
    // そこで既に収集している整数Counterから、各登録がどの種類の仕事だったかを派生させます。
    //
    // ActiveCellCount:
    //   Build中に初めて使われたCell数なので、新規Bucket Activation回数と一致します。
    // ExistingBucketReuseCount:
    //   RegistrationCount - ActiveCellCount。既にActiveなBucketへTriangle Indexを追加した回数です。
    // ExtraProbeCount:
    //   ProbeCount - RegistrationCount。各登録は最低1 Probe必要なので、1回を超えた分だけを
    //   Hash Collision / Linear Probeの追加仕事量として数えます。
    // VectorGrowCount:
    //   TriangleIndices push_back時にcapacityが増えた回数です。
    const uint64_t bucketActivationCount = static_cast<uint64_t>(m_ActiveCellCount);
    const uint64_t existingBucketReuseCount =
        m_BuildRegistrationCount >= bucketActivationCount
            ? m_BuildRegistrationCount - bucketActivationCount
            : 0u;
    const uint64_t extraProbeCount =
        m_BuildProbeCount >= m_BuildRegistrationCount
            ? m_BuildProbeCount - m_BuildRegistrationCount
            : 0u;

    const double bucketActivationCountValue = static_cast<double>(bucketActivationCount);
    const double existingBucketReuseCountValue = static_cast<double>(existingBucketReuseCount);
    const double extraProbeCountValue = static_cast<double>(extraProbeCount);
    const double vectorGrowCountValue = static_cast<double>(m_BuildVectorGrowCount);

    const double bucketActivationRatio = m_BuildRegistrationCount > 0u
        ? bucketActivationCountValue / registrationCount
        : 0.0;
    const double existingBucketReuseRatio = m_BuildRegistrationCount > 0u
        ? existingBucketReuseCountValue / registrationCount
        : 0.0;
    const double extraProbePerRegistration = m_BuildRegistrationCount > 0u
        ? extraProbeCountValue / registrationCount
        : 0.0;
    const double vectorGrowRatio = m_BuildRegistrationCount > 0u
        ? vectorGrowCountValue / registrationCount
        : 0.0;

    // 1 Buildにつき各Counterを1回だけ登録します。
    // StatisticsPanel側では12 iteration分をTotal / Average / Maxとして集計できます。
    profiler.AddCounter("SoftBody.TriangleHash.RegistrationCount", registrationCount);
    profiler.AddCounter("SoftBody.TriangleHash.ActiveCellCount", activeCellCount);
    profiler.AddCounter("SoftBody.TriangleHash.AverageCellsPerTriangle", averageCellsPerTriangle);
    profiler.AddCounter("SoftBody.TriangleHash.AverageTrianglesPerCell", averageTrianglesPerCell);
    profiler.AddCounter("SoftBody.TriangleHash.AverageProbeCount", averageProbeCount);
    profiler.AddCounter("SoftBody.TriangleHash.MaxProbeCount", static_cast<double>(m_BuildMaxProbeCount));
    profiler.AddCounter("SoftBody.TriangleHash.VectorGrowCount", vectorGrowCountValue);
    profiler.AddCounter("SoftBody.TriangleHash.TableGrowCount", static_cast<double>(m_BuildTableGrowCount));
    profiler.AddCounter("SoftBody.TriangleHash.LoadFactor", loadFactor);
    profiler.AddCounter("SoftBody.TriangleHash.MaxCellSpanX", static_cast<double>(m_BuildMaxCellSpanX));
    profiler.AddCounter("SoftBody.TriangleHash.MaxCellSpanY", static_cast<double>(m_BuildMaxCellSpanY));
    profiler.AddCounter("SoftBody.TriangleHash.MaxCellSpanZ", static_cast<double>(m_BuildMaxCellSpanZ));

    // CellRegistrationの内訳を同じprefixへまとめ、StatisticsPanel上で隣接表示しやすくします。
    profiler.AddCounter(
        "SoftBody.TriangleHash.CellRegistration.BucketActivationCount",
        bucketActivationCountValue);
    profiler.AddCounter(
        "SoftBody.TriangleHash.CellRegistration.BucketActivationRatio",
        bucketActivationRatio);
    profiler.AddCounter(
        "SoftBody.TriangleHash.CellRegistration.ExistingBucketReuseCount",
        existingBucketReuseCountValue);
    profiler.AddCounter(
        "SoftBody.TriangleHash.CellRegistration.ExistingBucketReuseRatio",
        existingBucketReuseRatio);
    profiler.AddCounter(
        "SoftBody.TriangleHash.CellRegistration.ExtraProbeCount",
        extraProbeCountValue);
    profiler.AddCounter(
        "SoftBody.TriangleHash.CellRegistration.ExtraProbePerRegistration",
        extraProbePerRegistration);
    profiler.AddCounter(
        "SoftBody.TriangleHash.CellRegistration.VectorGrowCount",
        vectorGrowCountValue);
    profiler.AddCounter(
        "SoftBody.TriangleHash.CellRegistration.VectorGrowRatio",
        vectorGrowRatio);
    profiler.AddCounter(
        "SoftBody.TriangleHash.CellRegistration.TableGrowCount",
        static_cast<double>(m_BuildTableGrowCount));
}

} // namespace ph
} // namespace Raven
