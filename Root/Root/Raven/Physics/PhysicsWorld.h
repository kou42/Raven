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
    // RayCastで最終的に採用された最短ヒット情報です。
    // Fractionは origin + direction * Fraction で交点を再構築できます。
    Entity HitEntity{};
    math::Vec3 Point{};
    math::Vec3 Normal{};
    float Fraction = 0.0f;
};

// ============================================================================
// RayCast Query Filter
// ============================================================================
// RayCastの「どのBody/Shapeを候補にするか」を呼び出し側から指定します。
//
// 重要:
// フィルタは「最短Hitを求めた後」に適用するのではなく、各候補を実形状判定する前に
// 適用します。これにより、例えば床Planeを除外したMouse Pickingで、床の奥にある
// Dynamic Sphere/Boxまで正しく探索できます。
//
// 現在のデフォルトはMouse Pickingで最もよく使う設定に合わせ、
// Dynamic Bodyのみ・Plane除外です。全Colliderを対象にしたい用途では All() を使えます。
struct PhysicsRayCastFilter
{
    bool IncludeStatic = false;
    bool IncludeKinematic = false;
    bool IncludeDynamic = true;
    bool IncludePlanes = false;

    static PhysicsRayCastFilter All()
    {
        PhysicsRayCastFilter filter{};
        filter.IncludeStatic = true;
        filter.IncludeKinematic = true;
        filter.IncludeDynamic = true;
        filter.IncludePlanes = true;
        return filter;
    }
};

struct PhysicsSolverDebugStatistics
{
    // Manifold/Point数はNarrow Phaseの接触量を示します。
    uint32_t ManifoldCount = 0;
    uint32_t ContactPointCount = 0;
    // Persistent系は前フレーム接触の再利用率を示し、値が高いほど安定傾向です。
    uint32_t PersistentManifoldCount = 0;
    uint32_t PersistentContactPointCount = 0;
    // Warm Started制約数は初期インパルス再利用の対象数です。
    uint32_t WarmStartedConstraintCount = 0;
    // 実際に走った反復回数（下限1）を記録します。
    uint32_t VelocityIterations = 0;
    // 収束状態の目安として最大貫通/最大法線インパルス/最大摩擦インパルスを保持します。
    float MaxPenetration = 0.0f;
    float MaxNormalImpulse = 0.0f;
    float MaxFrictionImpulse = 0.0f;

    void Reset() { *this = PhysicsSolverDebugStatistics{}; }
};

class PhysicsWorld
{
public:
    // 固定ステップシミュレーション本体。呼び出し側は一定dtで更新する想定です。
    void SetGravity(const math::Vec3& gravity);
    const math::Vec3& GetGravity() const;
    void Step(Scene& scene, float fixedDeltaTime);

    void SetSolverSettings(const ContactSolverSettings& settings) { m_SolverSettings = settings; }
    ContactSolverSettings& GetSolverSettings() { return m_SolverSettings; }
    const ContactSolverSettings& GetSolverSettings() const { return m_SolverSettings; }
    const PhysicsSolverDebugStatistics& GetSolverDebugStatistics() const { return m_SolverDebugStatistics; }

    // ========================================================================
    // 剛体制御用API
    // ========================================================================
    // Force / Torque は次の Step まで蓄積され、Impulse は呼び出した瞬間に速度へ反映されます。
    void AddForce(Scene& scene, Entity entity, const math::Vec3& force);
    void AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse);
    void AddTorque(Scene& scene, Entity entity, const math::Vec3& torque);

    // worldPoint はワールド座標です。重心から外れた位置に Impulse を与えると、
    // r × J による角 Impulse も同時に発生します。
    void AddImpulseAtPoint(Scene& scene, Entity entity, const math::Vec3& impulse,
        const math::Vec3& worldPoint);

    void SetLinearVelocity(Scene& scene, Entity entity, const math::Vec3& velocity);
    math::Vec3 GetLinearVelocity(const Scene& scene, Entity entity) const;
    void Teleport(Scene& scene, Entity entity, const math::Vec3& position);
    void MovePosition(Scene& scene, Entity entity, const math::Vec3& position);
    void WakeUp(Scene& scene, Entity entity);

    // デフォルトRayCastはDynamic Bodyのみ・Plane除外です。
    // Mouse Pickingでは床などに探索を遮られず、奥のDynamic Colliderを選択できます。
    bool RayCast(Scene& scene, const math::Vec3& origin, const math::Vec3& direction,
        float maxFraction, PhysicsRayCastHit& outHit);

    // 用途ごとにBody種別 / Plane有無を明示したい場合はこちらを使用します。
    // フィルタは候補選択前に適用されるため、除外Shapeが手前にあっても探索を継続します。
    bool RayCast(Scene& scene, const math::Vec3& origin, const math::Vec3& direction,
        float maxFraction, const PhysicsRayCastFilter& filter, PhysicsRayCastHit& outHit);

    // QueryAABBはBroadPhase候補を使いつつ、実AABB重なりで最終フィルタします。
    void QueryAABB(Scene& scene, const AABB& queryBounds, std::vector<Entity>& outEntities);

    const std::vector<ContactManifold>& GetContactManifolds() const { return m_Manifolds; }
    const BroadPhase& GetBroadPhase() const { return m_BroadPhase; }
    const std::vector<BroadPhasePair>& GetBroadPhasePairs() const { return m_BroadPhase.GetLastPairs(); }

private:
    // Step内部フェーズ。順序を変えると挙動が変わるため責務を分離しています。
    void ApplyForces(Scene& scene, float dt);
    void IntegrateVelocities(Scene& scene, float dt);
    void IntegratePositions(Scene& scene, float dt);
    void DetectCollisions(Scene& scene);
    void RestorePersistentContacts(Scene& scene);
    void SolveCollisions(Scene& scene, float dt);
    void UpdateSolverDebugStatisticsAfterSolve();
    void UpdateSleeping(Scene& scene, float dt);
    void ClearForces(Scene& scene);

private:
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };
    BroadPhase m_BroadPhase;
    ContactSolverSettings m_SolverSettings{};
    PhysicsSolverDebugStatistics m_SolverDebugStatistics{};
    std::vector<ContactManifold> m_Manifolds;
    std::vector<ContactManifold> m_PreviousManifolds;
};

} // namespace ph
} // namespace Raven
