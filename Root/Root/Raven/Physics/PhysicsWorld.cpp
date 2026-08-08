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
    return (a.A == b.A && a.B == b.B)
        || (a.A == b.B && a.B == b.A);
}

// 剛体に明示的なQuaternion姿勢がある場合はその向きを使用し、
// 未初期化ならTransformのEuler角から接触計算用の姿勢を求めます。
// そうでない場合は Transform のオイラー角から接触向きを求めます。
math::Quat GetContactOrientation(
    const TransformComponent& transform,
    const RigidBodyComponent* body)
{
    return (body && body->OrientationInitialized)
        ? body->Orientation.Normalized()
        : PhysicsOrientationFromEuler(transform.Rotation);
}

// ワールド空間の接触点を剛体ローカル空間へ変換します。
// Persistent Contactで回転後も同じ接触を対応付けるために使用します。
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
// -----------------------------------------------------------------------
// Feature IDが同じ幾何特徴を表すか確認します。
// Entity順序が前フレームと逆転している場合はA/Bを入れ替えて比較します。
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
// 二次方程式を解き、maxFraction以内で最も手前の交点を返します。
// -----------------------------------------------------------------------
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
        // -----------------------------------------------------------------------
        // 二次方程式 a*t^2 + 2b*t + c = 0 を解いて最短交点を選択します。
        // tは origin + direction * t のパラメータです。
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
// Planeは無限形状なのでBroad Phase Treeには載せず直接判定します。
// ----------------------------------------------------------------------------
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
    math::Vec3 normal = collider.PlaneNormal.Normalized();
    if (normal.LengthSq() <= 1.0e-12f)
    {
        return false;
    }

    const float denominator = math::Vec3::Dot(normal, direction);
    const float distance = math::Vec3::Dot(
        normal,
        origin - (transform.Position + collider.Offset));

    if (std::abs(denominator) <= 1.0e-8f)
    {
        // RayとPlaneが平行で、始点がPlane上でもない場合は交差しません。
        if (std::abs(distance) > 1.0e-6f)
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

// Collider種別ごとのRayCast実装へ振り分けます。
// --------------------------------------------
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

// Solver実行前にDynamic剛体へ蓄積済みForce/Torqueを反映します。
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

        // Torqueはworld-space逆慣性テンソルを通して角加速度へ変換します。
        EnsurePhysicsOrientation(transform, rigidBody);
        rigidBody.AngularVelocity +=
            (ComputeWorldInverseInertia(&transform, &rigidBody, &collider) * rigidBody.Torque) * dt;
    }
}

// 線形速度・角速度へDampingを適用します。
// ----------------------------------------------------------
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

        rigidBody.LinearVelocity *=
            1.0f / (1.0f + std::max(rigidBody.LinearDamping, 0.0f) * dt);
        rigidBody.AngularVelocity *=
            1.0f / (1.0f + std::max(rigidBody.AngularDamping, 0.0f) * dt);
    }
}

// 現在の速度をTransformへ積分します。
// --------------------------------------------------------
// 速度更新後に、剛体の位置と向きを実際に移動させます。
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

// Broad Phase候補から有限形状同士のContact Manifoldを生成します。
// Planeは無限形状でTreeに載せないため、後半でSphere/Boxとの組み合わせを別途処理します。
// ----------------------------------------------------------------------------------
// Broad Phase で見つかったコライダーの組み合わせから、新しい接触マニホールドを生成します。
void PhysicsWorld::DetectCollisions(Scene& scene)
{
    // ------------------------------------------------------------
    // 前フレーム接触を退避し、今フレーム結果を作り直します。
    // 後段のRestorePersistentContactsでWarm Start再利用先として参照します。
    // ------------------------------------------------------------
    // 前フレーム接触を退避し、Warm Start用の参照元として保持します。
    m_PreviousManifolds = std::move(m_Manifolds);
    m_Manifolds.clear();

    std::vector<BroadPhasePair> pairs;
    m_BroadPhase.ComputePairs(scene, pairs);

    // Broad Phase で抽出された候補ペアだけを調べ、実際に接触が起きるかを判定します。
    // ------------------------------------------------------------------------------
    // Broad Phaseが抽出した有限形状の候補だけをNarrow Phaseへ渡します。
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

    // Planeは無限形状なので通常のBroad Phase Treeには登録しません。
    // 既存のSphere-Plane経路を拡張し、今回追加したBox-Planeも同じ場所で処理します。
    // -----------------------------------------------------------------------------
    // Plane は通常の Broad Phase では扱わないため、別途球体との組み合わせを確認します。
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
// ----------------------------------------------------------------------
// 前フレームの接触点を再利用して、同じ幾何形状の接触を滑らかに継続します。
// Feature ID -> Local Anchor -> World Positionの順に対応付けを試みます。
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
            if (candidate.IsTrigger
                || candidate.PointCount == 0
                || !IsSamePair(current, candidate))
            {
                continue;
            }

            const bool same = current.A == candidate.A && current.B == candidate.B;
            const math::Vec3 previousNormal = same ? candidate.Normal : -candidate.Normal;

            // 法線が大きく変化した接触は同じContactとして再利用しません。
            if (math::Vec3::Dot(
                    current.Normal.Normalized(),
                    previousNormal.Normalized()) >= NormalThreshold)
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

            const math::Vec3 currentLocalA = transformA
                ? ContactLocalAnchor(*transformA, bodyA, current.Points[i].Position)
                : math::Vec3{};
            const math::Vec3 currentLocalB = transformB
                ? ContactLocalAnchor(*transformB, bodyB, current.Points[i].Position)
                : math::Vec3{};

            for (std::size_t j = 0; j < previous->PointCount; ++j)
            {
                if (used[j])
                {
                    continue;
                }

                const ContactPoint& old = previous->Points[j];
                float score = std::numeric_limits<float>::max();

                // まずは接触特徴 ID が一致するものを優先し、回転しても Warm Start が維持されるようにします。
                // --------------------------------------------------------------------------------------------
                // 1. Feature ID一致を最優先します。
                if (FeatureMatches(current.Points[i].Feature, old.Feature, sameOrder))
                {
                    score = 0.0f;
                }

                // 接触特徴 ID が一致しない場合は、ローカルアンカーの近さで候補を探します。
                // ---------------------------------------------------------------------------------------------
                // 2. Feature IDで決まらなければLocal Anchorの近さを使います。
                if (score == std::numeric_limits<float>::max()
                    && old.PositionAnchorsInitialized
                    && transformA
                    && transformB)
                {
                    const math::Vec3 oldA = sameOrder ? old.LocalAnchorA : old.LocalAnchorB;
                    const math::Vec3 oldB = sameOrder ? old.LocalAnchorB : old.LocalAnchorA;
                    const float distanceA = (currentLocalA - oldA).LengthSq();
                    const float distanceB = (currentLocalB - oldB).LengthSq();

                    if (distanceA <= LocalAnchorDistanceSq
                        && distanceB <= LocalAnchorDistanceSq)
                    {
                        score = 1.0f + distanceA + distanceB;
                    }
                }

                // それでも見つからない場合は、ワールド空間上の接触点の距離で最後の判定を行います。
                // -----------------------------------------------------------------------------------------------
                // 3. 最後のfallbackとしてWorld接触点距離を使います。
                if (score == std::numeric_limits<float>::max())
                {
                    const float distance =
                        (current.Points[i].Position - old.Position).LengthSq();
                    if (distance <= WorldDistanceSq)
                    {
                        score = 2.0f + distance;
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
            // -------------------------------------------------------------------------
            // 同じ接触と判断できた場合、前フレームImpulseを引き継いでWarm Startします。
            const ContactPoint& source = previous->Points[best];
            ContactPoint& destination = current.Points[i];
            destination.AccumulatedNormalImpulse = source.AccumulatedNormalImpulse;
            destination.AccumulatedTangentImpulse = sameOrder
                ? source.AccumulatedTangentImpulse
                : -source.AccumulatedTangentImpulse;
            destination.CachedTangent = sameOrder
                ? source.CachedTangent
                : -source.CachedTangent;

            ++m_SolverDebugStatistics.PersistentContactPointCount;
        }
    }
}

// シーン内をレイで走査し、最も近い衝突点を見つけて結果構造体へ格納します。
//---------------------------------------------------------------------------------------
// シーン内をRayCastし、最も近い実形状へのHitを返します。
bool PhysicsWorld::RayCast(
    Scene& scene,
    const math::Vec3& origin,
    const math::Vec3& direction,
    float maxFraction,
    PhysicsRayCastHit& outHit)
{
    if (maxFraction < 0.0f || direction.LengthSq() <= 1.0e-12f)
    {
        return false;
    }

    bool hit = false;
    float closest = maxFraction;
    PhysicsRayCastHit result{};

    // Broad Phase側RayCastはFat AABB候補のみ返すため、ここで実形状に対する最終判定を行います。
    // ------------------------------------------------------------------
    // Broad Phase側RayCastはFat AABB候補のみ返すため、
    // callback内でCollider実形状に対する最終RayCastを行います。
    m_BroadPhase.RayCast(
        scene,
        origin,
        direction,
        maxFraction,
        [&](Entity entity, uint32_t, float, const math::Vec3&, float current) -> float
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
            if (!RayCastCollider(
                origin,
                direction,
                current,
                *transform,
                *collider,
                fraction,
                normal))
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

    // Planeは無限形状なのでTreeに載せず、全Planeを別途評価します。
    for (auto [entity, transform, collider]
        : scene.View<TransformComponent, ColliderComponent>())
    {
        if (collider.Type != ColliderType::Plane)
        {
            continue;
        }

        float fraction = 0.0f;
        math::Vec3 normal{};
        if (RayCastPlane(
                origin,
                direction,
                closest,
                transform,
                collider,
                fraction,
                normal)
            && (!hit || fraction < closest))
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
// -----------------------------------------------------------------------------
// 指定AABBと重なるColliderをBroad Phase候補から収集します。
void PhysicsWorld::QueryAABB(
    Scene& scene,
    const AABB& queryBounds,
    std::vector<Entity>& outEntities)
{
    outEntities.clear();
    if (!queryBounds.IsValid())
    {
        return;
    }

    m_BroadPhase.QueryAABB(
        scene,
        queryBounds,
        [&](Entity entity, uint32_t) -> bool
        {
            auto* transform = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
            auto* collider = scene.TryGetComponent<ColliderComponent>(entity.GetIndex());
            if (!transform || !collider)
            {
                return true;
            }

            AABB bounds{};
            // AABB と重なっているコライダーだけを結果に追加します。
            if (ComputeColliderAABB(*transform, *collider, bounds)
                && bounds.Overlaps(queryBounds))
            {
                outEntities.push_back(entity);
            }

            return true;
        });
}

// 有効な接触制約を解き、結果として得られたインパルスをデバッグ統計に反映します。
// -----------------------------------------------------------------------------
// 有効なContact Manifoldを解き、Solver統計を更新します。
void PhysicsWorld::SolveCollisions(Scene& scene, float dt)
{
    if (m_SolverSettings.EnableWarmStart)
    {
        // Warm Start が有効な場合、前フレームのインパルスを持つ接触だけを数えます。
        for (const auto& manifold : m_Manifolds)
        {
            for (std::size_t i = 0; i < manifold.PointCount; ++i)
            {
                if (!manifold.IsTrigger
                    && (manifold.Points[i].AccumulatedNormalImpulse > 0.0f
                        || std::abs(manifold.Points[i].AccumulatedTangentImpulse) > 1.0e-8f))
                {
                    ++m_SolverDebugStatistics.WarmStartedConstraintCount;
                }
            }
        }
    }

    // Solver内部で最小1反復に丸めるため、表示値も同じ基準へ揃えます。
    // ----------------------------------------------------------------------------
    // Solver内部と同じく最低1回は反復する前提で統計値を記録します。
    m_SolverDebugStatistics.VelocityIterations =
        std::max(m_SolverSettings.VelocityIterations, 1u);

    // 実際の接触制約解決を実行し、結果のインパルスをデバッグ統計へ反映します。
    SolveContactManifolds(scene, m_Manifolds, dt, m_SolverSettings);
    UpdateSolverDebugStatisticsAfterSolve();
}

// ソルバーの結果をデバッグしやすい統計情報としてまとめます。
// --------------------------------------------------------------------------------
// Solver後の最大Impulseをデバッグ統計へ反映します。
void PhysicsWorld::UpdateSolverDebugStatisticsAfterSolve()
{
    // 解法後のインパルス分布を走査し、フレーム内ピーク値を保持します。
    // 収束悪化時はこれらの値が急上昇するため、チューニング指標として使えます。
    for (const auto& manifold : m_Manifolds)
    {
        for (std::size_t i = 0; i < manifold.PointCount; ++i)
        {
            const ContactPoint& point = manifold.Points[i];

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
// ---------------------------------------------------------------------------------
// 設定された閾値まで静止したDynamic BodyをSleepingへ移行します。
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
        if (rigidBody.LinearVelocity.LengthSq() <= linearThreshold * linearThreshold
            && rigidBody.AngularVelocity.LengthSq() <= angularThreshold * angularThreshold)
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
// -------------------------------------------------------------------------------
// Force/Torqueは1step分だけ蓄積するためStep末尾でクリアします。
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
// -------------------------------------------------------------------------------
// Dynamic剛体へForceを追加し、Sleeping中なら起床させます。
void PhysicsWorld::AddForce(Scene& scene, Entity entity, const math::Vec3& force)
{
    if (!scene.IsEntityAlive(entity))
    {
        return;
    }

    auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (!rigidBody
        || rigidBody->Type != BodyType::Dynamic
        || rigidBody->InverseMass <= 0.0f)
    {
        return;
    }

    WakeRigidBody(*rigidBody);
    // Forceは1step分を蓄積し、Step末尾のClearForcesで必ずリセットされます。
    rigidBody->Force += force;
}

// 速度へ即時的に Impulse を与え、必要に応じて起床させます。
// --------------------------------------------------------------------------------
// ImpulseをDynamic剛体のLinearVelocityへ即時反映します。
void PhysicsWorld::AddImpulse(Scene& scene, Entity entity, const math::Vec3& impulse)
{
    if (!scene.IsEntityAlive(entity))
    {
        return;
    }

    auto* rigidBody = scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());
    if (!rigidBody
        || rigidBody->Type != BodyType::Dynamic
        || rigidBody->InverseMass <= 0.0f)
    {
        return;
    }

    WakeRigidBody(*rigidBody);
    // Impulseは即時速度へ反映され、次の制約解法に直接影響します。
    rigidBody->LinearVelocity += impulse * rigidBody->InverseMass;
}

// 睡眠中の動的剛体でも、すぐに起床させます。
// -------------------------------------------------------------------------------
// Dynamic剛体を明示的に起床させます。
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
// -------------------------------------------------------------------------------
// 1固定step分のPhysics処理を進めます。
// 元の処理順序を維持し、今回の修正では変更していません。
void PhysicsWorld::Step(Scene& scene, float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }

    m_SolverDebugStatistics.Reset();

    // 1) Force/Torque反映
    // 2) Damping適用
    // 3) Collision Detection
    // 4) Contact Solver
    // 5) Position/Orientation積分
    // 6) Sleeping更新
    // 7) Force/Torqueクリア
    ApplyForces(scene, dt);
    IntegrateVelocities(scene, dt);
    DetectCollisions(scene);
    SolveCollisions(scene, dt);
    IntegratePositions(scene, dt);
    UpdateSleeping(scene, dt);
    ClearForces(scene);
}

} // namespace ph
} // namespace Raven
