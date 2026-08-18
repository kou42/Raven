#include "Raven/Physics/SoftBody/SoftBodySolver.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace Raven
{
namespace ph
{
namespace
{

// ============================================================================
// XPBD Tetrahedron Volume Constraint Iteration
// ============================================================================
// 四面体の符号付き体積
//
//   V = dot(p1 - p0, cross(p2 - p0, p3 - p0)) / 6
//
// に対する各頂点のGradientは次の形になります。
//
//   grad1 = cross(p2 - p0, p3 - p0) / 6
//   grad2 = cross(p3 - p0, p1 - p0) / 6
//   grad3 = cross(p1 - p0, p2 - p0) / 6
//   grad0 = -(grad1 + grad2 + grad3)
//
// XPBDではDistance Constraintと同様に
//
//   deltaLambda = (-C - alphaTilde * lambda)
//               / (sum(w_i * |grad_i|^2) + alphaTilde)
//
// を解き、deltaX_i = w_i * grad_i * deltaLambda を各Particleへ適用します。
void SolveVolumeConstraintIteration(
    std::vector<SoftBodyParticle>& particles,
    std::vector<XPBDVolumeConstraint>& constraints,
    float deltaTime)
{
    const float deltaTimeSq = deltaTime * deltaTime;

    for (XPBDVolumeConstraint& constraint : constraints)
    {
        assert(constraint.Particle0 < particles.size());
        assert(constraint.Particle1 < particles.size());
        assert(constraint.Particle2 < particles.size());
        assert(constraint.Particle3 < particles.size());

        SoftBodyParticle& particle0 = particles[constraint.Particle0];
        SoftBodyParticle& particle1 = particles[constraint.Particle1];
        SoftBodyParticle& particle2 = particles[constraint.Particle2];
        SoftBodyParticle& particle3 = particles[constraint.Particle3];

        const math::Vec3 p10 = particle1.Position - particle0.Position;
        const math::Vec3 p20 = particle2.Position - particle0.Position;
        const math::Vec3 p30 = particle3.Position - particle0.Position;

        const math::Vec3 gradient1 = math::Vec3::Cross(p20, p30) / 6.0f;
        const math::Vec3 gradient2 = math::Vec3::Cross(p30, p10) / 6.0f;
        const math::Vec3 gradient3 = math::Vec3::Cross(p10, p20) / 6.0f;
        const math::Vec3 gradient0 = (gradient1 + gradient2 + gradient3) * -1.0f;

        // 現在体積とRestVolumeの差を0へ近づけます。
        // RestVolumeは符号付きなので、四面体が反転した場合はConstraint Errorが大きくなり、
        // 元の向きへ戻そうとする補正が発生します。
        const float currentVolume = ComputeSignedTetrahedronVolume(
            particle0.Position,
            particle1.Position,
            particle2.Position,
            particle3.Position);
        const float constraintValue = currentVolume - constraint.RestVolume;
        const float alphaTilde = std::max(0.0f, constraint.Compliance) / deltaTimeSq;

        const float denominator =
            particle0.InverseMass * gradient0.LengthSq()
            + particle1.InverseMass * gradient1.LengthSq()
            + particle2.InverseMass * gradient2.LengthSq()
            + particle3.InverseMass * gradient3.LengthSq()
            + alphaTilde;

        // 完全退化した四面体では全Gradientが0になる場合があります。
        // その状態で除算するとNaNが全Particleへ伝播するため、このConstraintだけ安全にskipします。
        if (denominator <= math::Epsilon)
        {
            continue;
        }

        const float deltaLambda =
            (-constraintValue - alphaTilde * constraint.Lambda) / denominator;
        constraint.Lambda += deltaLambda;

        if (particle0.IsFixed() == false)
        {
            particle0.Position += gradient0 * (particle0.InverseMass * deltaLambda);
        }

        if (particle1.IsFixed() == false)
        {
            particle1.Position += gradient1 * (particle1.InverseMass * deltaLambda);
        }

        if (particle2.IsFixed() == false)
        {
            particle2.Position += gradient2 * (particle2.InverseMass * deltaLambda);
        }

        if (particle3.IsFixed() == false)
        {
            particle3.Position += gradient3 * (particle3.InverseMass * deltaLambda);
        }
    }
}

} // namespace

void SoftBodySolver::StepWithVolumeConstraints(
    float deltaTime,
    std::vector<XPBDVolumeConstraint>& volumeConstraints)
{
    // XPBDでは alphaTilde = compliance / dt^2 を使うため、0以下のdtでは解けません。
    if (deltaTime <= 0.0f)
    {
        return;
    }

    PredictPositions(deltaTime);
    ResetConstraintLambdas();
    ResetCollisionConstraintState();

    // Volume Lambdaも既存Distance / Dihedralと同じく、同一Step内のiteration間だけ蓄積します。
    // Warm Startはまだ行わないためStep開始時に0へ戻します。
    for (XPBDVolumeConstraint& constraint : volumeConstraints)
    {
        constraint.Lambda = 0.0f;
    }

    const uint32_t iterationCount = std::max(1u, m_Settings.SolverIterations);
    for (uint32_t iteration = 0u; iteration < iterationCount; ++iteration)
    {
        // ====================================================================
        // Volumetric Soft Body Unified XPBD Order
        // ====================================================================
        // Distanceで辺長・Shearを整えた後にVolumeを戻し、必要ならDihedralも処理します。
        // 最後にCollisionで外部形状から押し戻し、次iterationで再度Distance/Volumeが評価します。
        // これにより「床に押し付けられながら体積を保つ」ようなゼリー挙動を同一Step内で収束させます。
        SolveDistanceConstraints(deltaTime);
        SolveVolumeConstraintIteration(m_Particles, volumeConstraints, deltaTime);
        SolveDihedralConstraints(deltaTime);
        SolveSphereCollisions(deltaTime);
        SolvePlaneCollisions(deltaTime);
    }

    // Position Constraintをすべて解いた後に一度だけVelocityを再構築します。
    // Volume補正による反発もVelocityへ反映され、次Stepの「ぷるん」と戻る運動へつながります。
    UpdateVelocities(deltaTime);
}

} // namespace ph
} // namespace Raven
