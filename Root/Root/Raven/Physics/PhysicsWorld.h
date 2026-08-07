#pragma once

#include <cstdint>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Contact.h"
#include "Raven/Physics/Collision/BroadPhase.h"
#include "Raven/Physics/Solver/ContactSolver.h"

namespace Raven
{
class Entity;
class Scene;

namespace ph
{

struct PhysicsRayCastHit
{
    Entity HitEntity{};
    math::Vec3 Point{};
    math::Vec3 Normal{};
    float Fraction = 0.0f;
};

// ============================================================================
// PhysicsSolverDebugStatistics
// ============================================================================
// 1回の PhysicsWorld::Step() で集計される診断値です。
// ここには「何個の接触があったか」「どれだけ接触が継続したか」
// 「warm start が何件使えたか」「最大のめり込みや impulse はどれか」
// といった、Solver の挙動を UI やテストから確認するための情報を入れます。
// 毎 Step の先頭で必ず Reset() され、前フレームの値は持ち越しません。
struct PhysicsSolverDebugStatistics
{
    // Narrow Phase で検出された Manifold の総数です。
    uint32_t ManifoldCount = 0;
    // Manifold 内に存在した接触点の総数です。
    uint32_t ContactPointCount = 0;

    // 前 Step の Manifold から cached impulse を引き継げた接触数です。
    uint32_t PersistentManifoldCount = 0;
    // 前 Step の接触点と位置が近く、実際に再利用できた接触点数です。
    uint32_t PersistentContactPointCount = 0;

    // Solver 開始時点で非ゼロの cached impulse を持っていた代表 Constraint 数です。
    uint32_t WarmStartedConstraintCount = 0;

    // 実際に使用した Velocity Solver の反復回数です。
    uint32_t VelocityIterations = 0;

    // 現フレームの最大めり込み深さです。
    float MaxPenetration = 0.0f;
    // Solver が収束させた正規方向 impulse の最大値です。
    float MaxNormalImpulse = 0.0f;
    // Solver が収束させた摩擦 impulse の最大値です。
    float MaxFrictionImpulse = 0.0f;

    void Reset()
    {
        *this = PhysicsSolverDebugStatistics{};
    }
};

class PhysicsWorld
{
public:
    // 重力を設定します。全 Dynamic rigid body の加速度計算で使われます。
    void SetGravity(const math::Vec3& gravity);
    // 現在の重力を返します。
    const math::Vec3& GetGravity() const;
    // 1 フレーム分の物理更新をまとめて実行します。
    void Step(Scene& scene, float fixedDeltaTime);

    // Solver の設定値を差し替えます。
    void SetSolverSettings(const ContactSolverSettings& settings) { m_SolverSettings = settings; }
    ContactSolverSettings& GetSolverSettings() { return m_SolverSettings; }
    const ContactSolverSettings& GetSolverSettings() const { return m_SolverSettings; }

    // 直近 Step の Solver 統計を読み出します。
    const PhysicsSolverDebugStatistics& GetSolverDebugStatistics() const
    {
        return m_SolverDebugStatistics;
    }

    // 外力、インパルス、速度、位置などの直接操作 API です。
    void AddForce(Scene& scene, Entity entity, const math::Vec3& force);
    void AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse);
    void AddTorque(Scene& scene, Entity entity, const math::Vec3& torque);
    void SetLinearVelocity(Scene& scene, Entity entity, const math::Vec3& velocity);
    math::Vec3 GetLinearVelocity(const Scene& scene, Entity entity) const;
    void Teleport(Scene& scene, Entity entity, const math::Vec3& position);
    void MovePosition(Scene& scene, Entity entity, const math::Vec3& position);
    void WakeUp(Scene& scene, Entity entity);

    // レイキャストと AABB クエリです。デバッグ表示や選択処理で使います。
    bool RayCast(Scene& scene, const math::Vec3& origin, const math::Vec3& direction,
        float maxFraction, PhysicsRayCastHit& outHit);
    void QueryAABB(Scene& scene, const AABB& queryBounds, std::vector<Entity>& outEntities);

    // 現在の接触候補を外部から確認するための読み出し口です。
    const std::vector<ContactManifold>& GetContactManifolds() const { return m_Manifolds; }
    const BroadPhase& GetBroadPhase() const { return m_BroadPhase; }

    // 直近のDetectCollisions()で実際にBroad Phaseから生成された候補Pairです。
    // Narrow Phaseで弾かれたPairも含むため、Broad Phaseのfalse positive確認にも使えます。
    const std::vector<BroadPhasePair>& GetBroadPhasePairs() const { return m_BroadPhasePairs; }

private:
    // Step の中で順番に呼ばれる内部処理です。
    void ApplyForces(Scene& scene, float dt);
    void IntegrateVelocities(Scene& scene, float dt);
    void IntegratePositions(Scene& scene, float dt);
    void DetectCollisions(Scene& scene);
    void RestorePersistentContacts();
    void SolveCollisions(Scene& scene, float dt);
    void UpdateSolverDebugStatisticsAfterSolve();
    void UpdateSleeping(Scene& scene, float dt);
    void ClearForces(Scene& scene);

private:
    // 既定の重力は地球標準近傍値です。
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
    BroadPhase m_BroadPhase;
    ContactSolverSettings m_SolverSettings{};
    PhysicsSolverDebugStatistics m_SolverDebugStatistics{};

    // 現フレームでBroad Phaseが生成した候補PairのSnapshotです。
    // DebugRenderer側では再計算せず、この結果をそのまま可視化します。
    std::vector<BroadPhasePair> m_BroadPhasePairs;

    // 現フレームと前フレームの接触情報を保持します。
    std::vector<ContactManifold> m_Manifolds;
    std::vector<ContactManifold> m_PreviousManifolds;
};

} // namespace ph
} // namespace Raven
