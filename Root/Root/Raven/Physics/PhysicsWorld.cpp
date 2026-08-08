#include <algorithm>
#include <cmath>
#include <limits>

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/RigidBodyDynamics.h"
#include "Raven/Physics/Collision/BroadPhase.inl"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/OBB.h"
#include "Raven/Physics/Solver/ContactSolver.h"

namespace Raven
{
namespace ph
{
namespace
{
// PhysicsWorld内ユーティリティ:
// 接触再利用やRayCast共通処理を局所化し、Step本体の見通しを保ちます。
void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}

// 接触ペアの順序が入れ替わっていても、同じ接触として扱えるようにします。
bool IsSamePair(const ContactManifold& a, const ContactManifold& b)
{
    return (a.A == b.A && a.B == b.B)
        || (a.A == b.B && a.B == b.A);
}

// 剛体側でQuaternion姿勢が初期化済みならそれを優先し、
// 未初期化ならTransformのEuler角から接触計算用の姿勢を作ります。
math::Quat GetContactOrientation(
    const TransformComponent& transform,
    const RigidBodyComponent* body)
{
    return (body && body->OrientationInitialized)
        ? body->Orientation.Normalized()
        : PhysicsOrientationFromEuler(transform.Rotation);
}

// ワールド接触点を剛体ローカルへ変換します。
// Persistent Contactで回転後も同じ接触位置を追跡するために使用します。
math::Vec3 ContactLocalAnchor(
    const TransformComponent& transform,
    const RigidBodyComponent* body,
    const math::Vec3& worldPoint)
{
    return GetContactOrientation(transform, body)
        .Conjugate()
        .Rotate(worldPoint - transform.Position);
}

// Feature IDが有効なら、接触ペアのEntity順序も考慮して同じ幾何特徴か判定します。
bool FeatureMatches(
    const ContactFeatureID& current,
    const ContactFeatureID& previous,
    bool sameOrder)
{
    if (!current.IsValid() || !previous.IsValid())
    {
        return false;
    }

    if (sameOrder)
    {
        return current == previous;
    }

    return current.TypeA == previous.TypeB
        && current.IndexA == previous.IndexB
        && current.TypeB == previous.TypeA
        && current.IndexB == previous.IndexA;
}

// Sphereに対するRayCastです。
// 二次方程式を解き、指定されたmaxFraction以内の最初の交点を返します。
bool RayCastSphere(
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    const math::Vec3& center,
    float radius,
    float& outFraction,
    math::Vec3& outNormal)
{
    const math::Vec3 m = origin - center;
    const float a = math::Vec3::Dot(direction, direction);
    if (a <= 1.0e-12f)
    {
        return false;
    }

    const float c = math::Vec3::Dot(m, m) - radius * radius;
    if (c <= 0.0f)
    {
        // Ray始点がSphere内部の場合はfraction=0として扱います。
        outFraction = 0.0f;
        const float lengthSquared = m.LengthSq();
        outNormal = lengthSquared > 1.0e-12f
            ? m / std::sqrt(lengthSquared)
            : -direction.Normalized();
        return true;
    }

    const float b = math::Vec3::Dot(m, direction);
    const float discriminant = b * b - a * c;
    if (discriminant < 0.0f)
    {
        return false;
    }

    outFraction = (-b - std::sqrt(discriminant)) / a;
    if (outFraction < 0.0f || outFraction > maxFraction)
    {
        return false;
    }

    outNormal = (origin + direction * outFraction - center).Normalized();
    return true;
}

// Planeに対するRayCastです。
// Planeは無限形状なのでBroad Phase Treeには依存せず直接判定できます。
bool RayCastPlane(
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    const TransformComponent& transform,
    const ColliderComponent& collider,
    float& outFraction,
    math::Vec3& outNormal)
{
    outNormal = collider.PlaneNormal.Normalized();
    if (outNormal.LengthSq() <= 1.0e-12f)
    {
        return false;
    }

    const float denominator = math::Vec3::Dot(outNormal, direction);
    const float distance = math::Vec3::Dot(
        outNormal,
        origin - (transform.Position + collider.Offset));

    if (std::abs(denominator) <= 1.0e-8f)
    {
        // RayがPlaneと平行で、かつPlane上にない場合は交差しません。
        if (std::abs(distance) > 1.0e-6f)
        {
            return false;
        }

        outFraction = 0.0f;
        return true;
    }

    outFraction = -distance / denominator;
    if (outFraction < 0.0f || outFraction > maxFraction)
    {
        return false;
    }

    // RayがPlaneの裏側から入る場合も、Ray側へ向く法線を返します。
    outNormal = denominator < 0.0f ? outNormal : -outNormal;
    return true;
}

// Collider種別ごとのRayCast実装へ振り分けます。
bool RayCastCollider(
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    const TransformComponent& transform,
    const ColliderComponent& collider,
    float& outFraction,
    math::Vec3& outNormal)
{
    if (collider.Type == ColliderType::Sphere)
    {
        return RayCastSphere(
            origin,
            direction,
            maxFraction,
            transform.Position + collider.Offset,
            std::max(collider.Radius, 0.0f),
            outFraction,
            outNormal);
    }

    if (collider.Type == ColliderType::Box)
    {
        OBB box{};
        return ComputeBoxOBB(transform, collider, box)
            && box.RayCast(origin, direction, maxFraction, outFraction, &outNormal);
    }

    if (collider.Type == ColliderType::Plane)
    {
        return RayCastPlane(
            origin,
            direction,
            maxFraction,
            transform,
            collider,
            outFraction,
            outNormal);
    }

    return false;
}
} // namespace

void PhysicsWorld::SetGravity(const math::Vec3& gravity)
{
    m_Gravity = gravity;
}

const math::Vec3& PhysicsWorld::GetGravity() const
{
    return m_Gravity;
}

// Dynamic剛体へ重力・Force・Torqueを反映します。
void PhysicsWorld::ApplyForces(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody, collider]
        : scene.View<TransformComponent, RigidBodyComponent, ColliderComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type != BodyType::Dynamic
            || rigidBody.IsSleeping
            || rigidBody.InverseMass <= 0.0f)
        {
            continue;
        }

        math::Vec3 acceleration{};
        if (rigidBody.UseGravity)
        {
            acceleration += m_Gravity;
        }

        acceleration += rigidBody.Force * rigidBody.InverseMass;
        rigidBody.LinearVelocity += acceleration * dt;

        // Torqueはワールド逆慣性テンソルを通して角加速度へ変換します。
        EnsurePhysicsOrientation(transform, rigidBody);
        rigidBody.AngularVelocity +=
            (ComputeWorldInverseInertia(&transform, &rigidBody, &collider) * rigidBody.Torque) * dt;
    }
}

// 線形・角速度へDampingを適用します。
void PhysicsWorld::IntegrateVelocities(Scene& scene, float dt)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type != BodyType::Dynamic || rigidBody.IsSleeping)
        {
            continue;
        }

        rigidBody.LinearVelocity *=
            1.0f / (1.0f + std::max(rigidBody.LinearDamping, 0.0f) * dt);
        rigidBody.AngularVelocity *=
            1.0f / (1.0f + std::max(rigidBody.AngularDamping, 0.0f) * dt);
    }
}

// 現在の速度をTransformへ積分します。
void PhysicsWorld::IntegratePositions(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody]
        : scene.View<TransformComponent, RigidBodyComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type == BodyType::Static
            || (rigidBody.Type == BodyType::Dynamic && rigidBody.IsSleeping))
        {
            continue;
        }

        transform.Position += rigidBody.LinearVelocity * dt;
        IntegratePhysicsOrientation(transform, rigidBody, dt);
    }
}

// Broad Phase候補から有限形状同士の接触を生成し、
// Treeへ登録しない無限Planeについては別経路でSphere/Boxとの接触を生成します。
void PhysicsWorld::DetectCollisions(Scene& scene)
{
    // 前フレームのManifoldはPersistent Contact/Warm Start用に退避します。
    m_PreviousManifolds = std::move(m_Manifolds);
    m_Manifolds.clear();

    std::vector<BroadPhasePair> pairs;
    m_BroadPhase.ComputePairs(scene, pairs);

    // Dynamic AABB Treeから得た有限形状同士の候補をNarrow Phaseへ振り分けます。
    for (const auto& pair : pairs)
    {
        if (!scene.IsEntityAlive(pair.A) || !scene.IsEntityAlive(pair.B))
        {
            continue;
        }

        auto* transformA = scene.TryGetComponent<TransformComponent>(pair.A.GetIndex());
        auto* transformB = scene.TryGetComponent<TransformComponent>(pair.B.GetIndex());
        auto* colliderA = scene.TryGetComponent<ColliderComponent>(pair.A.GetIndex());
        auto* colliderB = scene.TryGetComponent<ColliderComponent>(pair.B.GetIndex());

        if (!transformA || !transformB || !colliderA || !colliderB)
        {
            continue;
        }

        ContactManifold manifold{};
        bool generated = false;

        if (colliderA->Type == ColliderType::Sphere
            && colliderB->Type == ColliderType::Sphere)
        {
            generated = GenerateSphereSphereManifold(
                pair.A, *transformA, *colliderA,
                pair.B, *transformB, *colliderB,
                manifold);
        }
        else if (colliderA->Type == ColliderType::Sphere
            && colliderB->Type == ColliderType::Box)
        {
            generated = GenerateSphereBoxManifold(
                pair.A, *transformA, *colliderA,
                pair.B, *transformB, *colliderB,
                manifold);
        }
        else if (colliderA->Type == ColliderType::Box
            && colliderB->Type == ColliderType::Sphere)
        {
            // Sphere-Box APIはSphereをAとして受けるため、引数順だけ入れ替えます。
            generated = GenerateSphereBoxManifold(
                pair.B, *transformB, *colliderB,
                pair.A, *transformA, *colliderA,
                manifold);
        }
        else if (colliderA->Type == ColliderType::Box
            && colliderB->Type == ColliderType::Box)
        {
            generated = GenerateBoxBoxManifold(
                pair.A, *transformA, *colliderA,
                pair.B, *transformB, *colliderB,
                manifold);
        }

        if (generated)
        {
            m_Manifolds.push_back(manifold);
        }
    }

    // Planeは無限形状なのでDynamic AABB Treeへ登録しません。
    // そのため有限形状側を走査し、Sphere-Plane / Box-Planeを明示的に判定します。
    // 今回追加したBox-Planeも既存Sphere-Planeと同じ経路へ統合しています。
    for (auto [shapeEntity, shapeTransform, shapeCollider]
        : scene.View<TransformComponent, ColliderComponent>())
    {
        if (shapeCollider.Type != ColliderType::Sphere
            && shapeCollider.Type != ColliderType::Box)
        {
            continue;
        }

        for (auto [planeEntity, planeTransform, planeCollider]
            : scene.View<TransformComponent, ColliderComponent>())
        {
            if (planeCollider.Type != ColliderType::Plane || shapeEntity == planeEntity)
            {
                continue;
            }

            ContactManifold manifold{};
            bool generated = false;

            if (shapeCollider.Type == ColliderType::Sphere)
            {
                generated = GenerateSpherePlaneManifold(
                    shapeEntity, shapeTransform, shapeCollider,
                    planeEntity, planeTransform, planeCollider,
                    manifold);
            }
            else
            {
                generated = GenerateBoxPlaneManifold(
                    shapeEntity, shapeTransform, shapeCollider,
                    planeEntity, planeTransform, planeCollider,
                    manifold);
            }

            if (generated)
            {
                m_Manifolds.push_back(manifold);
            }
        }
    }

    RestorePersistentContacts(scene);

    // 今フレームの接触情報をSolverデバッグ統計へ反映します。
    m_SolverDebugStatistics.ManifoldCount = static_cast<uint32_t>(m_Manifolds.size());
    for (const auto& manifold : m_Manifolds)
    {
        m_SolverDebugStatistics.ContactPointCount += static_cast<uint32_t>(manifold.PointCount);

        for (std::size_t i = 0; i < manifold.PointCount; ++i)
        {
            m_SolverDebugStatistics.MaxPenetration = std::max(
                m_SolverDebugStatistics.MaxPenetration,
                std::max(manifold.Points[i].Penetration, 0.0f));
        }
    }
}

// 前フレームの接触Impulseを現在の接触へ引き継ぎ、Warm Startを成立させます。
void PhysicsWorld::RestorePersistentContacts(Scene& scene)
{
    constexpr float WorldDistanceSq = 0.05f * 0.05f;
    constexpr float LocalAnchorDistanceSq = 0.075f * 0.075f;
    constexpr float NormalThreshold = 0.9f;

    for (auto& current : m_Manifolds)
    {
        if (current.IsTrigger || current.PointCount == 0)
        {
            continue;
        }

        const ContactManifold* previous = nullptr;
        bool sameOrder = true;

        // 同じEntityペアの前フレームManifoldを探します。
        for (const auto& candidate : m_PreviousManifolds)
        {
            if (candidate.IsTrigger
                || candidate.PointCount == 0
                || !IsSamePair(current, candidate))
            {
                continue;
            }

            previous = &candidate;
            sameOrder = current.A == candidate.A && current.B == candidate.B;
            break;
        }

        if (!previous)
        {
            continue;
        }

        const auto* transformA = scene.TryGetComponent<TransformComponent>(current.A.GetIndex());
        const auto* transformB = scene.TryGetComponent<TransformComponent>(current.B.GetIndex());
        if (!transformA || !transformB)
        {
            continue;
        }

        const auto* bodyA = scene.TryGetComponent<RigidBodyComponent>(current.A.GetIndex());
        const auto* bodyB = scene.TryGetComponent<RigidBodyComponent>(current.B.GetIndex());

        // 法線が大きく変化した接触は、同一接触としてImpulseを再利用しません。
        const math::Vec3 previousNormal = sameOrder ? previous->Normal : -previous->Normal;
        if (math::Vec3::Dot(current.Normal, previousNormal) < NormalThreshold)
        {
            continue;
        }

        for (std::size_t i = 0; i < current.PointCount; ++i)
        {
            auto& currentPoint = current.Points[i];

            const math::Vec3 localAnchorA =
                ContactLocalAnchor(*transformA, bodyA, currentPoint.Position);
            const math::Vec3 localAnchorB =
                ContactLocalAnchor(*transformB, bodyB, currentPoint.Position);

            const ContactPoint* best = nullptr;
            float bestDistanceSquared = std::numeric_limits<float>::max();

            for (std::size_t j = 0; j < previous->PointCount; ++j)
            {
                const auto& previousPoint = previous->Points[j];

                // Feature IDが一致する場合は最も信頼できる同一接触として優先します。
                if (FeatureMatches(currentPoint.Feature, previousPoint.Feature, sameOrder))
                {
                    best = &previousPoint;
                    break;
                }

                // Feature IDが使えない接触ではワールド接触点の近さをfallbackにします。
                const float distanceSquared =
                    (currentPoint.Position - previousPoint.Position).LengthSq();
                if (distanceSquared < bestDistanceSquared
                    && distanceSquared <= WorldDistanceSq)
                {
                    best = &previousPoint;
                    bestDistanceSquared = distanceSquared;
                }
            }

            if (best)
            {
                currentPoint.AccumulatedNormalImpulse = best->AccumulatedNormalImpulse;
                currentPoint.AccumulatedTangentImpulse = best->AccumulatedTangentImpulse;
                currentPoint.CachedTangent = best->CachedTangent;
            }

            // SolverのPosition Constraintが現在姿勢から分離量を再評価できるよう、
            // ローカルアンカーと初期分離量を毎フレーム保存します。
            currentPoint.LocalAnchorA = localAnchorA;
            currentPoint.LocalAnchorB = localAnchorB;
            currentPoint.InitialSeparation = -currentPoint.Penetration;
            currentPoint.PositionAnchorsInitialized = true;
        }
    }
}

// 1フレーム分の物理シミュレーションを進めます。
// この順序は現在の挙動を維持するため変更していません。
void PhysicsWorld::Step(Scene& scene, float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }

    m_SolverDebugStatistics = {};

    ApplyForces(scene, dt);
    IntegrateVelocities(scene, dt);
    IntegratePositions(scene, dt);

    // Transform更新後のAABBをBroad Phaseへ反映してから接触候補を取得します。
    m_BroadPhase.Update(scene, dt);
    DetectCollisions(scene);

    SolveContactManifolds(
        scene,
        m_Manifolds,
        dt,
        m_SolverSettings,
        &m_SolverDebugStatistics);

    UpdateSleeping(scene, dt);
    ClearAccumulators(scene);
}

// Force/Torqueは1stepだけ有効なので、Step末尾で必ずクリアします。
void PhysicsWorld::ClearAccumulators(Scene& scene)
{
    for (auto [entity, body] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);
        body.Force = {};
        body.Torque = {};
    }
}

// 一定時間十分に低速なDynamic BodyをSleepingへ移行します。
void PhysicsWorld::UpdateSleeping(Scene& scene, float dt)
{
    for (auto [entity, body] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);

        if (body.Type != BodyType::Dynamic || !body.AllowSleep)
        {
            body.SleepTimer = 0.0f;
            continue;
        }

        if (body.LinearVelocity.LengthSq() < m_SleepLinearThreshold * m_SleepLinearThreshold
            && body.AngularVelocity.LengthSq() < m_SleepAngularThreshold * m_SleepAngularThreshold)
        {
            body.SleepTimer += dt;

            if (body.SleepTimer >= m_TimeToSleep)
            {
                body.IsSleeping = true;
                body.LinearVelocity = {};
                body.AngularVelocity = {};
            }
        }
        else
        {
            body.SleepTimer = 0.0f;
            body.IsSleeping = false;
        }
    }
}

void PhysicsWorld::AddForce(Scene& scene, Entity entity, const math::Vec3& force)
{
    if (auto* body = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex()))
    {
        body->Force += force;
        WakeRigidBody(*body);
    }
}

void PhysicsWorld::AddTorque(Scene& scene, Entity entity, const math::Vec3& torque)
{
    if (auto* body = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex()))
    {
        body->Torque += torque;
        WakeRigidBody(*body);
    }
}

void PhysicsWorld::AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse)
{
    auto* body = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (!body || body->Type != BodyType::Dynamic || body->InverseMass <= 0.0f)
    {
        return;
    }

    body->LinearVelocity += impulse * body->InverseMass;
    WakeRigidBody(*body);
}

// 重心以外へのImpulseは並進速度だけでなく角速度も変化させます。
void PhysicsWorld::AddImpulseAtPoint(
    Scene& scene,
    Entity entity,
    const math::Vec3& impulse,
    const math::Vec3& point)
{
    auto* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
    auto* body = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    auto* collider = scene.TryGetComponent<ColliderComponent>(entity.GetIndex());

    if (!transform || !body || body->Type != BodyType::Dynamic || body->InverseMass <= 0.0f)
    {
        return;
    }

    EnsurePhysicsOrientation(*transform, *body);
    body->LinearVelocity += impulse * body->InverseMass;
    body->AngularVelocity += ComputeWorldInverseInertia(transform, body, collider)
        * math::Vec3::Cross(point - transform->Position, impulse);
    WakeRigidBody(*body);
}

// 重心以外へのForceはTorqueも同時に生成します。
void PhysicsWorld::AddForceAtPoint(
    Scene& scene,
    Entity entity,
    const math::Vec3& force,
    const math::Vec3& point)
{
    auto* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
    auto* body = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());

    if (!transform || !body || body->Type != BodyType::Dynamic)
    {
        return;
    }

    body->Force += force;
    body->Torque += math::Vec3::Cross(point - transform->Position, force);
    WakeRigidBody(*body);
}

// シーン内のColliderを走査し、最も近いRay hitを返します。
bool PhysicsWorld::RayCast(
    Scene& scene,
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    PhysicsRayCastHit& outHit) const
{
    outHit = {};

    if (direction.LengthSq() <= 1.0e-12f || maxFraction < 0.0f)
    {
        return false;
    }

    float closest = maxFraction;

    for (auto [entity, transform, collider]
        : scene.View<TransformComponent, ColliderComponent>())
    {
        float fraction = 0.0f;
        math::Vec3 normal{};

        if (!RayCastCollider(
            origin,
            direction,
            closest,
            transform,
            collider,
            fraction,
            normal))
        {
            continue;
        }

        closest = fraction;
        outHit.EntityHit = entity;
        outHit.Fraction = fraction;
        outHit.Point = origin + direction * fraction;
        outHit.Normal = normal;
    }

    return static_cast<bool>(outHit.EntityHit);
}

const std::vector<ContactManifold>& PhysicsWorld::GetContactManifolds() const
{
    return m_Manifolds;
}

const SolverDebugStatistics& PhysicsWorld::GetSolverDebugStatistics() const
{
    return m_SolverDebugStatistics;
}

void PhysicsWorld::SetSolverSettings(const ContactSolverSettings& settings)
{
    m_SolverSettings = settings;
}

const ContactSolverSettings& PhysicsWorld::GetSolverSettings() const
{
    return m_SolverSettings;
}

void PhysicsWorld::SetSleepSettings(
    float linearThreshold,
    float angularThreshold,
    float timeToSleep)
{
    m_SleepLinearThreshold = std::max(linearThreshold, 0.0f);
    m_SleepAngularThreshold = std::max(angularThreshold, 0.0f);
    m_TimeToSleep = std::max(timeToSleep, 0.0f);
}

} // namespace ph
} // namespace Raven
