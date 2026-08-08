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
void WakeRigidBody(RigidBodyComponent& rigidBody){rigidBody.IsSleeping=false;rigidBody.SleepTimer=0.0f;}
bool IsSamePair(const ContactManifold&a,const ContactManifold&b){return(a.A==b.A&&a.B==b.B)||(a.A==b.B&&a.B==b.A);}

bool RayCastSphere(const math::Vec3& origin,const math::Vec3& direction,float maxFraction,const math::Vec3& center,float radius,float& outFraction,math::Vec3& outNormal)
{
    const math::Vec3 m=origin-center;const float a=math::Vec3::Dot(direction,direction);if(a<=1e-12f)return false;const float c=math::Vec3::Dot(m,m)-radius*radius;
    if(c<=0.0f){outFraction=0.0f;const float ls=m.LengthSq();outNormal=ls>1e-12f?m/std::sqrt(ls):-direction.Normalized();return true;}
    const float b=math::Vec3::Dot(m,direction);const float disc=b*b-a*c;if(disc<0.0f)return false;const float f=(-b-std::sqrt(disc))/a;if(f<0.0f||f>maxFraction)return false;outFraction=f;outNormal=(origin+direction*f-center).Normalized();return true;
}

bool RayCastPlane(const math::Vec3& origin,const math::Vec3& direction,float maxFraction,const TransformComponent& transform,const ColliderComponent& collider,float& outFraction,math::Vec3& outNormal)
{
    math::Vec3 normal=collider.PlaneNormal.Normalized();if(normal.LengthSq()<=1e-12f)return false;const float denominator=math::Vec3::Dot(normal,direction);const float distance=math::Vec3::Dot(normal,origin-(transform.Position+collider.Offset));
    if(std::abs(denominator)<=1e-8f){if(std::abs(distance)>1e-6f)return false;outFraction=0.0f;outNormal=normal;return true;}
    const float fraction=-distance/denominator;if(fraction<0.0f||fraction>maxFraction)return false;outFraction=fraction;outNormal=denominator<0.0f?normal:-normal;return true;
}

bool RayCastCollider(const math::Vec3& origin,const math::Vec3& direction,float maxFraction,const TransformComponent& transform,const ColliderComponent& collider,float& outFraction,math::Vec3& outNormal)
{
    if(collider.Type==ColliderType::Sphere)return RayCastSphere(origin,direction,maxFraction,transform.Position+collider.Offset,std::max(collider.Radius,0.0f),outFraction,outNormal);
    if(collider.Type==ColliderType::Box)
    {
        // Broad Phaseでは回転Boxを包むAABBを使いますが、最終Ray hitはOBBそのものへ
        // Slab testします。これにより回転AABBの空き角をクリックして誤hitする問題を防ぎます。
        OBB obb{};return ComputeBoxOBB(transform,collider,obb)&&obb.RayCast(origin,direction,maxFraction,outFraction,&outNormal);
    }
    if(collider.Type==ColliderType::Plane)return RayCastPlane(origin,direction,maxFraction,transform,collider,outFraction,outNormal);
    return false;
}
} // namespace

void PhysicsWorld::SetGravity(const math::Vec3& gravity){m_Gravity=gravity;}
const math::Vec3& PhysicsWorld::GetGravity()const{return m_Gravity;}

void PhysicsWorld::ApplyForces(Scene& scene,float dt)
{
    for(auto[entity,transform,rigidBody,collider]:scene.View<TransformComponent,RigidBodyComponent,ColliderComponent>())
    {static_cast<void>(entity);if(rigidBody.Type!=BodyType::Dynamic||rigidBody.IsSleeping||rigidBody.InverseMass<=0.0f)continue;math::Vec3 acceleration{};if(rigidBody.UseGravity)acceleration+=m_Gravity;acceleration+=rigidBody.Force*rigidBody.InverseMass;rigidBody.LinearVelocity+=acceleration*dt;EnsurePhysicsOrientation(transform,rigidBody);const math::Mat3 inverseInertia=ComputeWorldInverseInertia(&transform,&rigidBody,&collider);rigidBody.AngularVelocity+=(inverseInertia*rigidBody.Torque)*dt;}
}
void PhysicsWorld::IntegrateVelocities(Scene& scene,float dt){for(auto[entity,rb]:scene.View<RigidBodyComponent>()){static_cast<void>(entity);if(rb.Type!=BodyType::Dynamic||rb.IsSleeping)continue;rb.LinearVelocity*=1.0f/(1.0f+std::max(rb.LinearDamping,0.0f)*dt);rb.AngularVelocity*=1.0f/(1.0f+std::max(rb.AngularDamping,0.0f)*dt);}}
void PhysicsWorld::IntegratePositions(Scene& scene,float dt){for(auto[entity,t,rb]:scene.View<TransformComponent,RigidBodyComponent>()){static_cast<void>(entity);if(rb.Type==BodyType::Static||(rb.Type==BodyType::Dynamic&&rb.IsSleeping))continue;t.Position+=rb.LinearVelocity*dt;IntegratePhysicsOrientation(t,rb,dt);}}

void PhysicsWorld::DetectCollisions(Scene& scene)
{
    m_PreviousManifolds=std::move(m_Manifolds);m_Manifolds.clear();std::vector<BroadPhasePair> pairs;m_BroadPhase.ComputePairs(scene,pairs);
    for(const auto&pair:pairs){if(!scene.IsEntityAlive(pair.A)||!scene.IsEntityAlive(pair.B))continue;auto*ta=scene.TryGetComponent<TransformComponent>(pair.A.GetIndex());auto*tb=scene.TryGetComponent<TransformComponent>(pair.B.GetIndex());auto*ca=scene.TryGetComponent<ColliderComponent>(pair.A.GetIndex());auto*cb=scene.TryGetComponent<ColliderComponent>(pair.B.GetIndex());if(!ta||!tb||!ca||!cb)continue;ContactManifold m{};bool g=false;if(ca->Type==ColliderType::Sphere&&cb->Type==ColliderType::Sphere)g=GenerateSphereSphereManifold(pair.A,*ta,*ca,pair.B,*tb,*cb,m);else if(ca->Type==ColliderType::Sphere&&cb->Type==ColliderType::Box)g=GenerateSphereBoxManifold(pair.A,*ta,*ca,pair.B,*tb,*cb,m);else if(ca->Type==ColliderType::Box&&cb->Type==ColliderType::Sphere)g=GenerateSphereBoxManifold(pair.B,*tb,*cb,pair.A,*ta,*ca,m);else if(ca->Type==ColliderType::Box&&cb->Type==ColliderType::Box)g=GenerateBoxBoxManifold(pair.A,*ta,*ca,pair.B,*tb,*cb,m);if(g)m_Manifolds.push_back(m);}
    for(auto[se,st,sc]:scene.View<TransformComponent,ColliderComponent>()){if(sc.Type!=ColliderType::Sphere)continue;for(auto[pe,pt,pc]:scene.View<TransformComponent,ColliderComponent>()){if(pc.Type!=ColliderType::Plane||se==pe)continue;ContactManifold m{};if(GenerateSpherePlaneManifold(se,st,sc,pe,pt,pc,m))m_Manifolds.push_back(m);}}
    RestorePersistentContacts();m_SolverDebugStatistics.ManifoldCount=static_cast<uint32_t>(m_Manifolds.size());for(const ContactManifold&m:m_Manifolds){m_SolverDebugStatistics.ContactPointCount+=static_cast<uint32_t>(m.PointCount);for(std::size_t i=0;i<m.PointCount;++i)m_SolverDebugStatistics.MaxPenetration=std::max(m_SolverDebugStatistics.MaxPenetration,std::max(m.Points[i].Penetration,0.0f));}
}

void PhysicsWorld::RestorePersistentContacts()
{
    constexpr float matchDistanceSq=0.05f*0.05f;constexpr float normalThreshold=0.9f;for(auto&current:m_Manifolds){if(current.IsTrigger||current.PointCount==0)continue;const ContactManifold*previous=nullptr;for(const auto&candidate:m_PreviousManifolds){if(!candidate.IsTrigger&&candidate.PointCount>0&&IsSamePair(current,candidate)){const bool sameOrder=current.A==candidate.A&&current.B==candidate.B;const math::Vec3 previousNormal=sameOrder?candidate.Normal:-candidate.Normal;if(math::Vec3::Dot(current.Normal.Normalized(),previousNormal.Normalized())>=normalThreshold){previous=&candidate;break;}}}if(!previous)continue;++m_SolverDebugStatistics.PersistentManifoldCount;const bool sameOrder=current.A==previous->A&&current.B==previous->B;bool used[ContactManifold::MaxContactPointCount]{};for(std::size_t i=0;i<current.PointCount;++i){std::size_t best=ContactManifold::MaxContactPointCount;float bestDist=matchDistanceSq;for(std::size_t j=0;j<previous->PointCount;++j){if(used[j])continue;const float d=(current.Points[i].Position-previous->Points[j].Position).LengthSq();if(d<=bestDist){bestDist=d;best=j;}}if(best==ContactManifold::MaxContactPointCount)continue;used[best]=true;const auto&s=previous->Points[best];auto&t=current.Points[i];t.AccumulatedNormalImpulse=s.AccumulatedNormalImpulse;t.AccumulatedTangentImpulse=sameOrder?s.AccumulatedTangentImpulse:-s.AccumulatedTangentImpulse;t.CachedTangent=sameOrder?s.CachedTangent:-s.CachedTangent;++m_SolverDebugStatistics.PersistentContactPointCount;}}
}

bool PhysicsWorld::RayCast(Scene& scene,const math::Vec3& origin,const math::Vec3& direction,float maxFraction,PhysicsRayCastHit& outHit)
{
    if(maxFraction<0.0f||direction.LengthSq()<=1e-12f)return false;bool hit=false;float closest=maxFraction;PhysicsRayCastHit result{};
    m_BroadPhase.RayCast(scene,origin,direction,maxFraction,[&](Entity entity,uint32_t proxyIndex,float fraction,const math::Vec3& normal,float currentClosest)->float{static_cast<void>(proxyIndex);static_cast<void>(fraction);static_cast<void>(normal);auto*t=scene.TryGetComponent<TransformComponent>(entity.GetIndex());auto*c=scene.TryGetComponent<ColliderComponent>(entity.GetIndex());if(!t||!c||c->Type==ColliderType::Plane)return currentClosest;float f=0.0f;math::Vec3 n{};if(!RayCastCollider(origin,direction,currentClosest,*t,*c,f,n))return currentClosest;if(!hit||f<closest){hit=true;closest=f;result.HitEntity=entity;result.Fraction=f;result.Point=origin+direction*f;result.Normal=n;}return closest;});
    for(auto[entity,t,c]:scene.View<TransformComponent,ColliderComponent>()){if(c.Type!=ColliderType::Plane)continue;float f=0;math::Vec3 n{};if(RayCastPlane(origin,direction,closest,t,c,f,n)&&(!hit||f<closest)){hit=true;closest=f;result.HitEntity=entity;result.Fraction=f;result.Point=origin+direction*f;result.Normal=n;}}
    if(hit)outHit=result;return hit;
}
void PhysicsWorld::QueryAABB(Scene& scene,const AABB&q,std::vector<Entity>&out){out.clear();if(!q.IsValid())return;m_BroadPhase.QueryAABB(scene,q,[&](Entity e,uint32_t p)->bool{static_cast<void>(p);auto*t=scene.TryGetComponent<TransformComponent>(e.GetIndex());auto*c=scene.TryGetComponent<ColliderComponent>(e.GetIndex());if(!t||!c)return true;AABB b{};if(ComputeColliderAABB(*t,*c,b)&&b.Overlaps(q))out.push_back(e);return true;});}
void PhysicsWorld::SolveCollisions(Scene& scene,float dt){if(m_SolverSettings.EnableWarmStart){for(const ContactManifold&m:m_Manifolds)for(std::size_t i=0;i<m.PointCount;++i)if(!m.IsTrigger&&(m.Points[i].AccumulatedNormalImpulse>0.0f||std::abs(m.Points[i].AccumulatedTangentImpulse)>1e-8f))++m_SolverDebugStatistics.WarmStartedConstraintCount;}m_SolverDebugStatistics.VelocityIterations=std::max(m_SolverSettings.VelocityIterations,1u);SolveContactManifolds(scene,m_Manifolds,dt,m_SolverSettings);UpdateSolverDebugStatisticsAfterSolve();}
void PhysicsWorld::UpdateSolverDebugStatisticsAfterSolve(){for(const ContactManifold&m:m_Manifolds)for(std::size_t i=0;i<m.PointCount;++i){const ContactPoint&p=m.Points[i];m_SolverDebugStatistics.MaxNormalImpulse=std::max(m_SolverDebugStatistics.MaxNormalImpulse,std::max(p.AccumulatedNormalImpulse,0.0f));m_SolverDebugStatistics.MaxFrictionImpulse=std::max(m_SolverDebugStatistics.MaxFrictionImpulse,std::abs(p.AccumulatedTangentImpulse));}}
void PhysicsWorld::UpdateSleeping(Scene& scene,float dt){for(auto[entity,rb]:scene.View<RigidBodyComponent>()){static_cast<void>(entity);if(rb.Type!=BodyType::Dynamic)continue;if(!rb.AllowSleep){WakeRigidBody(rb);continue;}if(rb.IsSleeping){rb.LinearVelocity={};rb.AngularVelocity={};continue;}const float lt=std::max(rb.SleepThreshold,0.0f),at=std::max(rb.AngularSleepThreshold,0.0f);if(rb.LinearVelocity.LengthSq()<=lt*lt&&rb.AngularVelocity.LengthSq()<=at*at){rb.SleepTimer+=dt;const float rt=std::max(rb.SleepTimeThreshold,0.0f);if(rb.SleepTimer>=rt){rb.IsSleeping=true;rb.LinearVelocity={};rb.AngularVelocity={};rb.SleepTimer=rt;}}else rb.SleepTimer=0.0f;}}
void PhysicsWorld::ClearForces(Scene& scene){for(auto[e,rb]:scene.View<RigidBodyComponent>()){static_cast<void>(e);rb.Force={};rb.Torque={};}}
void PhysicsWorld::AddForce(Scene&scene,Entity entity,const math::Vec3&force){if(!scene.IsEntityAlive(entity))return;auto*rb=scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());if(!rb||rb->Type!=BodyType::Dynamic||rb->InverseMass<=0.0f)return;WakeRigidBody(*rb);rb->Force+=force;}
void PhysicsWorld::AddImpulse(Scene&scene,Entity entity,const math::Vec3&impulse){if(!scene.IsEntityAlive(entity))return;auto*rb=scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());if(!rb||rb->Type!=BodyType::Dynamic||rb->InverseMass<=0.0f)return;WakeRigidBody(*rb);rb->LinearVelocity+=impulse*rb->InverseMass;}
void PhysicsWorld::WakeUp(Scene&scene,Entity entity){if(!scene.IsEntityAlive(entity))return;auto*rb=scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());if(rb&&rb->Type==BodyType::Dynamic)WakeRigidBody(*rb);}
void PhysicsWorld::Step(Scene&scene,float dt){if(dt<=0.0f)return;m_SolverDebugStatistics.Reset();ApplyForces(scene,dt);IntegrateVelocities(scene,dt);IntegratePositions(scene,dt);DetectCollisions(scene);SolveCollisions(scene,dt);UpdateSleeping(scene,dt);ClearForces(scene);}
} // namespace ph
} // namespace Raven
