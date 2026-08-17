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

struct SoftBodySolverSettings
{
    // Position Constraintを1Step内で繰り返し解く回数です。
    // XPBDでは反復回数を増やすほどConstraint誤差が小さくなります。
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
// Static / Kinematic Sphere Collision Constraint
// ============================================================================
// SoftBody側で扱うSphere Colliderです。
// Solver自身はSphereを移動させず、外部側がSetSphereCollider()でCenter/Radiusを更新します。
// そのため静的Sphereだけでなく、RigidBody Transformを毎フレーム同期したKinematicな境界としても使えます。
//
// ParticleLambdasは「Particle × Sphere」の片側XPBD Constraintが同一Step内で蓄積するLambdaです。
// Distance Constraintと同じくStep開始時に0へ戻し、Solver iteration間だけ保持します。
// Collisionは C(x) >= 0 の不等式なのでLambdaは常に0以上へclampし、接触が離れる方向へ動いた場合は
// Lambdaを減少させて拘束を解放できるようにします。
//
// AccumulatedReactionImpulse / ContactPointSum / ContactCount は1Stepだけ有効なTransient情報です。
// XPBDで得たDeltaLambdaから
//
//   reaction impulse ~= -normal * DeltaLambda / dt
//
// としてSphere側の反作用を蓄積します。以前の「位置補正量からImpulseを逆算する方式」と異なり、
// Constraint Solverが実際に適用したLambda増減を直接利用するため、反復回数による過大評価を抑えられます。
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
// Plane式は dot(Normal, x) - Offset = 0 とします。
// 実際のCollision Constraintは共通Thicknessを加味して
//
//   C(x) = dot(Normal, x) - Offset - CollisionThickness >= 0
//
// として扱います。Sphereと同じく片側XPBD ConstraintとしてLambdaを0以上へclampするため、
// PlaneはParticleをNormal方向へ押せますが反対側へ引っ張りません。
// NormalはAdd/Set時に正規化し、ゼロベクトルが渡された場合は+Yを安全なfallbackとして使用します。
struct SoftBodyPlaneCollider
{
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };
    float Offset = 0.0f;

    // Particleごとの片側Constraint Lambdaです。
    // 現段階ではStepを跨ぐWarm Startは行わず、各Step開始時に0へ戻します。
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
// 1Stepの基本順序:
//   1. 重力を積分してParticleの予測位置を作る
//   2. Distance / Dihedral / Sphere / PlaneのLambdaをStep用に初期化する
//   3. XPBD Distance Constraintを反復解決する
//   4. Dihedral Angle Bending Constraintを反復解決する
//   5. 同じ反復内でSphere / Plane Collisionを片側XPBD Constraintとして解決する
//   6. 最終PositionとStep開始時PositionからVelocityを再構築する
//
// BendingをDistance近似とは別Constraintとして持つことで、Cloth Topology上の「隣接Triangle間の角度」を
// 直接制御できます。Collisionも同じPosition反復へ含めるため、各Constraintが互いに補正した結果を
// 同一Step内で再評価して収束させられます。
class SoftBodySolver
{
public:
    void SetGravity(const math::Vec3& gravity) { m_Gravity = gravity; }
    const math::Vec3& GetGravity() const { return m_Gravity; }

    void SetSettings(const SoftBodySolverSettings& settings) { m_Settings = settings; }
    SoftBodySolverSettings& GetSettings() { return m_Settings; }
    const SoftBodySolverSettings& GetSettings() const { return m_Settings; }

    // Particleを追加し、そのIndexを返します。
    // inverseMass == 0.0f のParticleは固定点として扱われます。
    uint32_t AddParticle(const math::Vec3& position, float inverseMass = 1.0f);

    // 現在のParticle間距離をRestLengthとしてXPBD Distance Constraintを追加します。
    // compliance == 0.0f に近いほど硬く、値を大きくすると柔らかい制約になります。
    uint32_t AddDistanceConstraint(uint32_t particleA, uint32_t particleB, float compliance = 0.0f);

    // 共有Edgeを持つ2TriangleからDihedral Bending Constraintを追加します。
    // oppositeA/oppositeBはそれぞれのTriangleで共有Edgeの反対側にある頂点です。
    // RestAngleは追加時の形状から自動計算します。
    uint32_t AddDihedralConstraint(
        uint32_t oppositeA,
        uint32_t oppositeB,
        uint32_t edgeA,
        uint32_t edgeB,
        float compliance = 0.0f);

    // Sphere Colliderを追加します。
    // 戻り値はCollider Indexで、SetSphereCollider()による後続更新に利用できます。
    uint32_t AddSphereCollider(const math::Vec3& center, float radius);
    void SetSphereCollider(uint32_t colliderIndex, const math::Vec3& center, float radius);
    void ClearSphereColliders();

    // 静的Plane Colliderを追加します。
    // offsetは dot(normal, x) = offset を満たすPlane位置を表します。
    uint32_t AddPlaneCollider(const math::Vec3& normal, float offset);
    void SetPlaneCollider(uint32_t colliderIndex, const math::Vec3& normal, float offset);
    void ClearPlaneColliders();

    // Particle / Constraint / Colliderをすべて破棄します。
    // Clothを再構築した場合は、必要なColliderも再登録する必要があります。
    void Clear();

    // XPBDの1Simulation Stepを進めます。
    // deltaTime <= 0.0f の場合は計算を行いません。
    void Step(float deltaTime);

    std::vector<SoftBodyParticle>& GetParticles() { return m_Particles; }
    const std::vector<SoftBodyParticle>& GetParticles() const { return m_Particles; }
    const std::vector<XPBDDistanceConstraint>& GetDistanceConstraints() const { return m_DistanceConstraints; }
    const std::vector<XPBDDihedralConstraint>& GetDihedralConstraints() const { return m_DihedralConstraints; }
    const std::vector<SoftBodySphereCollider>& GetSphereColliders() const { return m_SphereColliders; }
    const std::vector<SoftBodyPlaneCollider>& GetPlaneColliders() const { return m_PlaneColliders; }

private:
    // Step開始時の位置をPreviousPositionへ保存し、重力とVelocityから予測位置を作ります。
    void PredictPositions(float deltaTime);

    // Distance / Dihedral ConstraintのLambdaをStep開始時に0へ戻します。
    void ResetConstraintLambdas();

    // Sphere / Plane CollisionのParticle別Lambdaと、Sphereが外部へ返す1Step分Feedbackを初期化します。
    void ResetCollisionConstraintState();

    // Structural / Shear / 比較用Distance Bendingを含む全Distance ConstraintをXPBD式で解決します。
    void SolveDistanceConstraints(float deltaTime);

    // 隣接Triangle間の二面角を直接拘束するXPBD Bending Constraintです。
    void SolveDihedralConstraints(float deltaTime);

    // Sphere Collisionを C(x)=|x-center|-radius >= 0 の片側XPBD Constraintとして解決します。
    // DeltaLambdaからRigidBodyへ返す反作用Impulseも蓄積します。
    void SolveSphereCollisions(float deltaTime);

    // Plane Collisionを C(x)=dot(n,x)-offset-thickness >= 0 の片側XPBD Constraintとして解決します。
    void SolvePlaneCollisions(float deltaTime);

    // Constraint補正後のPosition差分からVelocityを再構築します。
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
