#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/SoftBody/SoftBodyParticle.h"
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
};

// ============================================================================
// Static Sphere Collision Constraint
// ============================================================================
// SoftBody側で扱う静的Sphere Colliderです。
// 現段階ではCollider自身は動かず、貫通したParticleだけをSphere表面の外側へ射影します。
// RigidBodyとの双方向Impulse連成は、この一方向Constraintが安定した後に追加する想定です。
struct SoftBodySphereCollider
{
    math::Vec3 Center{};
    float Radius = 0.5f;
};

// ============================================================================
// Static Plane Collision Constraint
// ============================================================================
// Plane式は dot(Normal, x) - Offset = 0 とします。
// Normal側を外側とみなし、signed distanceがCollisionThickness未満のParticleを外側へ押し戻します。
// NormalはAdd/Set時に正規化し、ゼロベクトルが渡された場合は+Yを安全なfallbackとして使用します。
struct SoftBodyPlaneCollider
{
    math::Vec3 Normal{ 0.0f, 1.0f, 0.0f };
    float Offset = 0.0f;
};

// ============================================================================
// Soft Body Solver
// ============================================================================
// RigidBodyのContactSolverとは分離した、Particle + Constraint用のXPBD Solverです。
//
// 1Stepの基本順序:
//   1. 重力を積分してParticleの予測位置を作る
//   2. XPBD Distance Constraintを反復解決する
//   3. 同じ反復内でSphere / Plane Collisionを解決する
//   4. 最終PositionとStep開始時PositionからVelocityを再構築する
//
// CollisionもConstraint反復の中へ含めることで、Distance ConstraintがParticleをCollider内部へ
// 戻した場合でも同一Step内で再度押し出され、ClothがCollider形状へ沿って収束しやすくなります。
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

    // 静的Sphere Colliderを追加します。
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
    const std::vector<SoftBodySphereCollider>& GetSphereColliders() const { return m_SphereColliders; }
    const std::vector<SoftBodyPlaneCollider>& GetPlaneColliders() const { return m_PlaneColliders; }

private:
    // Step開始時の位置をPreviousPositionへ保存し、重力とVelocityから予測位置を作ります。
    void PredictPositions(float deltaTime);

    // Lambdaは同一Step内のSolver iteration間では蓄積しますが、現在はStepを跨いでWarm Startしません。
    void ResetConstraintLambdas();

    // Structural / Shear / Bendingを含む全Distance ConstraintをXPBD式で解決します。
    void SolveDistanceConstraints(float deltaTime);

    // 静的Sphere内部へ入ったParticleをSphere表面外へ射影します。
    void SolveSphereCollisions();

    // Planeの許容側より内側へ入ったParticleをNormal方向へ射影します。
    void SolvePlaneCollisions();

    // Constraint補正後のPosition差分からVelocityを再構築します。
    void UpdateVelocities(float deltaTime);

private:
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
    SoftBodySolverSettings m_Settings{};
    std::vector<SoftBodyParticle> m_Particles;
    std::vector<XPBDDistanceConstraint> m_DistanceConstraints;
    std::vector<SoftBodySphereCollider> m_SphereColliders;
    std::vector<SoftBodyPlaneCollider> m_PlaneColliders;
};

} // namespace ph
} // namespace Raven
