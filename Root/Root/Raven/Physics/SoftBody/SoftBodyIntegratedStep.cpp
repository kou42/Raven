#include "Raven/Physics/SoftBody/SoftBodySolver.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Solver/SolverTemporaryAllocationCounter.h"
#include "Raven/Physics/SoftBody/SoftBodyCloth.h"
#include "Raven/Physics/SoftBody/SoftBodyParticleTriangleSelfCollision.h"
#include "Raven/Physics/SoftBody/SoftBodySelfCollision.h"
#include "Raven/Physics/SoftBody/SoftBodySpatialHashGrid.h"
#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"

namespace Raven
{
namespace ph
{
namespace
{

// ============================================================================
// SoftBody Solver Temporary Container Types
// ============================================================================
// StepWithSelfCollisions()内でStep寿命しか持たないコンテナを同じCounterへ接続します。
// Phase ②ではBacking Allocatorを指定しないため、実際の確保元は従来どおり通常Heapです。
// Phase ③では同じ型のままFrameAllocatorを渡し、計測方法を変えずにBefore / After比較します。
//
// unordered_* はNode/Bucket用の内部型へAllocatorをrebindします。
// Candidate vectorはSpatialHashからCounter付きvectorへ直接push_backし、中間copyを作りません。
using TemporaryExcludedPairAllocator = SolverTemporaryAllocator<uint64_t>;
using TemporaryExcludedPairSet = std::unordered_set<
    uint64_t,
    std::hash<uint64_t>,
    std::equal_to<uint64_t>,
    TemporaryExcludedPairAllocator>;

using TemporaryLambdaValue = std::pair<const uint64_t, float>;
using TemporaryLambdaAllocator = SolverTemporaryAllocator<TemporaryLambdaValue>;
using TemporaryLambdaMap = std::unordered_map<
    uint64_t,
    float,
    std::hash<uint64_t>,
    std::equal_to<uint64_t>,
    TemporaryLambdaAllocator>;

using TemporaryParticleCandidateAllocator = SolverTemporaryAllocator<SoftBodySpatialHashPair>;
using TemporaryParticleCandidateVector = std::vector<
    SoftBodySpatialHashPair,
    TemporaryParticleCandidateAllocator>;

using TemporaryParticleTriangleCandidateAllocator =
    SolverTemporaryAllocator<SoftBodyParticleTrianglePair>;
using TemporaryParticleTriangleCandidateVector = std::vector<
    SoftBodyParticleTrianglePair,
    TemporaryParticleTriangleCandidateAllocator>;

// ============================================================================
// Standard Triangle Vector Allocation Tracker
// ============================================================================
// trianglesはBuildTriangles()が現時点でstd::vector<SoftBodyTriangle>を受け取るため、Phase ②では
// vector型自体を変更しません。ただしBuildClothTriangles()は空vectorに対して必要最大数を1回reserveし、
// その後はその範囲内へpush_backするため、Step中のHeap allocationはreserveによる1回だけです。
//
// reserve後の実capacityを使って要求bytesを記録し、scope終了時に同じbytesをdeallocationとして記録します。
// Phase ③ではBuildTriangles側を連続配列View対応へ変更し、この特殊計測を外してFrameAllocatorへ直接載せます。
class ScopedTriangleVectorAllocationTracker
{
public:
    explicit ScopedTriangleVectorAllocationTracker(
        SolverTemporaryAllocationStatistics& statistics)
        : m_Statistics(&statistics)
    {
    }

    ScopedTriangleVectorAllocationTracker(const ScopedTriangleVectorAllocationTracker&) = delete;
    ScopedTriangleVectorAllocationTracker& operator=(const ScopedTriangleVectorAllocationTracker&) = delete;

    ~ScopedTriangleVectorAllocationTracker()
    {
        if (m_Statistics != nullptr && m_AllocatedBytes > 0u)
        {
            m_Statistics->RecordDeallocation(m_AllocatedBytes);
        }
    }

    void RecordReservedCapacity(std::size_t capacity)
    {
        if (m_Statistics == nullptr || m_AllocatedBytes > 0u || capacity == 0u)
        {
            return;
        }

        m_AllocatedBytes = capacity * sizeof(SoftBodyTriangle);
        m_Statistics->RecordAllocation(m_AllocatedBytes);
    }

private:
    SolverTemporaryAllocationStatistics* m_Statistics = nullptr;
    std::size_t m_AllocatedBytes = 0u;
};

uint64_t MakeParticlePairKey(uint32_t particleA, uint32_t particleB)
{
    const uint32_t lower = std::min(particleA, particleB);
    const uint32_t upper = std::max(particleA, particleB);
    return (static_cast<uint64_t>(lower) << 32u) | static_cast<uint64_t>(upper);
}

uint64_t MakeParticleTriangleKey(uint32_t particleIndex, uint32_t triangleIndex)
{
    return (static_cast<uint64_t>(particleIndex) << 32u)
        | static_cast<uint64_t>(triangleIndex);
}

template<typename ExcludedPairSet>
void BuildExcludedParticlePairs(
    const std::vector<XPBDDistanceConstraint>& constraints,
    ExcludedPairSet& outExcludedPairs)
{
    outExcludedPairs.clear();
    outExcludedPairs.reserve(constraints.size());

    for (const XPBDDistanceConstraint& constraint : constraints)
    {
        outExcludedPairs.insert(MakeParticlePairKey(
            constraint.ParticleA,
            constraint.ParticleB));
    }
}

void BuildClothTriangles(
    const SoftBodyCloth& cloth,
    std::vector<SoftBodyTriangle>& outTriangles,
    ScopedTriangleVectorAllocationTracker& allocationTracker)
{
    outTriangles.clear();
    outTriangles.reserve(
        static_cast<std::size_t>(cloth.Rows)
        * static_cast<std::size_t>(cloth.Columns)
        * 2u);

    // 空vectorから1回だけreserveした直後の実capacityを記録します。
    // 以降のpush_back数はRows * Columns * 2以下なので、このStep中に追加growは発生しません。
    allocationTracker.RecordReservedCapacity(outTriangles.capacity());

    for (uint32_t row = 0u; row < cloth.Rows; ++row)
    {
        for (uint32_t column = 0u; column < cloth.Columns; ++column)
        {
            const uint32_t topLeft = cloth.GetParticleIndex(row, column);
            const uint32_t topRight = cloth.GetParticleIndex(row, column + 1u);
            const uint32_t bottomLeft = cloth.GetParticleIndex(row + 1u, column);
            const uint32_t bottomRight = cloth.GetParticleIndex(row + 1u, column + 1u);

            // Renderer / SoftBodyClothBuilder と同じ固定Topologyです。
            outTriangles.push_back({ topLeft, bottomLeft, topRight });
            outTriangles.push_back({ bottomLeft, bottomRight, topRight });
        }
    }
}

bool TriangleContainsParticle(const SoftBodyTriangle& triangle, uint32_t particleIndex)
{
    return triangle.ParticleA == particleIndex
        || triangle.ParticleB == particleIndex
        || triangle.ParticleC == particleIndex;
}

struct ClosestPointResult
{
    math::Vec3 Point{};
    float WeightA = 0.0f;
    float WeightB = 0.0f;
    float WeightC = 0.0f;
};

ClosestPointResult ComputeClosestPointOnTriangle(
    const math::Vec3& point,
    const math::Vec3& a,
    const math::Vec3& b,
    const math::Vec3& c)
{
    const math::Vec3 ab = b - a;
    const math::Vec3 ac = c - a;
    const math::Vec3 ap = point - a;

    // ========================================================================
    // EdgeAC Fast Path
    // ========================================================================
    // 実測でEdgeAC / EdgeBCが最近傍Regionの大半を占めることを確認できたため、
    // この判定順を検証用分岐ではなく正式なHot Pathとして採用します。
    // EdgeACはd1 / d2 / d5 / d6だけで判定できるため、まず4 Dotで早期returnを試します。
    //
    // Ericson本来の <= / >= ではなく厳密不等号を使う点は維持します。
    // Vertex A/CやRegion境界上の等号ケースをFast Pathで奪わず従来判定へfallbackさせることで、
    // 境界ケースのClosest PointとBarycentric Weightを従来実装と一致させます。
    const float d1 = math::Vec3::Dot(ab, ap);
    const float d2 = math::Vec3::Dot(ac, ap);

    const math::Vec3 cp = point - c;
    const float d5 = math::Vec3::Dot(ab, cp);
    const float d6 = math::Vec3::Dot(ac, cp);

    const float vb = d5 * d2 - d1 * d6;
    if (vb < 0.0f && d2 > 0.0f && d6 < 0.0f)
    {
        const float w = d2 / (d2 - d6);
        return { a + ac * w, 1.0f - w, 0.0f, w };
    }

    // ========================================================================
    // EdgeBC Fast Path
    // ========================================================================
    // EdgeACでreturnしなかった場合だけB側の2 Dotを追加します。
    // EdgeBCが支配的なケースでは、従来のVertexA -> VertexB -> EdgeAB -> VertexC -> EdgeACという
    // 複数branchを通過せずにここでreturnできます。
    const math::Vec3 bp = point - b;
    const float d3 = math::Vec3::Dot(ab, bp);
    const float d4 = math::Vec3::Dot(ac, bp);

    const float va = d3 * d6 - d5 * d4;
    const float edgeBCFromB = d4 - d3;
    const float edgeBCFromC = d5 - d6;
    if (va < 0.0f && edgeBCFromB > 0.0f && edgeBCFromC > 0.0f)
    {
        const float w = edgeBCFromB / (edgeBCFromB + edgeBCFromC);
        const math::Vec3 bc = c - b;
        return { b + bc * w, 0.0f, 1.0f - w, w };
    }

    // ========================================================================
    // Ericson Fallback
    // ========================================================================
    // Fast Pathは厳密なEdge内部だけを扱います。
    // 頂点・境界・EdgeAB・Face・退化Triangleは従来の判定順を維持します。
    if (d1 <= 0.0f && d2 <= 0.0f)
    {
        return { a, 1.0f, 0.0f, 0.0f };
    }

    if (d3 >= 0.0f && d4 <= d3)
    {
        return { b, 0.0f, 1.0f, 0.0f };
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        const float v = d1 / (d1 - d3);
        return { a + ab * v, 1.0f - v, v, 0.0f };
    }

    if (d6 >= 0.0f && d5 <= d6)
    {
        return { c, 0.0f, 0.0f, 1.0f };
    }

    // Fast Pathは境界を除外しているため、等号を含むEdgeAC / EdgeBC条件はfallback側にも残します。
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        const float w = d2 / (d2 - d6);
        return { a + ac * w, 1.0f - w, 0.0f, w };
    }

    if (va <= 0.0f && edgeBCFromB >= 0.0f && edgeBCFromC >= 0.0f)
    {
        const math::Vec3 bc = c - b;
        const float w = edgeBCFromB / (edgeBCFromB + edgeBCFromC);
        return { b + bc * w, 0.0f, 1.0f - w, w };
    }

    const float denominator = va + vb + vc;
    if (std::abs(denominator) <= math::Epsilon)
    {
        // 退化Triangleでは従来どおりAを安全なfallbackとして返します。
        return { a, 1.0f, 0.0f, 0.0f };
    }

    const float inverseDenominator = 1.0f / denominator;
    const float v = vb * inverseDenominator;
    const float w = vc * inverseDenominator;
    const float u = 1.0f - v - w;
    return { a * u + b * v + c * w, u, v, w };
}

template<typename ExcludedPairSet, typename LambdaMap, typename CandidatePairVector>
void SolveParticleSelfCollisionIteration(
    std::vector<SoftBodyParticle>& particles,
    SoftBodySpatialHashGrid& spatialHash,
    const ExcludedPairSet& excludedPairs,
    LambdaMap& lambdas,
    CandidatePairVector& candidatePairs,
    float targetDistance,
    float alphaTilde)
{
    // Particle-Particle自己衝突はBroad PhaseとNarrow Phaseの両方が重くなり得ます。
    // それぞれを個別Scopeへ分け、Spatial Hash自体が原因なのか候補解決側が原因なのかを判別します。
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleSelfCollision.HashBuild");
        spatialHash.Build(particles);
    }

    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleSelfCollision.CandidateGeneration");
        spatialHash.GenerateCandidatePairs(candidatePairs);
    }

    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleSelfCollision.NarrowPhase");

        for (const SoftBodySpatialHashPair& pair : candidatePairs)
        {
            if (pair.ParticleA >= particles.size() || pair.ParticleB >= particles.size())
            {
                continue;
            }

            const uint64_t pairKey = MakeParticlePairKey(pair.ParticleA, pair.ParticleB);
            if (excludedPairs.find(pairKey) != excludedPairs.end())
            {
                continue;
            }

            SoftBodyParticle& particleA = particles[pair.ParticleA];
            SoftBodyParticle& particleB = particles[pair.ParticleB];
            const float inverseMassSum = particleA.InverseMass + particleB.InverseMass;
            if (inverseMassSum <= 0.0f)
            {
                continue;
            }

            const math::Vec3 delta = particleB.Position - particleA.Position;
            const float distanceSq = delta.LengthSq();

            float distance = 0.0f;
            math::Vec3 normal{ 1.0f, 0.0f, 0.0f };
            if (distanceSq > math::Epsilon * math::Epsilon)
            {
                distance = std::sqrt(distanceSq);
                normal = delta / distance;
            }
            else
            {
                const math::Vec3 previousDelta =
                    particleB.PreviousPosition - particleA.PreviousPosition;
                const float previousDistanceSq = previousDelta.LengthSq();
                if (previousDistanceSq > math::Epsilon * math::Epsilon)
                {
                    normal = previousDelta / std::sqrt(previousDistanceSq);
                }
            }

            const float constraintValue = distance - targetDistance;
            float& lambda = lambdas[pairKey];
            if (constraintValue >= 0.0f && lambda <= 0.0f)
            {
                continue;
            }

            const float denominator = inverseMassSum + alphaTilde;
            if (denominator <= math::Epsilon)
            {
                continue;
            }

            const float unconstrainedDeltaLambda =
                (-constraintValue - alphaTilde * lambda) / denominator;
            const float oldLambda = lambda;
            const float newLambda = std::max(0.0f, oldLambda + unconstrainedDeltaLambda);
            const float appliedDeltaLambda = newLambda - oldLambda;
            lambda = newLambda;

            if (std::abs(appliedDeltaLambda) <= math::Epsilon)
            {
                continue;
            }

            if (particleA.IsFixed() == false)
            {
                particleA.Position -= normal * (particleA.InverseMass * appliedDeltaLambda);
            }

            if (particleB.IsFixed() == false)
            {
                particleB.Position += normal * (particleB.InverseMass * appliedDeltaLambda);
            }
        }
    }
}

template<typename LambdaMap, typename CandidatePairVector>
void SolveParticleTriangleSelfCollisionIteration(
    std::vector<SoftBodyParticle>& particles,
    const std::vector<SoftBodyTriangle>& triangles,
    SoftBodyTriangleSpatialHashGrid& spatialHash,
    LambdaMap& lambdas,
    CandidatePairVector& candidatePairs,
    SoftBodyParticleTriangleCollisionStatistics& statistics,
    float thickness,
    float alphaTilde)
{
    // Particle-Triangleは現在もっとも大きなボトネック候補です。
    // TriangleのGrid登録、Particleからの候補収集、実Triangle距離計算の3段階へ明確に分離します。
    // この結果を使って、次にGrid更新を最適化するか、Candidate/Narrow Phaseを並列化するか判断します。
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.HashBuild");
        spatialHash.BuildTriangles(particles, triangles, thickness);
    }

    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.CandidateGeneration");
        spatialHash.GenerateParticleTriangleCandidates(particles, candidatePairs);

        // CandidateCountはSpatial Hashが返した直後に加算します。
        // Topology除外前の値を保持することで、Cell Size変更そのものがBroad Phase候補数へ
        // 与える影響を公平に比較できます。
        statistics.CandidateCount += static_cast<uint64_t>(candidatePairs.size());
    }

    const float thicknessSq = thickness * thickness;

    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision.NarrowPhase");

        for (const SoftBodyParticleTrianglePair& pair : candidatePairs)
        {
            if (pair.ParticleIndex >= particles.size() || pair.TriangleIndex >= triangles.size())
            {
                continue;
            }

            const SoftBodyTriangle& triangle = triangles[pair.TriangleIndex];
            if (TriangleContainsParticle(triangle, pair.ParticleIndex))
            {
                continue;
            }

            if (triangle.ParticleA >= particles.size()
                || triangle.ParticleB >= particles.size()
                || triangle.ParticleC >= particles.size())
            {
                continue;
            }

            // ここから先は最近傍点、距離、法線、XPBD Constraintを計算する実Narrow Phaseです。
            // cheap rejectを通過した回数だけを数えることで、CandidateCountとの差から
            // Broad Phase後にどれだけ不要候補を除外できているか確認できます。
            ++statistics.NarrowPhaseCount;

            SoftBodyParticle& particle = particles[pair.ParticleIndex];
            SoftBodyParticle& particleA = particles[triangle.ParticleA];
            SoftBodyParticle& particleB = particles[triangle.ParticleB];
            SoftBodyParticle& particleC = particles[triangle.ParticleC];

            const ClosestPointResult closest = ComputeClosestPointOnTriangle(
                particle.Position,
                particleA.Position,
                particleB.Position,
                particleC.Position);

            const math::Vec3 delta = particle.Position - closest.Point;
            const float distanceSq = delta.LengthSq();

            // ====================================================================
            // Distance Squared Fast Reject
            // ====================================================================
            // funnel計測ではNarrow Phase候補の大半がDistance計算後のthickness判定で除外されていました。
            // 非接触候補に対してsqrtとCollision Normal構築まで行う必要はないため、
            // distanceSqとthicknessSqだけで先に判定し、遠方候補を安価に除外します。
            //
            // ただしXPBDの片側Constraintでは、前iterationで正のLambdaを持った候補が接触範囲外へ
            // 移動した場合もLambdaを減少・解放する必要があります。既存Lambda > 0の候補はFast Rejectせず、
            // 従来どおりConstraint計算まで通します。
            //
            // また、Fast Rejectされる候補ではunordered_map::operator[]を呼ばないことで、
            // 0 Lambdaの不要エントリ生成とHash Map拡張コストも避けます。
            const uint64_t pairKey = MakeParticleTriangleKey(pair.ParticleIndex, pair.TriangleIndex);
            const auto lambdaIterator = lambdas.find(pairKey);
            const bool hasActiveLambda =
                lambdaIterator != lambdas.end() && lambdaIterator->second > 0.0f;
            if (distanceSq >= thicknessSq && hasActiveLambda == false)
            {
                continue;
            }

            float distance = 0.0f;
            math::Vec3 normal{ 0.0f, 0.0f, 1.0f };
            if (distanceSq > math::Epsilon * math::Epsilon)
            {
                distance = std::sqrt(distanceSq);
                normal = delta / distance;
            }
            else
            {
                const math::Vec3 triangleNormal = math::Vec3::Cross(
                    particleB.Position - particleA.Position,
                    particleC.Position - particleA.Position);
                const float triangleNormalLengthSq = triangleNormal.LengthSq();
                if (triangleNormalLengthSq > math::Epsilon * math::Epsilon)
                {
                    normal = triangleNormal / std::sqrt(triangleNormalLengthSq);
                    const float previousSide = math::Vec3::Dot(
                        particle.PreviousPosition - closest.Point,
                        normal);
                    if (previousSide < 0.0f)
                    {
                        normal *= -1.0f;
                    }
                }
            }

            // Fast Rejectを通過し、sqrtを含む距離・法線構築まで実行した候補だけを数えます。
            // NarrowPhaseCountとの差がDistance Squared Fast Rejectで除外できた仕事量になります。
            ++statistics.DistanceCount;

            const float constraintValue = distance - thickness;
            float& lambda =
                lambdaIterator != lambdas.end()
                    ? lambdaIterator->second
                    : lambdas[pairKey];
            if (constraintValue >= 0.0f && lambda <= 0.0f)
            {
                continue;
            }

            // thickness判定を通過し、実際にXPBD Constraint計算へ進む候補だけを数えます。
            ++statistics.ConstraintCount;

            const float denominator =
                particle.InverseMass
                + particleA.InverseMass * closest.WeightA * closest.WeightA
                + particleB.InverseMass * closest.WeightB * closest.WeightB
                + particleC.InverseMass * closest.WeightC * closest.WeightC
                + alphaTilde;
            if (denominator <= math::Epsilon)
            {
                // Constraintへ到達したものの、DeltaLambda計算を安全に行えない候補です。
                // このRejectを独立計測することで、ConstraintCountとPositionCorrectionCountの差を
                // 「分母無効」と「DeltaLambdaが実質0」の2種類へ分解できます。
                ++statistics.DenominatorRejectCount;
                continue;
            }

            // 有効なdenominatorを得て、ここからXPBDのLambda更新を実際に計算します。
            // ConstraintCount - DenominatorRejectCount と一致することが期待されます。
            ++statistics.DeltaLambdaCount;

            const float unconstrainedDeltaLambda =
                (-constraintValue - alphaTilde * lambda) / denominator;
            const float oldLambda = lambda;
            const float newLambda = std::max(0.0f, oldLambda + unconstrainedDeltaLambda);
            const float appliedDeltaLambda = newLambda - oldLambda;
            lambda = newLambda;

            if (std::abs(appliedDeltaLambda) <= math::Epsilon)
            {
                // XPBD式までは評価したものの、実際の位置補正へ寄与しない候補です。
                // この割合が大きければ、将来的にLambda更新前のcheap rejectを検討する価値があります。
                ++statistics.DeltaLambdaRejectCount;
                continue;
            }

            bool positionCorrected = false;
            if (particle.IsFixed() == false)
            {
                particle.Position += normal * (particle.InverseMass * appliedDeltaLambda);
                positionCorrected = true;
            }

            if (particleA.IsFixed() == false)
            {
                particleA.Position -= normal
                    * (particleA.InverseMass * closest.WeightA * appliedDeltaLambda);
                positionCorrected = true;
            }

            if (particleB.IsFixed() == false)
            {
                particleB.Position -= normal
                    * (particleB.InverseMass * closest.WeightB * appliedDeltaLambda);
                positionCorrected = true;
            }

            if (particleC.IsFixed() == false)
            {
                particleC.Position -= normal
                    * (particleC.InverseMass * closest.WeightC * appliedDeltaLambda);
                positionCorrected = true;
            }

            // DeltaLambda計算だけでなく、少なくとも1 Particleへ実Position補正が入った回数です。
            if (positionCorrected)
            {
                ++statistics.PositionCorrectionCount;
            }
        }
    }
}

} // namespace

void SoftBodySolver::StepWithSelfCollisions(
    float deltaTime,
    const SoftBodyCloth& cloth,
    const SoftBodySelfCollisionSettings& particleSettings,
    const SoftBodyParticleTriangleSelfCollisionSettings& particleTriangleSettings)
{

    // ========================================================================
    // TEMP: SoftBody simulation disabled
    // ========================================================================
    // SoftBody最適化を一時中断し、他システムの計測・実装へ集中するため、
    // Solver処理全体を入口で停止します。
    //
    // このreturnにより以下はすべて実行されません。
    // - PredictPositions
    // - Distance / Dihedral Constraints
    // - Particle-Particle Self Collision
    // - Particle-Triangle Self Collision
    // - Sphere / Plane Collision
    // - UpdateVelocities
    //
    // SoftBody作業を再開するときは、このreturnだけを削除すれば元に戻せます。

    // SoftBodyを停止中でもDebug / Profiler表示へ古い自己衝突統計・Temporary Allocation統計を
    // 残さないため、Statisticsは入口で必ず初期化します。
    m_ParticleTriangleCollisionStatistics.Reset();
    m_TemporaryAllocationStatistics.Reset();

    // ========================================================================
    // TEMP: SoftBody simulation disabled
    // ========================================================================
    // SoftBody最適化を一時中断している間、Simulation処理全体をここで停止します。
    // 再開時はこのreturnだけを削除してください。
    return;

    RAVEN_PROFILE_SCOPE("SoftBody.Solver.StepWithSelfCollisions");

    // Statisticsは「直近1 Step」の値として扱います。
    // deltaTime不正や自己衝突無効時も古い値を残さないよう、Early Returnより前に必ずResetします。
    m_ParticleTriangleCollisionStatistics.Reset();
    m_TemporaryAllocationStatistics.Reset();

    if (deltaTime <= 0.0f)
    {
        return;
    }

    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.PredictPositions");
        PredictPositions(deltaTime);
    }

    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.ResetConstraints");
        ResetConstraintLambdas();
        ResetCollisionConstraintState();
    }

    // Self Collision Lambdaも通常のXPBD Constraintと同じく同一Step内のiteration間で保持します。
    // 以前の後処理passではSelf Collision側だけ独立反復していましたが、ここでは全Constraintを
    // 1つのSolverIterationsへ統一するため、Complianceを含むLambda蓄積も正しく連続します。
    const float particleRadius = std::max(0.0f, particleSettings.ParticleRadius);
    const float particleTargetDistance = particleRadius * 2.0f;
    const bool particleCollisionEnabled =
        particleSettings.Enabled && particleTargetDistance > math::Epsilon;

    const float triangleThickness = std::max(0.0f, particleTriangleSettings.Thickness);
    const bool particleTriangleCollisionEnabled =
        particleTriangleSettings.Enabled
        && triangleThickness > math::Epsilon
        && cloth.Rows > 0u
        && cloth.Columns > 0u;

    // ========================================================================
    // Phase ②: Step Local Temporary Allocation Counter
    // ========================================================================
    // Step寿命のHash/Map/Candidate vectorを同一Statisticsへ集約します。
    // Backing Allocatorはまだ指定しないため通常Heapを使用し、ここで得る値がBeforeです。
    TemporaryExcludedPairSet excludedParticlePairs{
        0u,
        std::hash<uint64_t>{},
        std::equal_to<uint64_t>{},
        TemporaryExcludedPairAllocator(&m_TemporaryAllocationStatistics) };

    TemporaryLambdaMap particleLambdas{
        0u,
        std::hash<uint64_t>{},
        std::equal_to<uint64_t>{},
        TemporaryLambdaAllocator(&m_TemporaryAllocationStatistics) };

    TemporaryLambdaMap particleTriangleLambdas{
        0u,
        std::hash<uint64_t>{},
        std::equal_to<uint64_t>{},
        TemporaryLambdaAllocator(&m_TemporaryAllocationStatistics) };

    TemporaryParticleCandidateVector particleCandidatePairs{
        TemporaryParticleCandidateAllocator(&m_TemporaryAllocationStatistics) };

    TemporaryParticleTriangleCandidateVector particleTriangleCandidatePairs{
        TemporaryParticleTriangleCandidateAllocator(&m_TemporaryAllocationStatistics) };

    // Triangle topology vectorだけはBuildTriangles APIとの互換性のためPhase ②では通常vectorを維持します。
    // allocation自体は下のRAII Trackerで同じStatisticsへ正確に加算します。
    std::vector<SoftBodyTriangle> triangles;
    ScopedTriangleVectorAllocationTracker triangleAllocationTracker(m_TemporaryAllocationStatistics);

    SoftBodySpatialHashGrid particleSpatialHash(
        std::max(particleTargetDistance, 1.0e-4f));

    const float triangleCellSize =
        std::max(particleTriangleSettings.SpatialHashCellSize, 1.0e-4f);

    // GridをSolver memberとしてframe間で再利用します。
    // SetCellSize()はBucket / ScratchをClearするため、同じ値では呼ばないことが重要です。
    if (m_ParticleTriangleSpatialHash.GetCellSize() != triangleCellSize)
    {
        m_ParticleTriangleSpatialHash.SetCellSize(triangleCellSize);
    }

    {
        // 自己衝突用Topologyと除外ペアの構築コストをSolver反復とは分離します。
        // ここが大きい場合は毎Step再構築せずTopology変更時だけCacheする最適化候補になります。
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.CollisionSetup");

        if (particleCollisionEnabled)
        {
            BuildExcludedParticlePairs(m_DistanceConstraints, excludedParticlePairs);
        }

        if (particleTriangleCollisionEnabled)
        {
            BuildClothTriangles(cloth, triangles, triangleAllocationTracker);
        }
    }

    const float particleAlphaTilde =
        std::max(0.0f, particleSettings.Compliance) / (deltaTime * deltaTime);
    const float particleTriangleAlphaTilde =
        std::max(0.0f, particleTriangleSettings.Compliance) / (deltaTime * deltaTime);

    const uint32_t iterationCount = std::max(1u, m_Settings.SolverIterations);
    for (uint32_t iteration = 0u; iteration < iterationCount; ++iteration)
    {
        // ====================================================================
        // Unified XPBD Iteration Order
        // ====================================================================
        // Internal shape -> self collision -> external collision の順に解きます。
        // 次iterationではSphere/Planeによる押し戻しもInternal ConstraintとSelf Collisionが再評価するため、
        // 後処理方式より折り畳み・外部Collider接触が同時発生した場合に収束しやすくなります。
        {
            RAVEN_PROFILE_SCOPE("SoftBody.Solver.DistanceConstraints");
            SolveDistanceConstraints(deltaTime);
        }

        {
            RAVEN_PROFILE_SCOPE("SoftBody.Solver.DihedralConstraints");
            SolveDihedralConstraints(deltaTime);
        }

        if (particleCollisionEnabled)
        {
            // 子ScopeでHash構築・候補生成・Narrow Phaseまで分解して記録します。
            RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleSelfCollision");
            SolveParticleSelfCollisionIteration(
                m_Particles,
                particleSpatialHash,
                excludedParticlePairs,
                particleLambdas,
                particleCandidatePairs,
                particleTargetDistance,
                particleAlphaTilde);
        }

        if (particleTriangleCollisionEnabled && triangles.empty() == false)
        {
            // 子ScopeでTriangle Hash構築・候補生成・Narrow Phaseまで分解して記録します。
            RAVEN_PROFILE_SCOPE("SoftBody.Solver.ParticleTriangleSelfCollision");
            SolveParticleTriangleSelfCollisionIteration(
                m_Particles,
                triangles,
                m_ParticleTriangleSpatialHash,
                particleTriangleLambdas,
                particleTriangleCandidatePairs,
                m_ParticleTriangleCollisionStatistics,
                triangleThickness,
                particleTriangleAlphaTilde);
        }

        {
            RAVEN_PROFILE_SCOPE("SoftBody.Solver.ExternalCollisions");
            SolveSphereCollisions(deltaTime);
            SolvePlaneCollisions(deltaTime);
        }
    }

    // Position Constraintがすべて完了してから一度だけVelocityを再構築します。
    // これにより旧後処理passで必要だった二重のVelocity更新を廃止できます。
    {
        RAVEN_PROFILE_SCOPE("SoftBody.Solver.UpdateVelocities");
        UpdateVelocities(deltaTime);
    }
}

} // namespace ph
} // namespace Raven