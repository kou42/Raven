#include "Raven/Physics/SoftBody/SoftBodySolver.h"

#include <algorithm>
#include <cassert>

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

void SoftBodySolver::Clear()
{
    m_Particles.clear();
    m_DistanceConstraints.clear();
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
    // ここでは最低1回は実行し、設定値0による意図しない無制約状態を防ぎます。
    const uint32_t iterationCount = std::max(1u, m_Settings.SolverIterations);
    for (uint32_t iteration = 0u; iteration < iterationCount; ++iteration)
    {
        SolveDistanceConstraints(deltaTime);
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
