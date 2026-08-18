#include "Raven/Physics/SoftBody/SoftBodySelfCollision.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodySolver.h"
#include "Raven/Physics/SoftBody/SoftBodySpatialHashGrid.h"

namespace Raven
{
namespace ph
{
namespace
{

uint64_t MakeParticlePairKey(uint32_t particleA, uint32_t particleB)
{
    const uint32_t lower = std::min(particleA, particleB);
    const uint32_t upper = std::max(particleA, particleB);
    return (static_cast<uint64_t>(lower) << 32u) | static_cast<uint64_t>(upper);
}

void BuildExcludedParticlePairs(
    const SoftBodySolver& solver,
    std::unordered_set<uint64_t>& outExcludedPairs)
{
    outExcludedPairs.clear();

    const std::vector<XPBDDistanceConstraint>& constraints = solver.GetDistanceConstraints();
    outExcludedPairs.reserve(constraints.size());

    for (const XPBDDistanceConstraint& constraint : constraints)
    {
        outExcludedPairs.insert(MakeParticlePairKey(
            constraint.ParticleA,
            constraint.ParticleB));
    }
}

} // namespace

void SolveSoftBodyParticleSelfCollisions(
    SoftBodySolver& solver,
    float deltaTime,
    const SoftBodySelfCollisionSettings& settings)
{
    if (settings.Enabled == false || deltaTime <= 0.0f)
    {
        return;
    }

    const float particleRadius = std::max(0.0f, settings.ParticleRadius);
    const float targetDistance = particleRadius * 2.0f;
    if (targetDistance <= math::Epsilon)
    {
        return;
    }

    std::vector<SoftBodyParticle>& particles = solver.GetParticles();
    if (particles.size() < 2u)
    {
        return;
    }

    // Distance Constraint で直接接続された Pair は Cloth Topology の隣接頂点です。
    // Structural / Shear Constraint の自然長より Self Collision Diameter が大きい場合でも、
    // 隣接頂点同士を自己衝突で無理に押し広げないよう除外します。
    std::unordered_set<uint64_t> excludedPairs;
    BuildExcludedParticlePairs(solver, excludedPairs);

    SoftBodySpatialHashGrid spatialHash(targetDistance);
    std::vector<SoftBodySpatialHashPair> candidatePairs;

    // Pair は deformation によって iteration ごとに入れ替わるため、Lambda は Pair Key で保持します。
    // 同一 Step 内では XPBD Lambda を蓄積し、Step を跨ぐ Warm Start は行いません。
    std::unordered_map<uint64_t, float> lambdas;

    const float compliance = std::max(0.0f, settings.Compliance);
    const float alphaTilde = compliance / (deltaTime * deltaTime);
    const uint32_t iterationCount = std::max(1u, settings.SolverIterations);

    for (uint32_t iteration = 0u; iteration < iterationCount; ++iteration)
    {
        // Constraint 補正で Particle が別セルへ移動するため、各 iteration で必ず再構築します。
        // Grid を Step 冒頭に1回だけ構築すると、折り畳まれた Cloth がセル境界を跨いだ後に
        // 本来接触すべき Pair を取りこぼす可能性があります。
        spatialHash.Build(particles);
        spatialHash.GenerateCandidatePairs(candidatePairs);

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
                // 完全同位置では現在位置だけから分離方向を決められません。
                // PreviousPosition の差が残っていれば直前の相対方向を利用し、それも退化している場合は
                // Pair Index に依存しない固定 +X を fallback にして NaN を防ぎます。
                const math::Vec3 previousDelta =
                    particleB.PreviousPosition - particleA.PreviousPosition;
                const float previousDistanceSq = previousDelta.LengthSq();
                if (previousDistanceSq > math::Epsilon * math::Epsilon)
                {
                    normal = previousDelta / std::sqrt(previousDistanceSq);
                }
            }

            // Particle Sphere 同士の片側 Constraint:
            //
            //   C(x) = |xB - xA| - targetDistance >= 0
            //
            // C < 0 のときだけ押し離します。既に Lambda がある Pair は他 Constraint で離れた場合に
            // Lambda を減らして拘束を解放できるよう、C >= 0 でも一度 XPBD 式を評価します。
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

            // C = |xB-xA|-D の Gradient は A=-n, B=+n です。
            // inverseMass で補正量を分配するため、固定点(InverseMass=0)はその場に残り、
            // 可動 Particle 側だけが必要な距離だけ押し出されます。
            if (particleA.IsFixed() == false)
            {
                particleA.Position -=
                    normal * (particleA.InverseMass * appliedDeltaLambda);
            }

            if (particleB.IsFixed() == false)
            {
                particleB.Position +=
                    normal * (particleB.InverseMass * appliedDeltaLambda);
            }
        }
    }

    // 既存 Solver::Step() は自己衝突前の Position で Velocity を再構築済みです。
    // ここで Step 開始時 PreviousPosition との差からもう一度再構築し、自己衝突による Position 補正も
    // 次フレームの運動へ反映します。
    const float inverseDeltaTime = 1.0f / deltaTime;
    for (SoftBodyParticle& particle : particles)
    {
        if (particle.IsFixed())
        {
            particle.Velocity = math::Vec3{};
            continue;
        }

        particle.Velocity =
            (particle.Position - particle.PreviousPosition) * inverseDeltaTime;
    }
}

} // namespace ph
} // namespace Raven
