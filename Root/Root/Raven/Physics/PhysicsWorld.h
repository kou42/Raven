#pragma once

#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Contact.h"

namespace Raven
{

class Entity;
class Scene;

namespace ph // 物理演算のための名前空間
{

// ============================================================================
// PhysicsWorld
// ============================================================================
// Scene内に存在するRigidBodyComponentとColliderComponentをまとめて更新する
// 物理ワールドです。
//
// 現在の責務
//   1. 重力・外力から速度を更新する
//   2. 速度からTransformの位置を更新する
//   3. SphereとPlaneのContactを生成する
//   4. ContactSolverへ接触解決を委譲する
//   5. Damping、Sleep、Forceクリアを管理する
class PhysicsWorld
{
public:
    void SetGravity(const math::Vec3& gravity);
    const math::Vec3& GetGravity() const;

    // 固定タイムステップ1回分の物理更新を行います。
    void Step(Scene& scene, float fixedDeltaTime);

    void AddForce(Scene& scene, Entity entity, const math::Vec3& force);
    void AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse);
    void AddTorque(Scene& scene, Entity entity, const math::Vec3& torque);

    void SetLinearVelocity(Scene& scene, Entity entity, const math::Vec3& velocity);
    math::Vec3 GetLinearVelocity(const Scene& scene, Entity entity) const;

    void Teleport(Scene& scene, Entity entity, const math::Vec3& position);
    void MovePosition(Scene& scene, Entity entity, const math::Vec3& position);
    void WakeUp(Scene& scene, Entity entity);

    // 直前のPhysics Stepで生成されたContactを読み取ります。
    // デバッグ描画、接触イベント生成、テストなどで利用できます。
    // 次のStepが始まると内容は再生成されるため、参照を長期間保持しないでください。
    const std::vector<Contact>& GetContacts() const
    {
        return m_Contacts;
    }

private:
    void ApplyForces(Scene& scene, float dt);
    void IntegrateVelocities(Scene& scene, float dt);
    void IntegratePositions(Scene& scene, float dt);

    void DetectCollisions(Scene& scene);
    void SolveCollisions(Scene& scene, float dt);

    void UpdateSleeping(Scene& scene, float dt);
    void ClearForces(Scene& scene);

private:
    math::Vec3 m_Gravity{ 0.0f, -9.80665f, 0.0f };

    // Narrow Phaseで生成された、そのStep限りの接触情報です。
    // 現段階ではSphere-Plane Contactだけが格納されます。
    std::vector<Contact> m_Contacts;
};

} // namespace ph

} // namespace Raven
