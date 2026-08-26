#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Raven/Core/Memory/FrameAllocator.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Solver/SolverTemporaryAllocationCounter.h"
#include "Raven/Physics/SoftBody/SoftBodyParticle.h"
#include "Raven/Physics/SoftBody/SoftBodyTriangleSpatialHashGrid.h"
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

enum class SoftBodyTemporaryAllocatorMode : uint8_t
{
    Heap = 0u,
    FrameAllocator
};

struct SoftBodySolverSettings
{
    // Position Constraintを1Step内で繰り返し解く回数です。
    // XPBDでは反復回数を増やすほどConstraint誤差が小さくなります。
    // StepWithSelfCollisions()ではInternal Constraint・自己衝突・外部Collisionの全てが
    // この共通反復回数の中で解かれます。
    uint32_t SolverIterations = 8u;

    // ParticleをCollider表面から僅かに離して保持する共通厚みです。
    float CollisionThickness = 0.005f;

    // Sphere Collision ConstraintのComplianceです。
    float SphereCollisionCompliance = 0.0f;

    // Plane Collision ConstraintのComplianceです。
    float PlaneCollisionCompliance = 0.0f;

    // ========================================================================
    // Solver Temporary Allocator Mode
    // ========================================================================
    // Heap:
    //   Phase ②のBefore計測用です。SolverTemporaryAllocatorはstd::allocatorを使用します。
    //
    // FrameAllocator:
    //   Phase ③のAfter経路です。SoftBodySolver所有のFrameAllocatorへ一時確保を集約します。
    //
    // デフォルトはPhase ③を正式適用した状態としてFrameAllocatorにします。
    // ④のBefore/After計測時だけ同一シーンでHeapへ切り替えて比較できます。
    SoftBodyTemporaryAllocatorMode TemporaryAllocatorMode =
        SoftBodyTemporaryAllocatorMode::FrameAllocator;
};

// ============================================================================
// Particle-Triangle Self Collision Statistics
// ============================================================================
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
// RigidBodyのContactSolverとは分離した、Particle + Constraint用のXPBD Solverです。
//
// Cloth用統合Step:
//   Predict
//     -> [Distance -> Dihedral -> Particle-Particle -> Particle-Triangle -> Sphere -> Plane] x Iterations
//     -> Velocity
class SoftBodySolver
{
public:
    void SetGravity(const math::Vec3& gravity) { m_Gravity = gravity; }
    const math::Vec3& GetGravity() const { return m_Gravity; }

    void SetSettings(const SoftBodySolverSettings& settings)
    {
        // Allocator Mode変更時、旧FrameAllocator領域は前StepのContainer破棄後にだけResetします。
        // SetSettings()はStep外から呼ぶ前提なので、ここで安全に切替できます。
        if (m_Settings.TemporaryAllocatorMode != settings.TemporaryAllocatorMode)
        {
            m_TemporaryAllocationStatistics.Reset();

            if (settings.TemporaryAllocatorMode == SoftBodyTemporaryAllocatorMode::FrameAllocator)
            {
                m_TemporaryAllocationStatistics.SetBackingAllocator(&m_TemporaryFrameAllocator);
            }
            else
            {
                m_TemporaryAllocationStatistics.SetBackingAllocator(nullptr);
            }
        }

        m_Settings = settings;
    }

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

    // 直近のStepWithSelfCollisions()でStepローカルSTLコンテナが要求したTemporary allocation統計です。
    // AllocationCount/BytesはHeap/Frameの双方で同じSTL要求を数えます。
    // BackingUsedMemory/Peakを見ると、FrameAllocatorへ実際に必要だったArena容量も確認できます。
    const SolverTemporaryAllocationStatistics& GetTemporaryAllocationStatistics() const
    {
        return m_TemporaryAllocationStatistics;
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
    // ========================================================================
    // SoftBody Solver Temporary Frame Allocator
    // ========================================================================
    // 4 MiBはPhase ③の安全側初期値です。④でGetTemporaryAllocationStatistics()の
    // BackingPeakUsedMemoryを計測し、十分なHeadroomを残して縮小します。
    // 容量不足時にHeapへfallbackするとAfter計測が不正確になるためfallbackは行いません。
    static constexpr std::size_t SoftBodyTemporaryFrameAllocatorCapacity = 4u * 1024u * 1024u;

    math::Vec3 m_Gravity{ 0.0f, -9.81f, 0.0f };
    SoftBodySolverSettings m_Settings{};
    std::vector<SoftBodyParticle> m_Particles;
    std::vector<XPBDDistanceConstraint> m_DistanceConstraints;
    std::vector<XPBDDihedralConstraint> m_DihedralConstraints;
    std::vector<SoftBodySphereCollider> m_SphereColliders;
    std::vector<SoftBodyPlaneCollider> m_PlaneColliders;
    SoftBodyParticleTriangleCollisionStatistics m_ParticleTriangleCollisionStatistics{};

    // FrameAllocatorはStatisticsより先に構築し、Statisticsへ非所有ポインタとして登録します。
    FrameAllocator m_TemporaryFrameAllocator{ SoftBodyTemporaryFrameAllocatorCapacity };
    SolverTemporaryAllocationStatistics m_TemporaryAllocationStatistics{ &m_TemporaryFrameAllocator };

    // Particle-Triangle Flat HashはBucketごとにvector capacityを持つため、Stepローカルにすると
    // 毎frame最初のSolver iterationで巨大なBucket配列を再確保してしまいます。
    // Solver寿命まで保持し、frame間でもBucket / Triangle scratch capacityを再利用します。
    SoftBodyTriangleSpatialHashGrid m_ParticleTriangleSpatialHash{ 0.05f };
};

} // namespace ph
} // namespace Raven
