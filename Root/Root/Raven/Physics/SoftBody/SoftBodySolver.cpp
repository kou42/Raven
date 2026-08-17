#include "Raven/Physics/SoftBody/SoftBodySolver.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace Raven
{
namespace ph
{

uint32_t SoftBodySolver::AddParticle(const math::Vec3& position, float inverseMass)
{
    SoftBodyParticle particle{};
    particle.Position = position;
    particle.PreviousPosition = position;
    particle.Velocity = math::Vec3{};
    particle.InverseMass = std::max(0.0f, inverseMass);

    m_Particles.push_back(particle);
    return static_cast<uint32_t>(m_Particles.size() - 1u);
}

uint32_t SoftBodySolver::AddDistanceConstraint(uint32_t particleA, uint32_t particleB, float compliance)
{
    assert(particleA < m_Particles.size());
    assert(particleB < m_Particles.size());
    assert(particleA != particleB);

    XPBDDistanceConstraint constraint{};
    constraint.ParticleA = particleA;
    constraint.ParticleB = particleB;
    constraint.RestLength = (m_Particles[particleB].Position - m_Particles[particleA].Position).Length();
    constraint.Compliance = std::max(0.0f, compliance);
    constraint.Lambda = 0.0f;

    m_DistanceConstraints.push_back(constraint);
    return static_cast<uint32_t>(m_DistanceConstraints.size() - 1u);
}

uint32_t SoftBodySolver::AddSphereCollider(const math::Vec3& center, float radius)
{
    SoftBodySphereCollider collider{};
    collider.Center = center;
    collider.Radius = std::max(0.0f, radius);

    m_SphereColliders.push_back(collider);
    return static_cast<uint32_t>(m_SphereColliders.size() - 1u);
}

void SoftBodySolver::SetSphereCollider(uint32_t colliderIndex, const math::Vec3& center, float radius)
{
    if (colliderIndex >= m_SphereColliders.size())
    {
        return;
    }

    SoftBodySphereCollider& collider = m_SphereColliders[colliderIndex];
    collider.Center = center;
    collider.Radius = std::max(0.0f, radius);
}

void SoftBodySolver::ClearSphereColliders()
{
    m_SphereColliders.clear();
}

void SoftBodySolver::Clear()
{
    m_Particles.clear();
    m_DistanceConstraints.clear();
    m_SphereColliders.clear();
}

void SoftBodySolver::Step(float deltaTime)
{
    // dt=0ではXPBDのalpha = compliance / dt^2を計算できません。
    // 固定ステップ側の一時停止などでも安全に呼べるよう、更新せず終了します。
    if (deltaTime <= 0.0f)
    {
        return;
    }

    PredictPositions(deltaTime);
    ResetConstraintLambdas();

    // Position Based Dynamicsでは、予測位置に対してConstraintを複数回解きます。
    // DistanceだけでなくCollisionも同じ反復の中へ入れることが重要です。
    // 距離制約がParticleをSphere内部へ戻しても、同じiterationで再度押し出されるため、
    // 布全体が球面へ沿う形へ収束しやすくなります。
    const uint32_t iterationCount = std::max(1u, m_Settings.SolverIterations);
    for (uint32_t iteration = 0u; iteration < iterationCount; ++iteration)
    {
        SolveDistanceConstraints(deltaTime);
        SolveSphereCollisions();
    }

    UpdateVelocities(deltaTime);
}

void SoftBodySolver::PredictPositions(float deltaTime)
{
    for (SoftBodyParticle& particle : m_Particles)
    {
        // PreviousPositionは「制約補正前の前フレーム位置」ではなく、今回Step開始時の
        // Positionを保存します。最終Positionとの差をdtで割ることで、制約が生んだ速度も
        // 次フレームへ引き継げます。
        particle.PreviousPosition = particle.Position;

        if (particle.IsFixed())
        {
            particle.Velocity = math::Vec3{};
            continue;
        }

        particle.Velocity += m_Gravity * deltaTime;
        particle.Position += particle.Velocity * deltaTime;
    }
}

void SoftBodySolver::ResetConstraintLambdas()
{
    for (XPBDDistanceConstraint& constraint : m_DistanceConstraints)
    {
        // LambdaはSolver iteration間では保持しますが、別Stepへは持ち越しません。
        // 将来Warm Startを導入する場合は、この境界を変更します。
        constraint.Lambda = 0.0f;
    }
}

void SoftBodySolver::SolveDistanceConstraints(float deltaTime)
{
    const float deltaTimeSq = deltaTime * deltaTime;

    for (XPBDDistanceConstraint& constraint : m_DistanceConstraints)
    {
        assert(constraint.ParticleA < m_Particles.size());
        assert(constraint.ParticleB < m_Particles.size());

        SoftBodyParticle& particleA = m_Particles[constraint.ParticleA];
        SoftBodyParticle& particleB = m_Particles[constraint.ParticleB];

        const float inverseMassSum = particleA.InverseMass + particleB.InverseMass;
        if (inverseMassSum <= 0.0f)
        {
            continue;
        }

        const math::Vec3 delta = particleB.Position - particleA.Position;
        const float distance = delta.Length();
        if (distance <= math::Epsilon)
        {
            continue;
        }

        const math::Vec3 normal = delta / distance;
        const float constraintValue = distance - constraint.RestLength;

        // XPBDの中心式です。
        // alphaTilde = compliance / dt^2
        // deltaLambda = (-C - alphaTilde * lambda) / (wA + wB + alphaTilde)
        //
        // Compliance=0なら通常の硬いPBD距離制約になり、値を増やすと柔らかくなります。
        const float alphaTilde = constraint.Compliance / deltaTimeSq;
        const float denominator = inverseMassSum + alphaTilde;
        if (denominator <= math::Epsilon)
        {
            continue;
        }

        const float deltaLambda =
            (-constraintValue - alphaTilde * constraint.Lambda) / denominator;
        constraint.Lambda += deltaLambda;

        // C = |xB-xA|-L なので gradientA=-n, gradientB=+n です。
        // deltaX = w * gradient(C) * deltaLambda を各Particleへ適用します。
        if (particleA.IsFixed() == false)
        {
            particleA.Position -= normal * (particleA.InverseMass * deltaLambda);
        }

        if (particleB.IsFixed() == false)
        {
            particleB.Position += normal * (particleB.InverseMass * deltaLambda);
        }
    }
}

void SoftBodySolver::SolveSphereCollisions()
{
    if (m_SphereColliders.empty())
    {
        return;
    }

    const float collisionThickness = std::max(0.0f, m_Settings.CollisionThickness);

    for (SoftBodyParticle& particle : m_Particles)
    {
        if (particle.IsFixed())
        {
            continue;
        }

        for (const SoftBodySphereCollider& collider : m_SphereColliders)
        {
            const float targetRadius = collider.Radius + collisionThickness;
            if (targetRadius <= 0.0f)
            {
                continue;
            }

            const math::Vec3 centerToParticle = particle.Position - collider.Center;
            const float distanceSq = centerToParticle.LengthSq();
            const float targetRadiusSq = targetRadius * targetRadius;

            if (distanceSq >= targetRadiusSq)
            {
                continue;
            }

            // Sphere中心とParticleが完全一致すると法線を決められません。
            // その場合だけWorld Upをフォールバックにし、NaNを発生させないようにします。
            math::Vec3 normal{ 0.0f, 1.0f, 0.0f };
            if (distanceSq > math::Epsilon * math::Epsilon)
            {
                normal = centerToParticle / std::sqrt(distanceSq);
            }

            // Collision Constraint:
            //   C(x) = |x - center| - radius >= 0
            // 貫通時だけPositionを最短距離で球面外へ射影します。
            // 静的Colliderなので質量分配は不要で、Particle側だけを補正します。
            particle.Position = collider.Center + normal * targetRadius;
        }
    }
}

void SoftBodySolver::UpdateVelocities(float deltaTime)
{
    const float inverseDeltaTime = 1.0f / deltaTime;

    for (SoftBodyParticle& particle : m_Particles)
    {
        if (particle.IsFixed())
        {
            particle.Velocity = math::Vec3{};
            continue;
        }

        // XPBDではConstraintがPositionを直接補正します。
        // その補正分も運動へ反映するため、最終位置からVelocityを再構築します。
        particle.Velocity = (particle.Position - particle.PreviousPosition) * inverseDeltaTime;
    }
}

} // namespace ph
} // namespace Raven
