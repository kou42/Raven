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
// 次のシミュレーションステップで参加するように、剛体を起床させます。
void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}

// 接触ペアの順序が入れ替わっていても、同じ接触として扱えるようにします。
bool IsSamePair(const ContactManifold& a, const ContactManifold& b)
{
    return (a.A == b.A && a.B == b.B) || (a.A == b.B && a.B == b.A);
}

// 剛体に明示的な回転がある場合はその向きを使用し、
// そうでない場合は Transform のオイラー角から接触向きを求めます。
math::Quat GetContactOrientation(
    const TransformComponent& transform,
    const RigidBodyComponent* body)
{
    return (body && body->OrientationInitialized)
        ? body->Orientation.Normalized()
        : PhysicsOrientationFromEuler(transform.Rotation);
}

// ワールド空間の接触点をコライダーのローカル空間へ変換し、
// 回転や微小な移動後でも永続接触を正しく対応付けできるようにします。
math::Vec3 ContactLocalAnchor(
    const TransformComponent& transform,
    const RigidBodyComponent* body,
    const math::Vec3& worldPoint)
{
    return GetContactOrientation(transform, body)
        .Conjugate()
        .Rotate(worldPoint - transform.Position);
}

// 2つの接触特徴量が同じ幾何特徴の組み合わせを指しているかを確認します。
// これにより、回転しても Warm Start 用の接触点を安定して再利用できます。
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

    return current.TypeA == previous.TypeB &&
           current.IndexA == previous.IndexB &&
           current.TypeB == previous.TypeA &&
           current.IndexB == previous.IndexA;
}

// 球体とレイの交差方程式を解き、レイ上で最も近いヒット位置を返します。
bool RayCastSphere(
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    const math::Vec3& center,
    float radius,
    float& outFraction,
    math::Vec3& outNormal)
{
    // 二次方程式 a*t^2 + 2b*t + c = 0 を解いて最短交点を選択します。
    // tは origin + direction * t のパラメータです。
    const math::Vec3 m = origin - center;
    const float a = math::Vec3::Dot(direction, direction);

    if (a <= 1e-12f)
    {
        return false;
    }

    const float c = math::Vec3::Dot(m, m) - radius * radius;
    if (c <= 0.0f)
    {
        // 始点が球内部にある場合は即ヒット(0)を返し、法線は安定方向を選びます。
        outFraction = 0.0f;
        const float ls = m.LengthSq();
        outNormal = ls > 1e-12f ? m / std::sqrt(ls) : -direction.Normalized();
        return true;
    }

    const float b = math::Vec3::Dot(m, direction);
    const float disc = b * b - a * c;
    if (disc < 0.0f)
    {
        return false;
    }

    const float f = (-b - std::sqrt(disc)) / a;
    if (f < 0.0f || f > maxFraction)
    {
        return false;
    }

    outFraction = f;
    outNormal = (origin + direction * f - center).Normalized();
    return true;
}

// 平面コライダーに対して、平面方程式を用いてワールド空間上のレイ交差を求めます。
bool RayCastPlane(
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    const TransformComponent& transform,
    const ColliderComponent& collider,
    float& outFraction,
    math::Vec3& outNormal)
{
    math::Vec3 n = collider.PlaneNormal.Normalized();
    if (n.LengthSq() <= 1e-12f)
    {
        return false;
    }

    const float d = math::Vec3::Dot(n, direction);
    const float distance = math::Vec3::Dot(n, origin - (transform.Position + collider.Offset));

    if (std::abs(d) <= 1e-8f)
    {
        // レイと平面がほぼ平行: レイ始点が平面上でなければヒットしません。
        if (std::abs(distance) > 1e-6f)
        {
            return false;
        }

        outFraction = 0.0f;
        outNormal = n;
        return true;
    }

    const float f = -distance / d;
    if (f < 0.0f || f > maxFraction)
    {
        return false;
    }

    outFraction = f;
    outNormal = d < 0.0f ? n : -n;
    return true;
}

// コライダーの形状に応じて、適切なレイキャスト処理へ振り分けます。
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
        return RayCastSphere(origin, direction, maxFraction, transform.Position + collider.Offset,
            std::max(collider.Radius, 0.0f), outFraction, outNormal);
    }

    if (collider.Type == ColliderType::Box)
    {
        OBB obb{};
        return ComputeBoxOBB(transform, collider, obb) && obb.RayCast(origin, direction, maxFraction, outFraction, &outNormal);
    }

    if (collider.Type == ColliderType::Plane)
    {
        return RayCastPlane(origin, direction, maxFraction, transform, collider, outFraction, outNormal);
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

// ソルバーを実行する前に、動的剛体へ蓄積済みの Force / Torque を反映します。
void PhysicsWorld::ApplyForces(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody, collider] : scene.View<TransformComponent, RigidBodyComponent, ColliderComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type != BodyType::Dynamic || rigidBody.IsSleeping || rigidBody.InverseMass <= 0.0f)
        {
            continue;
        }

        // 重力と蓄積された力をまとめて加速度に変換し、線形速度へ反映します。
        math::Vec3 acceleration{};
        if (rigidBody.UseGravity)
        {
            acceleration += m_Gravity;
        }

        acceleration += rigidBody.Force * rigidBody.InverseMass;
        rigidBody.LinearVelocity += acceleration * dt;

        // 回転運動もトルクから角速度へ変換して更新し、向きの整合性を保ちます。
        EnsurePhysicsOrientation(transform, rigidBody);
        rigidBody.AngularVelocity += (ComputeWorldInverseInertia(&transform, &rigidBody, &collider) * rigidBody.Torque) * dt;
    }
}

// 力が加わっていないときに、線形速度と角速度が自然に減衰するように調整します。
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
        rigidBody.AngularVelocity *= 1.0f / (1.0f + std::max(rigidBody.AngularDamping, 0.0f) * dt);
    }
}

// 速度更新後に、剛体の位置と向きを実際に移動させます。
void PhysicsWorld::IntegratePositions(Scene& scene, float dt)
{
    for (auto [entity, transform, rigidBody] : scene.View<TransformComponent, RigidBodyComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type == BodyType::Static || (rigidBody.Type == BodyType::Dynamic && rigidBody.IsSleeping))
        {
            continue;
        }

        transform.Position += rigidBody.LinearVelocity * dt;
        IntegratePhysicsOrientation(transform, rigidBody, dt);
    }
}

// Broad Phase で見つかったコライダーの組み合わせから、新しい接触マニホールドを生成します。
void PhysicsWorld::DetectCollisions(Scene& scene)
{
    // 前フレーム接触を退避し、今フレーム結果を作り直します。
    // 後段のRestorePersistentContactsでWarm Start再利用先として参照します。
    m_PreviousManifolds = std::move(m_Manifolds);
    m_Manifolds.clear();

    std::vector<BroadPhasePair> pairs;
    m_BroadPhase.ComputePairs(scene, pairs);

    // Broad Phase で抽出された候補ペアだけを調べ、実際に接触が起きるかを判定します。
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

        // 形状組み合わせごとに専用Narrow Phaseへ振り分けます。
        if (colliderA->Type == ColliderType::Sphere && colliderB->Type == ColliderType::Sphere)
        {
            generated = GenerateSphereSphereManifold(pair.A, *transformA, *colliderA, pair.B, *transformB, *colliderB, manifold);
        }
        else if (colliderA->Type == ColliderType::Sphere && colliderB->Type == ColliderType::Box)
        {
            generated = GenerateSphereBoxManifold(pair.A, *transformA, *colliderA, pair.B, *transformB, *colliderB, manifold);
        }
        else if (colliderA->Type == ColliderType::Box && colliderB->Type == ColliderType::Sphere)
        {
            generated = GenerateSphereBoxManifold(pair.B, *transformB, *colliderB, pair.A, *transformA, *colliderA, manifold);
        }
        else if (colliderA->Type == ColliderType::Box && colliderB->Type == ColliderType::Box)
        {
            generated = GenerateBoxBoxManifold(pair.A, *transformA, *colliderA, pair.B, *transformB, *colliderB, manifold);
        }

        if (generated)
        {
            m_Manifolds.push_back(manifold);
        }
    }

    // Plane は通常の Broad Phase では扱わないため、別途球体との組み合わせを確認します。
    for (auto [entityA, transformA, colliderA] : scene.View<TransformComponent, ColliderComponent>())
    {
        if (colliderA.Type != ColliderType::Sphere)
        {
            continue;
        }

        for (auto [entityB, transformB, colliderB] : scene.View<TransformComponent, ColliderComponent>())
        {
            if (colliderB.Type != ColliderType::Plane || entityA == entityB)
            {
                continue;
            }

            ContactManifold manifold{};
            if (GenerateSpherePlaneManifold(entityA, transformA, colliderA, entityB, transformB, colliderB, manifold))
            {
                m_Manifolds.push_back(manifold);
            }
        }
    }

    RestorePersistentContacts(scene);

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

// 前フレームの接触点を再利用して、同じ幾何形状の接触を滑らかに継続できるようにします。
// これにより、ソルバーの安定性が上がり、接触の揺れが抑えられます。
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

        // 前フレームの接触点と現在の接触点を対応付け、同じ接触として再利用できるかを確認します。
        for (const auto& candidate : m_PreviousManifolds)
        {
            if (candidate.IsTrigger || candidate.PointCount == 0 || !IsSamePair(current, candidate))
            {
                continue;
            }

            const bool same = current.A == candidate.A && current.B == candidate.B;
            const math::Vec3 previousNormal = same ? candidate.Normal : -candidate.Normal;
            if (math::Vec3::Dot(current.Normal.Normalized(), previousNormal.Normalized()) >= NormalThreshold)
            {
                previous = &candidate;
                sameOrder = same;
                break;
            }
        }

        if (!previous)
        {
            continue;
        }

        ++m_SolverDebugStatistics.PersistentManifoldCount;

        auto* transformA = scene.TryGetComponent<TransformComponent>(current.A.GetIndex());
        auto* transformB = scene.TryGetComponent<TransformComponent>(current.B.GetIndex());
        auto* bodyA = scene.TryGetComponent<RigidBodyComponent>(current.A.GetIndex());
        auto* bodyB = scene.TryGetComponent<RigidBodyComponent>(current.B.GetIndex());
        bool used[ContactManifold::MaxContactPointCount]{};

        for (std::size_t i = 0; i < current.PointCount; ++i)
        {
            std::size_t best = ContactManifold::MaxContactPointCount;
            float bestScore = std::numeric_limits<float>::max();

            const math::Vec3 currentLocalA = transformA ? ContactLocalAnchor(*transformA, bodyA, current.Points[i].Position) : math::Vec3{};
            const math::Vec3 currentLocalB = transformB ? ContactLocalAnchor(*transformB, bodyB, current.Points[i].Position) : math::Vec3{};

            for (std::size_t j = 0; j < previous->PointCount; ++j)
            {
                if (used[j])
                {
                    continue;
                }

                const ContactPoint& old = previous->Points[j];
                float score = std::numeric_limits<float>::max();

                // まずは接触特徴 ID が一致するものを優先し、回転しても Warm Start が維持されるようにします。
                if (FeatureMatches(current.Points[i].Feature, old.Feature, sameOrder))
                {
                    score = 0.0f;
                }

                // 接触特徴 ID が一致しない場合は、ローカルアンカーの近さで候補を探します。
                if (score == std::numeric_limits<float>::max() && old.PositionAnchorsInitialized && transformA && transformB)
                {
                    const math::Vec3 oldA = sameOrder ? old.LocalAnchorA : old.LocalAnchorB;
                    const math::Vec3 oldB = sameOrder ? old.LocalAnchorB : old.LocalAnchorA;
                    const float da = (currentLocalA - oldA).LengthSq();
                    const float db = (currentLocalB - oldB).LengthSq();
                    if (da <= LocalAnchorDistanceSq && db <= LocalAnchorDistanceSq)
                    {
                        score = 1.0f + da + db;
                    }
                }

                // それでも見つからない場合は、ワールド空間上の接触点の距離で最後の判定を行います。
                if (score == std::numeric_limits<float>::max())
                {
                    const float d = (current.Points[i].Position - old.Position).LengthSq();
                    if (d <= WorldDistanceSq)
                    {
                        score = 2.0f + d;
                    }
                }

                // scoreが小さいほど同一接触の確度が高いという単純序列で選択します。

                if (score < bestScore)
                {
                    bestScore = score;
                    best = j;
                }
            }

            if (best == ContactManifold::MaxContactPointCount)
            {
                continue;
            }

            used[best] = true;
            // ほぼ同じ接触であれば、前フレームのインパルスを初期値として引き継ぎます。
            const auto& source = previous->Points[best];
            auto& destination = current.Points[i];
            destination.AccumulatedNormalImpulse = source.AccumulatedNormalImpulse;
            destination.AccumulatedTangentImpulse = sameOrder ? source.AccumulatedTangentImpulse : -source.AccumulatedTangentImpulse;
            destination.CachedTangent = sameOrder ? source.CachedTangent : -source.CachedTangent;
            ++m_SolverDebugStatistics.PersistentContactPointCount;
        }
    }
}

// シーン内をレイで走査し、最も近い衝突点を見つけて結果構造体へ格納します。
bool PhysicsWorld::RayCast(
    Scene& scene,
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    PhysicsRayCastHit& outHit)
{
    if (maxFraction < 0.0f || direction.LengthSq() <= 1e-12f)
    {
        return false;
    }

    bool hit = false;
    float closest = maxFraction;
    PhysicsRayCastHit result{};

    // Broad Phase側RayCastはFat AABB候補のみ返すため、ここで実形状に対する最終判定を行います。
    m_BroadPhase.RayCast(scene, origin, direction, maxFraction, [&](Entity entity, uint32_t, float, const math::Vec3&, float current) -> float
        {
            auto* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
            auto* collider = scene.TryGetComponent<ColliderComponent>(entity.GetIndex());
            if (!transform || !collider || collider->Type == ColliderType::Plane)
            {
                return current;
            }

            float fraction = 0.0f;
            math::Vec3 normal{};
            // 現在の候補距離までにヒットするかを確認し、最も近い衝突だけを残します。
            if (!RayCastCollider(origin, direction, current, *transform, *collider, fraction, normal))
            {
                return current;
            }

            // 見つかった衝突のうち、レイの始点に最も近いものだけを採用します。
            if (!hit || fraction < closest)
            {
                hit = true;
                closest = fraction;
                result.HitEntity = entity;
                result.Fraction = fraction;
                result.Point = origin + direction * fraction;
                result.Normal = normal;
            }

            return closest;
        });

    // Planeは無限形状でTreeに載せないため、全件を別途評価します。
    for (auto [entity, transform, collider] : scene.View<TransformComponent, ColliderComponent>())
    {
        if (collider.Type != ColliderType::Plane)
        {
            continue;
        }

        float fraction = 0.0f;
        math::Vec3 normal{};
        if (RayCastPlane(origin, direction, closest, transform, collider, fraction, normal) && (!hit || fraction < closest))
        {
            hit = true;
            closest = fraction;
            result.HitEntity = entity;
            result.Fraction = fraction;
            result.Point = origin + direction * fraction;
            result.Normal = normal;
        }
    }

    if (hit)
    {
        outHit = result;
    }

    return hit;
}

// 指定した AABB と重なっているコライダーを持つエンティティをすべて収集します。
void PhysicsWorld::QueryAABB(Scene& scene, const AABB& queryBounds, std::vector<Entity>& outEntities)
{
    outEntities.clear();
    if (!queryBounds.IsValid())
    {
        return;
    }

    m_BroadPhase.QueryAABB(scene, queryBounds, [&](Entity entity, uint32_t) -> bool
        {
            auto* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
            auto* collider = scene.TryGetComponent<ColliderComponent>(entity.GetIndex());
            if (!transform || !collider)
            {
                return true;
            }

            AABB bounds{};
            // AABB と重なっているコライダーだけを結果に追加します。
            if (ComputeColliderAABB(*transform, *collider, bounds) && bounds.Overlaps(queryBounds))
            {
                outEntities.push_back(entity);
            }
            return true;
        });
}

// 有効な接触制約を解き、結果として得られたインパルスをデバッグ統計に反映します。
void PhysicsWorld::SolveCollisions(Scene& scene, float dt)
{
    if (m_SolverSettings.EnableWarmStart)
    {
        // Warm Start が有効な場合、前フレームのインパルスを持つ接触だけを数えます。
        for (const auto& manifold : m_Manifolds)
        {
            for (std::size_t i = 0; i < manifold.PointCount; ++i)
            {
                if (!manifold.IsTrigger &&
                    (manifold.Points[i].AccumulatedNormalImpulse > 0.0f || std::abs(manifold.Points[i].AccumulatedTangentImpulse) > 1e-8f))
                {
                    ++m_SolverDebugStatistics.WarmStartedConstraintCount;
                }
            }
        }
    }

    // Solver内部で最小1反復に丸めるため、表示値も同じ基準へ揃えます。
    m_SolverDebugStatistics.VelocityIterations = std::max(m_SolverSettings.VelocityIterations, 1u);
    // 実際の接触制約解決を実行し、結果のインパルスをデバッグ統計へ反映します。
    SolveContactManifolds(scene, m_Manifolds, dt, m_SolverSettings);
    UpdateSolverDebugStatisticsAfterSolve();
}

// ソルバーの結果をデバッグしやすい統計情報としてまとめます。
void PhysicsWorld::UpdateSolverDebugStatisticsAfterSolve()
{
    // 解法後のインパルス分布を走査し、フレーム内ピーク値を保持します。
    // 収束悪化時はこれらの値が急上昇するため、チューニング指標として使えます。
    for (const auto& manifold : m_Manifolds)
    {
        for (std::size_t i = 0; i < manifold.PointCount; ++i)
        {
            const auto& point = manifold.Points[i];
            m_SolverDebugStatistics.MaxNormalImpulse = std::max(
                m_SolverDebugStatistics.MaxNormalImpulse,
                std::max(point.AccumulatedNormalImpulse, 0.0f));
            m_SolverDebugStatistics.MaxFrictionImpulse = std::max(
                m_SolverDebugStatistics.MaxFrictionImpulse,
                std::abs(point.AccumulatedTangentImpulse));
        }
    }
}

// 設定された閾値まで静止した剛体を睡眠状態に移行させます。
void PhysicsWorld::UpdateSleeping(Scene& scene, float dt)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);

        if (rigidBody.Type != BodyType::Dynamic)
        {
            continue;
        }

        if (!rigidBody.AllowSleep)
        {
            WakeRigidBody(rigidBody);
            continue;
        }

        if (rigidBody.IsSleeping)
        {
            rigidBody.LinearVelocity = {};
            rigidBody.AngularVelocity = {};
            continue;
        }

        const float linearThreshold = std::max(rigidBody.SleepThreshold, 0.0f);
        const float angularThreshold = std::max(rigidBody.AngularSleepThreshold, 0.0f);

        // 速度が十分に小さければ睡眠候補と見なし、しきい値を超えたら実際に睡眠状態へ移行します。
        if (rigidBody.LinearVelocity.LengthSq() <= linearThreshold * linearThreshold &&
            rigidBody.AngularVelocity.LengthSq() <= angularThreshold * angularThreshold)
        {
            rigidBody.SleepTimer += dt;
            const float threshold = std::max(rigidBody.SleepTimeThreshold, 0.0f);
            if (rigidBody.SleepTimer >= threshold)
            {
                rigidBody.IsSleeping = true;
                rigidBody.LinearVelocity = {};
                rigidBody.AngularVelocity = {};
                rigidBody.SleepTimer = threshold;
            }
        }
        else
        {
            rigidBody.SleepTimer = 0.0f;
        }
    }
}

// 1ステップの終わりに、蓄積済みの Force / Torque をクリアして二重加算を防ぎます。
void PhysicsWorld::ClearForces(Scene& scene)
{
    for (auto [entity, rigidBody] : scene.View<RigidBodyComponent>())
    {
        static_cast<void>(entity);
        rigidBody.Force = {};
        rigidBody.Torque = {};
    }
}

// 動的剛体に Force を加え、即座に反応するように起床させます。
void PhysicsWorld::AddForce(Scene& scene, Entity entity, const math::Vec3& force)
{
    if (!scene.IsEntityAlive(entity))
    {
        return;
    }

    auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (!rigidBody || rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f)
    {
        return;
    }

    WakeRigidBody(*rigidBody);
    // Forceは1step分を蓄積し、Step末尾のClearForcesで必ずリセットされます。
    rigidBody->Force += force;
}

// 速度へ即時的に Impulse を与え、必要に応じて起床させます。
void PhysicsWorld::AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse)
{
    if (!scene.IsEntityAlive(entity))
    {
        return;
    }

    auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (!rigidBody || rigidBody->Type != BodyType::Dynamic || rigidBody->InverseMass <= 0.0f)
    {
        return;
    }

    WakeRigidBody(*rigidBody);
    // Impulseは即時速度へ反映され、次の制約解法に直接影響します。
    rigidBody->LinearVelocity += impulse * rigidBody->InverseMass;
}

// 睡眠中の動的剛体でも、すぐに起床させます。
void PhysicsWorld::WakeUp(Scene& scene, Entity entity)
{
    if (!scene.IsEntityAlive(entity))
    {
        return;
    }

    auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (rigidBody && rigidBody->Type == BodyType::Dynamic)
    {
        WakeRigidBody(*rigidBody);
    }
}

// 1フレーム分の物理シミュレーションをまとめて進めます。
// 力の適用、移動、衝突検出、接触解決、睡眠更新の順で処理します。
void PhysicsWorld::Step(Scene& scene, float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }

    m_SolverDebugStatistics.Reset();
    // 実行順序の意図:
    // 1) Force反映 -> 2) 速度更新 -> 3) 衝突検出 -> 4) 制約解決
    // 5) 姿勢積分 -> 6) Sleep判定 -> 7) 力クリア
    // の流れで、semi-implicitな安定性とデバッグ統計の整合を保ちます。
    ApplyForces(scene, dt);
    IntegrateVelocities(scene, dt);
    DetectCollisions(scene);
    SolveCollisions(scene, dt);
    IntegratePositions(scene, dt);
    UpdateSleeping(scene, dt);
    ClearForces(scene);
}
