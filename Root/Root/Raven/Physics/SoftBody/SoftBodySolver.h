#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodyParticle.h"
#include "Raven/Physics/SoftBody/XPBDDihedralConstraint.h"
#include "Raven/Physics/SoftBody/XPBDDistanceConstraint.h"
#include "Raven/Physics/SoftBody/XPBDVolumeConstraint.h"

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
    // StepWithSelfCollisions()ではInternal Constraint・自己衝突・外部Collisionの全てが
    // この共通反復回数の中で解かれます。
    uint32_t SolverIterations = 8u;

    // ParticleをCollider表面から僅かに離して保持する共通厚みです。
    // Clothは数学的には厚み0の面なので、完全にCollider表面へ配置すると浮動小数点誤差により
    // 次の反復で再び内部判定されやすくなります。小さな余白を持たせて接触を安定させます。
    float CollisionThickness = 0.005f;

    // Sphere Collision ConstraintのComplianceです。
    // 0.0fなら硬い片側Constraintとして働き、値を大きくするとCollider表面が柔らかくなります。
    // Distance Constraintと同じく alphaTilde = Compliance / dt^2 としてXPBD式へ入ります。
    float SphereCollisionCompliance = 0.0f;

    // Plane Collision ConstraintのComplianceです。
    // Sphereと同じXPBD式へ統一し、0.0fなら硬い床、値を増やすと沈み込みを許す柔らかい床になります。
    float PlaneCollisionCompliance = 0.0f;
};

// ============================================================================
// Particle-Triangle Self Collision Statistics
// ============================================================================
// Particle-Triangle Broad Phase / Narrow Phaseの負荷を1 Step単位で比較するための計測値です。
// StepWithSelfCollisions()開始時にReset()され、全Solver Iterationの値を合算します。
//
// CandidateCount:
//   Spatial Hashが生成したParticle-Triangle候補ペア総数です。
//
// NarrowPhaseCount:
//   cheap rejectを通過し、Closest Point計算へ進んだ回数です。
//
// DistanceCount:
//   Distance Squared Fast Rejectを通過し、sqrtを含む距離・法線構築まで完了した回数です。
//
// ConstraintCount:
//   thickness判定を通過し、XPBD Constraint計算へ進んだ回数です。
//
// DenominatorRejectCount:
//   Constraintへ進んだものの、逆質量とComplianceから作るdenominatorが小さすぎて
//   DeltaLambda計算を安全に行えなかった回数です。
//
// DeltaLambdaCount:
//   有効なdenominatorを得て、XPBDのDeltaLambda計算まで実行した回数です。
//   ConstraintCountとの差はDenominatorRejectCountと一致するため、funnelの連続性確認にも使えます。
//
// DeltaLambdaRejectCount:
//   DeltaLambda計算後、実際に適用するappliedDeltaLambdaがEpsilon以下でPosition更新を省略した回数です。
//   この値が大きい場合、Constraint後半まで到達しているのに実Position更新へ寄与しない候補が多いことを示します。
//
// PositionCorrectionCount:
//   有効なappliedDeltaLambdaを得た後、少なくとも1 Particleへ実際に位置補正した回数です。
struct SoftBodyParticleTriangleCollisionStatistics
{
    uint64_t CandidateCount = 0u;
    uint64_t NarrowPhaseCount = 0u;
    uint64_t DistanceCount = 0u;
    uint64_t ConstraintCount = 0u;
    uint64_t DenominatorRejectCount = 0u;
    uint64_t DeltaLambdaCount = 0u;
    uint64_t DeltaLambdaRejectCount = 0u;
    uint64_t PositionCorrectionCount = 0u;

    void Reset()
    {
        CandidateCount = 0u;
        NarrowPhaseCount = 0u;
        DistanceCount = 0u;
        ConstraintCount = 0u;
        DenominatorRejectCount = 0u;
        DeltaLambdaCount = 0u;
        DeltaLambdaRejectCount = 0u;
        PositionCorrectionCount = 0u;
    }
};

// ============================================================================
// Static / Kinematic Sphere Collision Constraint
// ============================================================================
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

// ============================================================================
// Static Plane Collision Constraint
// ============================================================================
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
    void Step(float deltaTime);

    void StepWithVolumeConstraints(
        float deltaTime,
        std::vector<XPBDVolumeConstraint>& volumeConstraints);

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

    const SoftBodyParticleTriangleCollisionStatistics& GetParticleTriangleCollisionStatistics() const
    {
        return m_ParticleTriangleCollisionStatistics;
    }

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
    math::Vec3 m_Gravity{ 0.0f, -9.81f, 0.0f };
    SoftBodySolverSettings m_Settings{};
    std::vector<SoftBodyParticle> m_Particles;
    std::vector<XPBDDistanceConstraint> m_DistanceConstraints;
    std::vector<XPBDDihedralConstraint> m_DihedralConstraints;
    std::vector<SoftBodySphereCollider> m_SphereColliders;
    std::vector<SoftBodyPlaneCollider> m_PlaneColliders;
    SoftBodyParticleTriangleCollisionStatistics m_ParticleTriangleCollisionStatistics{};
};

} // namespace ph
} // namespace Raven
