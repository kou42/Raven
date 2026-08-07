#include <algorithm>
#include <cmath>
#include <limits>

#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Solver/ContactSolver.h"

namespace Raven
{
namespace ph
{
namespace
{
void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}

bool IsSamePair(const ContactManifold& a, const ContactManifold& b)
{
    // CollisionDetectionによってSphere-Boxなどの順番が正規化されるケースもありますが、
    // 将来dispatch規約が変わってもPersistenceが壊れないよう逆順も同一ペアとします。
    return (a.A == b.A && a.B == b.B) || (a.A == b.B && a.B == b.A);
}

bool RayCastSphere(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
    const math::Vec3& center, float radius, float& outFraction, math::Vec3& outNormal)
{
    const math::Vec3 m = origin - center; const float a = math::Vec3::Dot(direction, direction);
    if (a <= 1.0e-12f) return false;
    const float c = math::Vec3::Dot(m, m) - radius * radius;
    if (c <= 0.0f) { outFraction = 0.0f; const float ls = m.LengthSq(); outNormal = ls > 1.0e-12f ? m / std::sqrt(ls) : -direction.Normalized(); return true; }
    const float b = math::Vec3::Dot(m, direction); const float d = b * b - a * c;
    if (d < 0.0f) return false;
    const float f = (-b - std::sqrt(d)) / a; if (f < 0.0f || f > maxFraction) return false;
    outFraction = f; outNormal = (origin + direction * f - center).Normalized(); return true;
}

bool RayCastPlane(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
    const TransformComponent& transform, const ColliderComponent& collider, float& outFraction, math::Vec3& outNormal)
{
    math::Vec3 normal = collider.PlaneNormal.Normalized(); if (normal.LengthSq() <= 1.0e-12f) return false;
    const math::Vec3 p = transform.Position + collider.Offset; const float denom = math::Vec3::Dot(normal, direction);
    const float dist = math::Vec3::Dot(normal, origin - p);
    if (std::abs(denom) <= 1.0e-8f) { if (std::abs(dist) > 1.0e-6f) return false; outFraction = 0; outNormal = normal; return true; }
    const float f = -dist / denom; if (f < 0 || f > maxFraction) return false;
    outFraction = f; outNormal = denom < 0 ? normal : -normal; return true;
}

bool RayCastCollider(const math::Vec3& origin, const math::Vec3& direction, float maxFraction,
    const TransformComponent& transform, const ColliderComponent& collider, float& outFraction, math::Vec3& outNormal)
{
    if (collider.Type == ColliderType::Sphere)
        return RayCastSphere(origin, direction, maxFraction, transform.Position + collider.Offset, std::max(collider.Radius, 0.0f), outFraction, outNormal);
    if (collider.Type == ColliderType::Box) { AABB b{}; return ComputeColliderAABB(transform, collider, b) && b.RayCast(origin, direction, maxFraction, outFraction, &outNormal); }
    if (collider.Type == ColliderType::Plane) return RayCastPlane(origin, direction, maxFraction, transform, collider, outFraction, outNormal);
    return false;
}
}

void PhysicsWorld::SetGravity(const math::Vec3& gravity) { m_Gravity = gravity; }
const math::Vec3& PhysicsWorld::GetGravity() const { return m_Gravity; }

void PhysicsWorld::ApplyForces(Scene& scene, float dt)
{
    for (auto [entity, transform, rb] : scene.View<TransformComponent, RigidBodyComponent>())
    { static_cast<void>(entity); static_cast<void>(transform); if (rb.Type != BodyType::Dynamic || rb.IsSleeping || rb.InverseMass <= 0) continue; math::Vec3 a{}; if (rb.UseGravity) a += m_Gravity; a += rb.Force * rb.InverseMass; rb.LinearVelocity += a * dt; }
}

void PhysicsWorld::IntegrateVelocities(Scene& scene, float dt)
{
    for (auto [entity, rb] : scene.View<RigidBodyComponent>()) { static_cast<void>(entity); if (rb.Type != BodyType::Dynamic || rb.IsSleeping) continue; rb.LinearVelocity *= 1.0f / (1.0f + std::max(rb.LinearDamping, 0.0f) * dt); }
}

void PhysicsWorld::IntegratePositions(Scene& scene, float dt)
{
    for (auto [entity, t, rb] : scene.View<TransformComponent, RigidBodyComponent>()) { static_cast<void>(entity); if (rb.Type == BodyType::Static || (rb.Type == BodyType::Dynamic && rb.IsSleeping)) continue; t.Position += rb.LinearVelocity * dt; }
}

void PhysicsWorld::DetectCollisions(Scene& scene)
{
    // Solve済みの前Step Manifoldを退避してから、新しい幾何学情報を生成します。
    // m_PreviousManifoldsは1Stepだけ保持するため、接触が途切れれば自動的にCacheから消えます。
    m_PreviousManifolds = std::move(m_Manifolds);
    m_Manifolds.clear();

    std::vector<BroadPhasePair> pairs; m_BroadPhase.ComputePairs(scene, pairs);
    for (const BroadPhasePair& pair : pairs)
    {
        if (!scene.IsEntityAlive(pair.A) || !scene.IsEntityAlive(pair.B)) continue;
        auto* ta = scene.TryGetComponent<TransformComponent>(pair.A.GetIndex()); auto* tb = scene.TryGetComponent<TransformComponent>(pair.B.GetIndex());
        auto* ca = scene.TryGetComponent<ColliderComponent>(pair.A.GetIndex()); auto* cb = scene.TryGetComponent<ColliderComponent>(pair.B.GetIndex());
        if (!ta || !tb || !ca || !cb) continue;
        ContactManifold m{}; bool generated = false;
        if (ca->Type == ColliderType::Sphere && cb->Type == ColliderType::Sphere) generated = GenerateSphereSphereManifold(pair.A,*ta,*ca,pair.B,*tb,*cb,m);
        else if (ca->Type == ColliderType::Sphere && cb->Type == ColliderType::Box) generated = GenerateSphereBoxManifold(pair.A,*ta,*ca,pair.B,*tb,*cb,m);
        else if (ca->Type == ColliderType::Box && cb->Type == ColliderType::Sphere) generated = GenerateSphereBoxManifold(pair.B,*tb,*cb,pair.A,*ta,*ca,m);
        else if (ca->Type == ColliderType::Box && cb->Type == ColliderType::Box) generated = GenerateBoxBoxManifold(pair.A,*ta,*ca,pair.B,*tb,*cb,m);
        if (generated) m_Manifolds.push_back(m);
    }
    for (auto [se, st, sc] : scene.View<TransformComponent, ColliderComponent>())
    {
        if (sc.Type != ColliderType::Sphere) continue;
        for (auto [pe, pt, pc] : scene.View<TransformComponent, ColliderComponent>())
        {
            if (pc.Type != ColliderType::Plane || se == pe) continue; ContactManifold m{};
            if (GenerateSpherePlaneManifold(se,st,sc,pe,pt,pc,m)) m_Manifolds.push_back(m);
        }
    }

    RestorePersistentContacts();
}

void PhysicsWorld::RestorePersistentContacts()
{
    constexpr float ContactMatchDistance = 0.05f;
    constexpr float ContactMatchDistanceSquared = ContactMatchDistance * ContactMatchDistance;
    constexpr float NormalMatchThreshold = 0.9f;

    for (ContactManifold& current : m_Manifolds)
    {
        if (current.IsTrigger || current.PointCount == 0) continue;

        const ContactManifold* previous = nullptr;
        for (const ContactManifold& candidate : m_PreviousManifolds)
        {
            if (!candidate.IsTrigger && candidate.PointCount > 0 && IsSamePair(current, candidate))
            {
                // 法線が大きく変化した接触へ古いImpulseを再利用すると逆方向へ押す危険があります。
                // ペア順が逆の場合は法線符号も反転して比較します。
                const bool sameOrder = current.A == candidate.A && current.B == candidate.B;
                const math::Vec3 previousNormal = sameOrder ? candidate.Normal : -candidate.Normal;
                if (math::Vec3::Dot(current.Normal.Normalized(), previousNormal.Normalized()) >= NormalMatchThreshold)
                { previous = &candidate; break; }
            }
        }
        if (!previous) continue;

        const bool sameOrder = current.A == previous->A && current.B == previous->B;
        bool previousPointUsed[ContactManifold::MaxContactPointCount]{};

        // Contact Feature IDをまだ持たないため、ワールド空間の最近傍点で対応付けます。
        // 0.05m以内という小さな閾値に限定し、別のfaceへ移った古いImpulseを誤適用しません。
        for (std::size_t i = 0; i < current.PointCount; ++i)
        {
            std::size_t bestIndex = ContactManifold::MaxContactPointCount;
            float bestDistanceSquared = ContactMatchDistanceSquared;
            for (std::size_t j = 0; j < previous->PointCount; ++j)
            {
                if (previousPointUsed[j]) continue;
                const float distanceSquared = (current.Points[i].Position - previous->Points[j].Position).LengthSq();
                if (distanceSquared <= bestDistanceSquared) { bestDistanceSquared = distanceSquared; bestIndex = j; }
            }
            if (bestIndex == ContactManifold::MaxContactPointCount) continue;

            previousPointUsed[bestIndex] = true;
            const ContactPoint& oldPoint = previous->Points[bestIndex];
            ContactPoint& newPoint = current.Points[i];
            newPoint.AccumulatedNormalImpulse = oldPoint.AccumulatedNormalImpulse;
            newPoint.AccumulatedTangentImpulse = sameOrder ? oldPoint.AccumulatedTangentImpulse : -oldPoint.AccumulatedTangentImpulse;
            newPoint.CachedTangent = sameOrder ? oldPoint.CachedTangent : -oldPoint.CachedTangent;
        }

        // Rotation未対応SolverはPoint[0]を代表Constraintとして使います。
        // Box faceのcorner順序が変わってPoint[0]だけ最近傍Matchに失敗した場合でも、
        // ペアが継続している限り代表Impulseは前回Point[0]から継承します。
        if (current.Points[0].AccumulatedNormalImpulse == 0.0f && previous->PointCount > 0)
        {
            current.Points[0].AccumulatedNormalImpulse = previous->Points[0].AccumulatedNormalImpulse;
            current.Points[0].AccumulatedTangentImpulse = sameOrder ? previous->Points[0].AccumulatedTangentImpulse : -previous->Points[0].AccumulatedTangentImpulse;
            current.Points[0].CachedTangent = sameOrder ? previous->Points[0].CachedTangent : -previous->Points[0].CachedTangent;
        }
    }
}

bool PhysicsWorld::RayCast(Scene& scene,const math::Vec3& o,const math::Vec3& d,float maxF,PhysicsRayCastHit& out)
{
    if(maxF<0||d.LengthSq()<=1e-12f)return false; bool hit=false; float closest=maxF; PhysicsRayCastHit h{};
    m_BroadPhase.RayCast(scene,o,d,maxF,[&](Entity e,uint32_t p,float ff,const math::Vec3& fn,float cur)->float{static_cast<void>(p);static_cast<void>(ff);static_cast<void>(fn);auto*t=scene.TryGetComponent<TransformComponent>(e.GetIndex());auto*c=scene.TryGetComponent<ColliderComponent>(e.GetIndex());if(!t||!c||c->Type==ColliderType::Plane)return cur;float f=0;math::Vec3 n{};if(!RayCastCollider(o,d,cur,*t,*c,f,n))return cur;if(!hit||f<closest){hit=true;closest=f;h.HitEntity=e;h.Fraction=f;h.Point=o+d*f;h.Normal=n;}return closest;});
    for(auto[e,t,c]:scene.View<TransformComponent,ColliderComponent>()){if(c.Type!=ColliderType::Plane)continue;float f=0;math::Vec3 n{};if(RayCastPlane(o,d,closest,t,c,f,n)&&(!hit||f<closest)){hit=true;closest=f;h.HitEntity=e;h.Fraction=f;h.Point=o+d*f;h.Normal=n;}}
    if(hit)out=h;return hit;
}

void PhysicsWorld::QueryAABB(Scene& scene,const AABB& q,std::vector<Entity>& out)
{
    out.clear();if(!q.IsValid())return;m_BroadPhase.QueryAABB(scene,q,[&](Entity e,uint32_t p)->bool{static_cast<void>(p);auto*t=scene.TryGetComponent<TransformComponent>(e.GetIndex());auto*c=scene.TryGetComponent<ColliderComponent>(e.GetIndex());if(!t||!c)return true;AABB b{};if(ComputeColliderAABB(*t,*c,b)&&b.Overlaps(q))out.push_back(e);return true;});
}

void PhysicsWorld::SolveCollisions(Scene& scene,float dt){SolveContactManifolds(scene,m_Manifolds,dt);}

void PhysicsWorld::UpdateSleeping(Scene& scene,float dt)
{
    for(auto[e,rb]:scene.View<RigidBodyComponent>()){static_cast<void>(e);if(rb.Type!=BodyType::Dynamic)continue;if(!rb.AllowSleep){WakeRigidBody(rb);continue;}if(rb.IsSleeping){rb.LinearVelocity=math::Vec3{};continue;}const float th=std::max(rb.SleepThreshold,0.0f);if(rb.LinearVelocity.LengthSq()<=th*th){rb.SleepTimer+=dt;const float req=std::max(rb.SleepTimeThreshold,0.0f);if(rb.SleepTimer>=req){rb.IsSleeping=true;rb.LinearVelocity=math::Vec3{};rb.SleepTimer=req;}}else rb.SleepTimer=0;}
}
void PhysicsWorld::ClearForces(Scene& scene){for(auto[e,rb]:scene.View<RigidBodyComponent>()){static_cast<void>(e);rb.Force=math::Vec3{};rb.Torque=math::Vec3{};}}
void PhysicsWorld::AddForce(Scene& scene,Entity e,const math::Vec3& f){if(!scene.IsEntityAlive(e))return;auto*rb=scene.TryGetComponent<RigidBodyComponent>(e.GetIndex());if(!rb||rb->Type!=BodyType::Dynamic||rb->InverseMass<=0)return;WakeRigidBody(*rb);rb->Force+=f;}
void PhysicsWorld::AddImpulse(Scene& scene,Entity e,const math::Vec3& i){if(!scene.IsEntityAlive(e))return;auto*rb=scene.TryGetComponent<RigidBodyComponent>(e.GetIndex());if(!rb||rb->Type!=BodyType::Dynamic||rb->InverseMass<=0)return;WakeRigidBody(*rb);rb->LinearVelocity+=i*rb->InverseMass;}
void PhysicsWorld::WakeUp(Scene& scene,Entity e){if(!scene.IsEntityAlive(e))return;auto*rb=scene.TryGetComponent<RigidBodyComponent>(e.GetIndex());if(rb&&rb->Type==BodyType::Dynamic)WakeRigidBody(*rb);}
void PhysicsWorld::Step(Scene& scene,float dt){if(dt<=0)return;ApplyForces(scene,dt);IntegrateVelocities(scene,dt);IntegratePositions(scene,dt);DetectCollisions(scene);SolveCollisions(scene,dt);UpdateSleeping(scene,dt);ClearForces(scene);}

} // namespace ph
} // namespace Raven
