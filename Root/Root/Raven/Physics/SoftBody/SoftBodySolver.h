#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodyParticle.h"
#include "Raven/Physics/SoftBody/XPBDDihedralConstraint.h"
#include "Raven/Physics/SoftBody/XPBDDistanceConstraint.h"

namespace Raven
{
namespace ph
{

struct SoftBodyCloth;
struct SoftBodySelfCollisionSettings;
struct SoftBodyParticleTriangleSelfCollisionSettings;

struct SoftBodySolverSettings
{
    // Position Constraintを1Step内で繰り返し解く回数です。
    // XPBDでは反復回数を増やすほどConstraint誤差が小さくなります。
    uint32_t SolverIterations = 8u;

    // ParticleをCollider表面から僅かに離して保持する共通厚みです。
    float CollisionThickness = 0.005f;

    // Sphere Collision ConstraintのComplianceです。
    float SphereCollisionCompliance = 0.0f;

    // Plane Collision ConstraintのComplianceです。
    float PlaneCollisionCompliance = 0.0f;
};

struct SoftBodySphereCollider
{
    math::Vec3 Center{};
    float Radius = 0.5f;

    std::vector<float> ParticleLambdas;

    math::Vec3 AccumulatedReactionImpulse{};
    math::Vec3 ContactPointSum{};
    uint32_t ContactCount = 0u;

    void ResetStepConstraintState(std::size_t particleCount)
    {
        ParticleLambdas.assign(particleCount, 0.0f);
        ResetStepFeedback();
    }

    void ResetStepFeedback()
    {
        AccumulatedReactionImpulse = math::Vec3{};
        ContactPointSum = math::Vec3{};
        ContactCount = 0u;
    }

    math::Vec3 GetAverageContactPoint() const
    {
        if (ContactCount == 0u)
        {
            return Center;
        }

        return ContactPointSum / static_cast<float>(ContactCount);
    }
};

struct SoftBodyPlaneCollider
{
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };
    float Offset = 0.0f;

    std::vector<float> ParticleLambdas;

    void ResetStepConstraintState(std::size_t particleCount)
    {
        ParticleLambdas.assign(particleCount, 0.0f);
    }
};

// ============================================================================
// Soft Body Solver
// ============================================================================
// RigidBodyのContactSolverとは分離した、Particle + Constraint用のXPBD Solverです。
//
// 通常Step:
//   Predict -> Distance -> Dihedral -> Sphere / Plane -> Velocity
//
// Cloth用統合Step:
//   Predict -> [ Distance -> Dihedral -> Particle-Particle Self Collision
//              -> Particle-Triangle Self Collision -> Sphere / Plane ] x Iterations
//           -> Velocity
//
// 自己衝突をSolver反復の外側へ後処理するのではなく同じPosition反復へ含めることで、
// Internal Constraint・自己衝突・外部Colliderが互いの補正結果を同一Step内で再評価できます。
class SoftBodySolver
{
public:
    void SetGravity(const math::Vec3& gravity) { m_Gravity = gravity; }
    const math::Vec3& GetGravity() const { return m_Gravity; }

    void SetSettings(const SoftBodySolverSettings& settings) { m_Settings = settings; }
    SoftBodySolverSettings& GetSettings() { return m_Settings; }
    const SoftBodySolverSettings& GetSettings() const { return m_Settings; }

    uint32_t AddParticle(const math::Vec3& position, float inverseMass = 1.0f);
    uint32_t AddDistanceConstraint(uint32_t particleA, uint32_t particleB, float compliance = 0.0f);
    uint32_t AddDihedralConstraint(
        uint32_t oppositeA,
        uint32_t oppositeB,
        uint32_t edgeA,
        uint32_t edgeB,
        float compliance = 0.0f);

    uint32_t AddSphereCollider(const math::Vec3& center, float radius);
    void SetSphereCollider(uint32_t colliderIndex, const math::Vec3& center, float radius);
    void ClearSphereColliders();

    uint32_t AddPlaneCollider(const math::Vec3& normal, float offset);
    void SetPlaneCollider(uint32_t colliderIndex, const math::Vec3& normal, float offset);
    void ClearPlaneColliders();

    void Clear();

    // 既存のSoftBody用Stepです。自己衝突Topologyを必要としない用途はこちらを使用します。
    void Step(float deltaTime);

    // ========================================================================
    // Cloth Integrated XPBD Step
    // ========================================================================
    // Cloth自己衝突を既存SolverIterationsの「内側」へ組み込むStepです。
    // 各self collision設定のSolverIterationsはここでは使用せず、外側のm_Settings.SolverIterationsへ
    // 統一します。これによりDistance / Dihedral / Self Collision / External Collisionが
    // 1つの反復系列として収束し、Step後の追加Position補正とVelocity再構築が不要になります。
    void StepWithSelfCollisions(
        float deltaTime,
        const SoftBodyCloth& cloth,
        const SoftBodySelfCollisionSettings& particleSettings,
        const SoftBodyParticleTriangleSelfCollisionSettings& particleTriangleSettings);

    std::vector<SoftBodyParticle>& GetParticles() { return m_Particles; }
    const std::vector<SoftBodyParticle>& GetParticles() const { return m_Particles; }
    const std::vector<XPBDDistanceConstraint>& GetDistanceConstraints() const { return m_DistanceConstraints; }
    const std::vector<XPBDDihedralConstraint>& GetDihedralConstraints() const { return m_DihedralConstraints; }
    const std::vector<SoftBodySphereCollider>& GetSphereColliders() const { return m_SphereColliders; }
    const std::vector<SoftBodyPlaneCollider>& GetPlaneColliders() const { return m_PlaneColliders; }

private:
    void PredictPositions(float deltaTime);
    void ResetConstraintLambdas();
    void ResetCollisionConstraintState();
    void SolveDistanceConstraints(float deltaTime);
    void SolveDihedralConstraints(float deltaTime);
    void SolveSphereCollisions(float deltaTime);
    void SolvePlaneCollisions(float deltaTime);
    void UpdateVelocities(float deltaTime);

private:
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
    SoftBodySolverSettings m_Settings{};
    std::vector<SoftBodyParticle> m_Particles;
    std::vector<XPBDDistanceConstraint> m_DistanceConstraints;
    std::vector<XPBDDihedralConstraint> m_DihedralConstraints;
    std::vector<SoftBodySphereCollider> m_SphereColliders;
    std::vector<SoftBodyPlaneCollider> m_PlaneColliders;
};

} // namespace ph
} // namespace Raven
