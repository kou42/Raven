#include <algorithm>
#include <cmath>
#include <limits>

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/Collision/BroadPhase.inl"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Solver/ContactSolver.h"

namespace Raven
{
namespace ph
{
namespace
{
// 睡眠中のRigidBodyを即座に実行対象へ戻します。
// 速度をそのまま残すと再起動時に古いSleep状態が悪影響を出すため、
// SleepTimerだけを確実に初期化します。
void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}

// 前フレームのManifoldと現在のManifoldが同じ衝突ペアかを判定します。
// 順序違いでも同一として扱い、接触情報の引き継ぎ元を探すために使います。
bool IsSamePair(const ContactManifold& a, const ContactManifold& b)
{
    return (a.A == b.A && a.B == b.B) || (a.A == b.B && a.B == b.A);
}

// 球体に対するレイキャストです。
// 既に球の内部にいる場合は fraction 0 のヒットとして扱い、法線は
// 中心方向、または退避用の既定方向から構成します。
bool RayCastSphere(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
    const math::Vec3& center, float radius, float& outFraction, math::Vec3& outNormal)
{
    const math::Vec3 m = origin - center;
    const float a = math::Vec3::Dot(direction, direction);
    if (a <= 1e-12f)
    {
        return false;
    }

    const float c = math::Vec3::Dot(m, m) - radius * radius;
    if (c <= 0.0f)
    {
        outFraction = 0.0f;
        const float lengthSq = m.LengthSq();
        outNormal = lengthSq > 1e-12f ? m / std::sqrt(lengthSq) : -direction.Normalized();
        return true;
    }

    const float b = math::Vec3::Dot(m, direction);
    const float discriminant = b * b - a * c;
    if (discriminant < 0.0f)
    {
        return false;
    }

    const float fraction = (-b - std::sqrt(discriminant)) / a;
    if (fraction < 0.0f || fraction > maxFraction)
    {
        return false;
    }

    outFraction = fraction;
    outNormal = (origin + direction * fraction - center).Normalized();
    return true;
}

// 平面コライダーに対するレイキャストです。
// 平行な場合は、レイが平面上にいるかどうかも判定して、接触扱いを返します。
bool RayCastPlane(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
    const TransformComponent& transform, const ColliderComponent& collider, float& outFraction,
    math::Vec3& outNormal)
{
    math::Vec3 normal = collider.PlaneNormal.Normalized();
    if (normal.LengthSq() <= 1e-12f)
    {
        return false;
    }

    const float denominator = math::Vec3::Dot(normal, direction);
    const float distance = math::Vec3::Dot(normal, origin - (transform.Position + collider.Offset));

    if (std::abs(denominator) <= 1e-8f)
    {
        if (std::abs(distance) > 1e-6f)
        {
            return false;
        }

        outFraction = 0.0f;
        outNormal = normal;
        return true;
    }

    const float fraction = -distance / denominator;
    if (fraction < 0.0f || fraction > maxFraction)
    {
        return false;
    }

    outFraction = fraction;
    outNormal = denominator < 0.0f ? normal : -normal;
    return true;
}

// 形状種別ごとに、共通のレイキャスト処理へ振り分けます。
// BoxはAABBへ変換した上で判定し、未対応型は false を返します。
bool RayCastCollider(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
    const TransformComponent& transform, const ColliderComponent& collider, float& outFraction,
    math::Vec3& outNormal)
{
    if (collider.Type == ColliderType::Sphere)
    {
        return RayCastSphere(origin, direction, maxFraction, transform.Position + collider.Offset,
            std::max(collider.Radius, 0.0f), outFraction, outNormal);
    }

    if (collider.Type == ColliderType::Box)
    {
        AABB bounds{};
        return ComputeColliderAABB(transform, collider, bounds) && bounds.RayCast(origin, direction,
            maxFraction, outFraction, &outNormal);
    }

    if (collider.Type == ColliderType::Plane)
    {
        return RayCastPlane(origin, direction, maxFraction, transform, collider, outFraction,
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

// 速度の元になる外力を集めます。
// Dynamic かつ Awake な剛体だけを更新し、静的オブジェクトや睡眠中の剛体は
// ここでは触りません。
void PhysicsWorld::ApplyForces(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody] : scene.View<TransformComponent, RigidBodyComponent>())
    {
        static_cast<void>(entity);
        static_cast<void>(transform);

        if (rigidBody.Type != BodyType::Dynamic || rigidBody.IsSleeping || rigidBody.InverseMass <= 0)
        {
            continue;
        }

        math::Vec3 acceleration{};
        if (rigidBody.UseGravity == true)
        {
            acceleration += m_Gravity;
        }
        acceleration += rigidBody.Force * rigidBody.InverseMass;

        rigidBody.LinearVelocity += acceleration * dt;
    }
}

// 速度のダンピングを適用します。
// 指数減衰ではなく、1ステップあたりの簡易減衰として扱っています。
void PhysicsWorld::IntegrateVelocities(Scene& scene, float dt)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type != BodyType::Dynamic || rigidBody.IsSleeping)
        {
            continue;
        }

        rigidBody.LinearVelocity *= 1.0f / (1.0f + std::max(rigidBody.LinearDamping, 0.0f) * dt);
    }
}

// 現在の速度を使って位置を進めます。
// Static は固定のまま、睡眠中の Dynamic もこの段階では動かしません。
void PhysicsWorld::IntegratePositions(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody] : scene.View<TransformComponent, RigidBodyComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type == BodyType::Static ||
            (rigidBody.Type == BodyType::Dynamic && rigidBody.IsSleeping))
        {
            continue;
        }

        transform.Position += rigidBody.LinearVelocity * dt;
    }
}

// Broad Phase で候補ペアを列挙し、実際の接触形状ごとに Narrow Phase を実行します。
// 直前フレームの Manifold は m_PreviousManifolds に退避し、後続処理で
// cached impulse の引き継ぎに利用します。
void PhysicsWorld::DetectCollisions(Scene& scene)
{
    m_PreviousManifolds = std::move(m_Manifolds);
    m_Manifolds.clear();

    std::vector<BroadPhasePair> pairs;
    m_BroadPhase.ComputePairs(scene, pairs);

    for (const auto& pair : pairs)
    {
        if (scene.IsEntityAlive(pair.A) == false || scene.IsEntityAlive(pair.B) == false)
        {
            continue;
        }

        auto* transformA = scene.TryGetComponent<TransformComponent>(pair.A.GetIndex());
        auto* transformB = scene.TryGetComponent<TransformComponent>(pair.B.GetIndex());
        auto* colliderA = scene.TryGetComponent<ColliderComponent>(pair.A.GetIndex());
        auto* colliderB = scene.TryGetComponent<ColliderComponent>(pair.B.GetIndex());
        if (transformA == nullptr || transformB == nullptr || colliderA == nullptr || colliderB == nullptr)
        {
            continue;
        }

        ContactManifold manifold{};
        bool generated = false;

        if (colliderA->Type == ColliderType::Sphere && colliderB->Type == ColliderType::Sphere)
        {
            generated = GenerateSphereSphereManifold(
                pair.A, *transformA, *colliderA, pair.B, *transformB, *colliderB, manifold);
        }
        else if (colliderA->Type == ColliderType::Sphere && colliderB->Type == ColliderType::Box)
        {
            generated = GenerateSphereBoxManifold(
                pair.A, *transformA, *colliderA, pair.B, *transformB, *colliderB, manifold);
        }
        else if (colliderA->Type == ColliderType::Box && colliderB->Type == ColliderType::Sphere)
        {
            generated = GenerateSphereBoxManifold(
                pair.B, *transformB, *colliderB, pair.A, *transformA, *colliderA, manifold);
        }
        else if (colliderA->Type == ColliderType::Box && colliderB->Type == ColliderType::Box)
        {
            generated = GenerateBoxBoxManifold(
                pair.A, *transformA, *colliderA, pair.B, *transformB, *colliderB, manifold);
        }

        if (generated == true)
        {
            m_Manifolds.push_back(manifold);
        }
    }

    // Plane は Broad Phase で拾いにくいので、球体だけ別ループで明示的に判定します。
    for (auto [sphereEntity, sphereTransform, sphereCollider] :
        scene.View<TransformComponent, ColliderComponent>())
    {
        if (sphereCollider.Type != ColliderType::Sphere)
        {
            continue;
        }

        for (auto [planeEntity, planeTransform, planeCollider] :
            scene.View<TransformComponent, ColliderComponent>())
        {
            if (planeCollider.Type != ColliderType::Plane || sphereEntity == planeEntity)
            {
                continue;
            }

            ContactManifold manifold{};
            if (GenerateSpherePlaneManifold(
                    sphereEntity, sphereTransform, sphereCollider, planeEntity, planeTransform,
                    planeCollider, manifold))
            {
                m_Manifolds.push_back(manifold);
            }
        }
    }

    // 直前フレームの接触情報を再利用して、Solver の warm start を効かせます。
    RestorePersistentContacts();

    // Narrow Phase 直後の形状統計を記録します。
    // ここでは impulse はまだ確定していないので、最大値の集計は Solver 後に別途行います。
    m_SolverDebugStatistics.ManifoldCount = static_cast<uint32_t>(m_Manifolds.size());
    for (const ContactManifold& manifold : m_Manifolds)
    {
        m_SolverDebugStatistics.ContactPointCount += static_cast<uint32_t>(manifold.PointCount);
        for (std::size_t i = 0; i < manifold.PointCount; ++i)
        {
            m_SolverDebugStatistics.MaxPenetration = std::max(
                m_SolverDebugStatistics.MaxPenetration, std::max(manifold.Points[i].Penetration, 0.0f));
        }
    }
}

// 前フレームの接触点が、現在の接触点にどれだけ継続しているかを判定します。
// ペアの継続、法線の継続、接触点の位置近接を満たした場合に、cached impulse を
// 現フレームへコピーします。
void PhysicsWorld::RestorePersistentContacts()
{
    constexpr float matchDistanceSq = 0.05f * 0.05f;
    constexpr float normalThreshold = 0.9f;

    for (auto& current : m_Manifolds)
    {
        if (current.IsTrigger || current.PointCount == 0)
        {
            continue;
        }

        const ContactManifold* previous = nullptr;
        for (const auto& candidate : m_PreviousManifolds)
        {
            if (candidate.IsTrigger == false && candidate.PointCount > 0 && IsSamePair(current, candidate))
            {
                const bool sameOrder = current.A == candidate.A && current.B == candidate.B;
                const math::Vec3 previousNormal = sameOrder ? candidate.Normal : -candidate.Normal;
                if (math::Vec3::Dot(current.Normal.Normalized(), previousNormal.Normalized()) >= normalThreshold)
                {
                    previous = &candidate;
                    break;
                }
            }
        }

        if (previous == nullptr)
        {
            continue;
        }

        // 同じ衝突面が継続しているため、Manifold と ContactPoint の両方を継続件数として数えます。
        ++m_SolverDebugStatistics.PersistentManifoldCount;

        const bool sameOrder = current.A == previous->A && current.B == previous->B;
        bool used[ContactManifold::MaxContactPointCount]{};

        for (std::size_t i = 0; i < current.PointCount; ++i)
        {
            std::size_t bestIndex = ContactManifold::MaxContactPointCount;
            float bestDistanceSq = matchDistanceSq;

            for (std::size_t j = 0; j < previous->PointCount; ++j)
            {
                if (used[j] == true)
                {
                    continue;
                }

                const float distanceSq =
                    (current.Points[i].Position - previous->Points[j].Position).LengthSq();
                if (distanceSq <= bestDistanceSq)
                {
                    bestDistanceSq = distanceSq;
                    bestIndex = j;
                }
            }

            if (bestIndex == ContactManifold::MaxContactPointCount)
            {
                continue;
            }

            used[bestIndex] = true;

            const auto& source = previous->Points[bestIndex];
            auto& target = current.Points[i];
            target.AccumulatedNormalImpulse = source.AccumulatedNormalImpulse;
            target.AccumulatedTangentImpulse = sameOrder ? source.AccumulatedTangentImpulse
                                                         : -source.AccumulatedTangentImpulse;
            target.CachedTangent = sameOrder ? source.CachedTangent : -source.CachedTangent;

            ++m_SolverDebugStatistics.PersistentContactPointCount;
        }

        // 代表接触点がまだ未初期化なら、前フレーム先頭点の cached impulse を使って
        // Solver の初期値として最低限の情報を残します。
        if (current.Points[0].AccumulatedNormalImpulse == 0.0f && previous->PointCount > 0)
        {
            current.Points[0].AccumulatedNormalImpulse = previous->Points[0].AccumulatedNormalImpulse;
            current.Points[0].AccumulatedTangentImpulse =
                sameOrder ? previous->Points[0].AccumulatedTangentImpulse
                          : -previous->Points[0].AccumulatedTangentImpulse;
            current.Points[0].CachedTangent = sameOrder ? previous->Points[0].CachedTangent
                                                        : -previous->Points[0].CachedTangent;
        }
    }
}

// Broad Phase で得た候補を使い、最も近いヒットだけを採用します。
// Plane は Broad Phase の外側にあるため、別途後段で比較します。
bool PhysicsWorld::RayCast(Scene& scene, const math::Vec3& origin, const math::Vec3& direction,
    float maxFraction, PhysicsRayCastHit& outHit)
{
    if (maxFraction < 0.0f || direction.LengthSq() <= 1e-12f)
    {
        return false;
    }

    bool hit = false;
    float closestFraction = maxFraction;
    PhysicsRayCastHit result{};

    m_BroadPhase.RayCast(scene, origin, direction, maxFraction,
        [&](Entity entity, uint32_t proxyIndex, float fraction, const math::Vec3& normal,
            float currentClosest) -> float
        {
            static_cast<void>(proxyIndex);
            static_cast<void>(fraction);
            static_cast<void>(normal);

            auto* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
            auto* collider = scene.TryGetComponent<ColliderComponent>(entity.GetIndex());
            if (transform == nullptr || collider == nullptr || collider->Type == ColliderType::Plane)
            {
                return currentClosest;
            }

            float candidateFraction = 0.0f;
            math::Vec3 candidateNormal{};
            if (RayCastCollider(origin, direction, currentClosest, *transform, *collider,
                    candidateFraction, candidateNormal) == false)
            {
                return currentClosest;
            }

            if (hit == false || candidateFraction < closestFraction)
            {
                hit = true;
                closestFraction = candidateFraction;
                result.HitEntity = entity;
                result.Fraction = candidateFraction;
                result.Point = origin + direction * candidateFraction;
                result.Normal = candidateNormal;
            }

            return closestFraction;
        });

    // Plane は全件走査し、Broad Phase 側で取れなかった近接ヒットも拾います。
    for (auto [entity, transform, collider] : scene.View<TransformComponent, ColliderComponent>())
    {
        if (collider.Type != ColliderType::Plane)
        {
            continue;
        }

        float candidateFraction = 0.0f;
        math::Vec3 candidateNormal{};
        if (RayCastPlane(origin, direction, closestFraction, transform, collider,
                candidateFraction, candidateNormal) &&
            (hit == false || candidateFraction < closestFraction))
        {
            hit = true;
            closestFraction = candidateFraction;
            result.HitEntity = entity;
            result.Fraction = candidateFraction;
            result.Point = origin + direction * candidateFraction;
            result.Normal = candidateNormal;
        }
    }

    if (hit == true)
    {
        outHit = result;
    }

    return hit;
}

// 指定 AABB と重なるコライダーを列挙します。
// Broad Phase で候補を絞り、実 AABB で最後に確認してから outEntities に積みます。
void PhysicsWorld::QueryAABB(Scene& scene, const AABB& queryBounds, std::vector<Entity>& outEntities)
{
    outEntities.clear();
    if (queryBounds.IsValid() == false)
    {
        return;
    }

    m_BroadPhase.QueryAABB(scene, queryBounds, [&](Entity entity, uint32_t proxyIndex) -> bool
    {
        static_cast<void>(proxyIndex);

        auto* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
        auto* collider = scene.TryGetComponent<ColliderComponent>(entity.GetIndex());
        if (transform == nullptr || collider == nullptr)
        {
            return true;
        }

        AABB bounds{};
        if (ComputeColliderAABB(*transform, *collider, bounds) && bounds.Overlaps(queryBounds))
        {
            outEntities.push_back(entity);
        }

        return true;
    });
}

// Solver で使用する cached impulse の量を、現在の Manifold から事前に数えます。
// warm start を無効化している場合は、値があっても実際の適用対象にはしません。
void PhysicsWorld::SolveCollisions(Scene& scene, float dt)
{
    if (m_SolverSettings.EnableWarmStart == true)
    {
        for (const ContactManifold& manifold : m_Manifolds)
        {
            if (manifold.IsTrigger == false && manifold.PointCount > 0 &&
                (manifold.Points[0].AccumulatedNormalImpulse > 0.0f ||
                    std::abs(manifold.Points[0].AccumulatedTangentImpulse) > 1.0e-8f))
            {
                ++m_SolverDebugStatistics.WarmStartedConstraintCount;
            }
        }
    }

    m_SolverDebugStatistics.VelocityIterations = std::max(m_SolverSettings.VelocityIterations, 1u);
    SolveContactManifolds(scene, m_Manifolds, dt, m_SolverSettings);
    UpdateSolverDebugStatisticsAfterSolve();
}

// Solver 実行後の最終 impulse を統計値へ反映します。
// 正規方向と接線方向で性質が異なるため、最大値の比較方法を分けています。
void PhysicsWorld::UpdateSolverDebugStatisticsAfterSolve()
{
    for (const ContactManifold& manifold : m_Manifolds)
    {
        for (std::size_t i = 0; i < manifold.PointCount; ++i)
        {
            const ContactPoint& point = manifold.Points[i];
            m_SolverDebugStatistics.MaxNormalImpulse = std::max(
                m_SolverDebugStatistics.MaxNormalImpulse, std::max(point.AccumulatedNormalImpulse, 0.0f));
            m_SolverDebugStatistics.MaxFrictionImpulse = std::max(
                m_SolverDebugStatistics.MaxFrictionImpulse, std::abs(point.AccumulatedTangentImpulse));
        }
    }
}

// 睡眠判定を更新します。
// 動いていない状態が閾値時間を超えたら眠らせ、睡眠中は速度を毎フレームゼロへ戻します。
void PhysicsWorld::UpdateSleeping(Scene& scene, float dt)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type != BodyType::Dynamic)
        {
            continue;
        }

        if (rigidBody.AllowSleep == false)
        {
            WakeRigidBody(rigidBody);
            continue;
        }

        if (rigidBody.IsSleeping == true)
        {
            rigidBody.LinearVelocity = math::Vec3{};
            continue;
        }

        const float threshold = std::max(rigidBody.SleepThreshold, 0.0f);
        if (rigidBody.LinearVelocity.LengthSq() <= threshold * threshold)
        {
            rigidBody.SleepTimer += dt;
            const float requiredTime = std::max(rigidBody.SleepTimeThreshold, 0.0f);
            if (rigidBody.SleepTimer >= requiredTime)
            {
                rigidBody.IsSleeping = true;
                rigidBody.LinearVelocity = math::Vec3{};
                rigidBody.SleepTimer = requiredTime;
            }
        }
        else
        {
            rigidBody.SleepTimer = 0.0f;
        }
    }
}

// 外力は各 Step の最後に使い切るため、次回へ持ち越さないよう初期化します。
void PhysicsWorld::ClearForces(Scene& scene)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);
        rigidBody.Force = math::Vec3{};
        rigidBody.Torque = math::Vec3{};
    }
}

// 物理的に有効な Dynamic rigid body にだけ force を加えます。
// 睡眠中の剛体は WakeUp 相当の処理で再起動します。
void PhysicsWorld::AddForce(Scene& scene, Entity entity, const math::Vec3& force)
{
    if (scene.IsEntityAlive(entity) == false)
    {
        return;
    }

    auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody == nullptr || rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0)
    {
        return;
    }

    WakeRigidBody(*rigidBody);
    rigidBody->Force += force;
}

// インパルスは瞬間的な速度変更として扱います。
// 質量の逆数を掛けて、速度へ直接反映します。
void PhysicsWorld::AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse)
{
    if (scene.IsEntityAlive(entity) == false)
    {
        return;
    }

    auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody == nullptr || rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0)
    {
        return;
    }

    WakeRigidBody(*rigidBody);
    rigidBody->LinearVelocity += impulse * rigidBody->InverseMass;
}

// 明示的に剛体を起こしたいときの入口です。
// Dynamic 以外は対象外なので、静的オブジェクトはそのままにします。
void PhysicsWorld::WakeUp(Scene& scene, Entity entity)
{
    if (scene.IsEntityAlive(entity) == false)
    {
        return;
    }

    auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody != nullptr && rigidBody->Type == BodyType::Dynamic)
    {
        WakeRigidBody(*rigidBody);
    }
}

// 1 回の Physics ステップの全体フローです。
// 外力の適用、速度と位置の更新、衝突検出、Solver、睡眠更新、force クリアまでを
// この順でまとめて実行します。
void PhysicsWorld::Step(Scene& scene, float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }

    // Statistics は毎 Step で必ずリセットし、前フレームの値を混ぜません。
    m_SolverDebugStatistics.Reset();

    ApplyForces(scene, dt);
    IntegrateVelocities(scene, dt);
    IntegratePositions(scene, dt);
    DetectCollisions(scene);
    SolveCollisions(scene, dt);
    UpdateSleeping(scene, dt);
    ClearForces(scene);
}
} // namespace ph
} // namespace Raven
