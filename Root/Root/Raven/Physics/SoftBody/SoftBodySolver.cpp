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

    // 負の逆質量には物理的な意味がないため0へclampします。
    // InverseMass == 0.0f は固定Particleとして扱われます。
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

    // Constraint登録時の距離を自然長として保存します。
    // Cloth Builderは初期形状を作った直後にConstraintを張るため、この値が未変形時の長さになります。
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
    collider.ResetStepFeedback();

    m_SphereColliders.push_back(collider);
    return static_cast<uint32_t>(m_SphereColliders.size() - 1u);
}

void SoftBodySolver::SetSphereCollider(uint32_t colliderIndex, const math::Vec3& center, float radius)
{
    if (colliderIndex >= m_SphereColliders.size())
    {
        return;
    }

    // Position/Radiusだけを更新し、前StepのFeedbackは次のStep冒頭でまとめてResetします。
    // 外部側がStep直後にFeedbackを読む時間を確保するため、Set時には消さないことが重要です。
    SoftBodySphereCollider& collider = m_SphereColliders[colliderIndex];
    collider.Center = center;
    collider.Radius = std::max(0.0f, radius);
}

void SoftBodySolver::ClearSphereColliders()
{
    m_SphereColliders.clear();
}

uint32_t SoftBodySolver::AddPlaneCollider(const math::Vec3& normal, float offset)
{
    SoftBodyPlaneCollider collider{};
    collider.Normal = normal;
    collider.Offset = offset;

    // Planeのsigned distance計算はNormalが単位ベクトルであることを前提にします。
    // ゼロベクトルの場合は正規化できないため、+Y Planeを安全なfallbackとして使用します。
    if (collider.Normal.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        collider.Normal = { 0.0f, 1.0f, 0.0f };
    }
    else
    {
        collider.Normal.Normalize();
    }

    m_PlaneColliders.push_back(collider);
    return static_cast<uint32_t>(m_PlaneColliders.size() - 1u);
}

void SoftBodySolver::SetPlaneCollider(uint32_t colliderIndex, const math::Vec3& normal, float offset)
{
    if (colliderIndex >= m_PlaneColliders.size())
    {
        return;
    }

    SoftBodyPlaneCollider& collider = m_PlaneColliders[colliderIndex];
    collider.Normal = normal;
    collider.Offset = offset;

    if (collider.Normal.LengthSq() <= math::Epsilon * math::Epsilon)
    {
        collider.Normal = { 0.0f, 1.0f, 0.0f };
    }
    else
    {
        collider.Normal.Normalize();
    }
}

void SoftBodySolver::ClearPlaneColliders()
{
    m_PlaneColliders.clear();
}

void SoftBodySolver::Clear()
{
    // SolverのTopologyを完全に作り直すためのClearです。
    // Colliderも同時に消えるため、Deformer側はCloth再構築後に保持している設定を再登録します。
    m_Particles.clear();
    m_DistanceConstraints.clear();
    m_SphereColliders.clear();
    m_PlaneColliders.clear();
}

void SoftBodySolver::Step(float deltaTime)
{
    // XPBDでは alphaTilde = compliance / dt^2 を使用するため、dt <= 0では計算できません。
    // Pauseや初期化時に0秒更新が来ても安全に終了します。
    if (deltaTime <= 0.0f)
    {
        return;
    }

    PredictPositions(deltaTime);
    ResetConstraintLambdas();
    ResetCollisionFeedback();

    // Position Based Dynamicsでは、予測位置に対してConstraintを複数回反復して収束させます。
    // DistanceだけでなくCollisionも同じ反復へ含めることが重要です。
    const uint32_t iterationCount = std::max(1u, m_Settings.SolverIterations);
    for (uint32_t iteration = 0u; iteration < iterationCount; ++iteration)
    {
        // 内部Constraintを先に解き、その結果Collider内部へ戻ったParticleを同じ反復内で
        // Sphere / Planeの外側へ押し出します。
        SolveDistanceConstraints(deltaTime);
        SolveSphereCollisions(deltaTime);
        SolvePlaneCollisions();
    }

    UpdateVelocities(deltaTime);
}

void SoftBodySolver::PredictPositions(float deltaTime)
{
    for (SoftBodyParticle& particle : m_Particles)
    {
        // PreviousPositionには「今回のStep開始時のPosition」を保存します。
        // Constraint補正後の最終Positionとの差からVelocityを再構築することで、Constraintによって
        // 生じた移動量も次フレームの運動へ反映できます。
        particle.PreviousPosition = particle.Position;

        if (particle.IsFixed())
        {
            // 固定点は外力でも移動させず、Velocityも残さないよう0へ戻します。
            particle.Velocity = math::Vec3{};
            continue;
        }

        // Semi-implicit Eulerに近い順序で、先にVelocityへ重力を加えてからPositionを予測します。
        particle.Velocity += m_Gravity * deltaTime;
        particle.Position += particle.Velocity * deltaTime;
    }
}

void SoftBodySolver::ResetConstraintLambdas()
{
    for (XPBDDistanceConstraint& constraint : m_DistanceConstraints)
    {
        // Lambdaは同一Step内のiteration間では蓄積しますが、現在はStepを跨ぐWarm Startを行いません。
        // そのため各Step開始時に0へ戻します。
        constraint.Lambda = 0.0f;
    }
}

void SoftBodySolver::ResetCollisionFeedback()
{
    for (SoftBodySphereCollider& collider : m_SphereColliders)
    {
        // Feedbackは「直前に完了したStep」の結果として外部側から読み取られます。
        // 新しいStepへ入る直前にResetすることで、Step間のImpulse蓄積を防ぎます。
        collider.ResetStepFeedback();
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
            // 両端が固定点ならPositionを補正できないため、このConstraintは解く必要がありません。
            continue;
        }

        const math::Vec3 delta = particleB.Position - particleA.Position;
        const float distance = delta.Length();
        if (distance <= math::Epsilon)
        {
            // 2点がほぼ同位置の場合はConstraint方向を決められないためNaN防止でスキップします。
            continue;
        }

        const math::Vec3 normal = delta / distance;

        // C(x) = |xB - xA| - RestLength
        // C=0が制約を満たす状態です。正なら伸び、負なら縮みを表します。
        const float constraintValue = distance - constraint.RestLength;

        // XPBDの中心式:
        //   alphaTilde = compliance / dt^2
        //   deltaLambda = (-C - alphaTilde * lambda)
        //                 / (wA + wB + alphaTilde)
        //
        // compliance=0なら硬いPBD距離制約となり、値を大きくすると柔らかくなります。
        const float alphaTilde = constraint.Compliance / deltaTimeSq;
        const float denominator = inverseMassSum + alphaTilde;

        if (denominator <= math::Epsilon)
        {
            continue;
        }

        const float deltaLambda =
            (-constraintValue - alphaTilde * constraint.Lambda) / denominator;
        constraint.Lambda += deltaLambda;

        // C = |xB-xA|-L のgradientは A側=-n, B側=+n です。
        // deltaX = inverseMass * gradient(C) * deltaLambda を各Particleへ適用します。
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

void SoftBodySolver::SolveSphereCollisions(float deltaTime)
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

        for (SoftBodySphereCollider& collider : m_SphereColliders)
        {
            // Clothの厚み分だけSphereを膨らませた半径を接触面として扱います。
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
                // Sphere外側ならConstraintを満たしているため補正不要です。
                continue;
            }

            // Sphere中心とParticleが完全一致すると法線を決められません。
            // その場合だけWorld Upをfallbackにし、0除算とNaNを防ぎます。
            math::Vec3 normal{ 0.0f, 1.0f, 0.0f };
            if (distanceSq > math::Epsilon * math::Epsilon)
            {
                normal = centerToParticle / std::sqrt(distanceSq);
            }

            const math::Vec3 oldPosition = particle.Position;
            const math::Vec3 correctedPosition = collider.Center + normal * targetRadius;
            const math::Vec3 correction = correctedPosition - oldPosition;

            // Collision Constraint:
            //   C(x) = |x - center| - targetRadius >= 0
            // 貫通時だけParticleを最短距離でSphere表面へ射影します。
            particle.Position = correctedPosition;

            // ====================================================================
            // Approximate Soft -> Rigid reaction impulse
            // ====================================================================
            // Position Based SolverはImpulseを直接解いていないため、位置補正から近似します。
            // ParticleのConstraint補正による速度変化は概ね correction / dt です。
            // mass = 1 / inverseMass なのでParticleに与えた運動量変化を
            //
            //   deltaP ~= mass * correction / dt
            //
            // とみなし、Sphere側にはその反作用 -deltaP を蓄積します。
            //
            // これは厳密なContact Jacobianによる双方向Solverではありません。
            // 同じParticleが複数iterationで押し出される分も累積されるため、外部連成側でScaleを掛けて
            // 安定性を調整し、本格連成ではRigid/Soft共通Constraintへ置換する想定です。
            if (particle.InverseMass > math::Epsilon)
            {
                const float particleMass = 1.0f / particle.InverseMass;
                const math::Vec3 particleImpulse =
                    correction * (particleMass / deltaTime);

                collider.AccumulatedReactionImpulse -= particleImpulse;

                // Thicknessを除いた実Sphere表面をContact Pointとして使用します。
                // 後でRigidBodyへAddImpulseAtPoint()すると、中心から外れた接触は回転にも寄与します。
                collider.ContactPointSum += collider.Center + normal * collider.Radius;
                ++collider.ContactCount;
            }
        }
    }
}

void SoftBodySolver::SolvePlaneCollisions()
{
    if (m_PlaneColliders.empty())
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

        for (const SoftBodyPlaneCollider& collider : m_PlaneColliders)
        {
            // Plane外側条件:
            //   dot(n, x) - offset >= thickness
            //
            // signedDistanceは単位Normalを前提としているため、Add/Set時にNormalを正規化しています。
            const float signedDistance =
                math::Vec3::Dot(collider.Normal, particle.Position) - collider.Offset;

            if (signedDistance >= collisionThickness)
            {
                continue;
            }

            // 条件を満たす最小距離だけNormal方向へ押し戻します。
            // Planeは静的なので、Sphereと同様にParticle側だけを補正します。
            const float correctionDistance = collisionThickness - signedDistance;
            particle.Position += collider.Normal * correctionDistance;
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
        // 最終位置とStep開始時位置の差からVelocityを再構築することで、Constraint補正による移動も
        // 次Stepへ正しく引き継ぎます。
        particle.Velocity = (particle.Position - particle.PreviousPosition) * inverseDeltaTime;
    }
}

} // namespace ph
} // namespace Raven
