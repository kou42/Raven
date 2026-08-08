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
bool IsSamePair(const ContactManifold& a,const ContactManifold& b){return(a.A==b.A&&a.B==b.B)||(a.A==b.B&&a.B==b.A);}
math::Quat GetContactOrientation(const TransformComponent& t,const RigidBodyComponent* b){return(b&&b->OrientationInitialized)?b->Orientation.Normalized():PhysicsOrientationFromEuler(t.Rotation);}
math::Vec3 ContactLocalAnchor(const TransformComponent& t,const RigidBodyComponent* b,const math::Vec3& p){return GetContactOrientation(t,b).Conjugate().Rotate(p-t.Position);}
bool FeatureMatches(const ContactFeatureID& c,const ContactFeatureID& p,bool same){if(!c.IsValid()||!p.IsValid())return false;if(same)return c==p;return c.TypeA==p.TypeB&&c.IndexA==p.IndexB&&c.TypeB==p.TypeA&&c.IndexB==p.IndexA;}

bool RayCastSphere(const math::Vec3& o,const math::Vec3& d,float max,const math::Vec3& c,float r,float& f,math::Vec3& n){const math::Vec3 m=o-c;const float a=math::Vec3::Dot(d,d);if(a<=1e-12f)return false;const float cc=math::Vec3::Dot(m,m)-r*r;if(cc<=0){f=0;const float ls=m.LengthSq();n=ls>1e-12f?m/std::sqrt(ls):-d.Normalized();return true;}const float b=math::Vec3::Dot(m,d),disc=b*b-a*cc;if(disc<0)return false;f=(-b-std::sqrt(disc))/a;if(f<0||f>max)return false;n=(o+d*f-c).Normalized();return true;}
bool RayCastPlane(const math::Vec3& o,const math::Vec3& d,float max,const TransformComponent& t,const ColliderComponent& c,float& f,math::Vec3& n){n=c.PlaneNormal.Normalized();if(n.LengthSq()<=1e-12f)return false;const float den=math::Vec3::Dot(n,d),dist=math::Vec3::Dot(n,o-(t.Position+c.Offset));if(std::abs(den)<=1e-8f){if(std::abs(dist)>1e-6f)return false;f=0;return true;}f=-dist/den;if(f<0||f>max)return false;n=den<0?n:-n;return true;}
bool RayCastCollider(const math::Vec3& o,const math::Vec3& d,float max,const TransformComponent& t,const ColliderComponent& c,float& f,math::Vec3& n){if(c.Type==ColliderType::Sphere)return RayCastSphere(o,d,max,t.Position+c.Offset,std::max(c.Radius,0.0f),f,n);if(c.Type==ColliderType::Box){OBB obb{};return ComputeBoxOBB(t,c,obb)&&obb.RayCast(o,d,max,f,&n);}if(c.Type==ColliderType::Plane)return RayCastPlane(o,d,max,t,c,f,n);return false;}
} // namespace

void PhysicsWorld::SetGravity(const math::Vec3& gravity){m_Gravity=gravity;}
const math::Vec3& PhysicsWorld::GetGravity() const{return m_Gravity;}

void PhysicsWorld::ApplyForces(Scene& scene,float dt){for(auto [entity,transform,rigidBody,collider]:scene.View<TransformComponent,RigidBodyComponent,ColliderComponent>()){static_cast<void>(entity);if(rigidBody.Type!=BodyType::Dynamic||rigidBody.IsSleeping||rigidBody.InverseMass<=0.0f)continue;math::Vec3 acceleration{};if(rigidBody.UseGravity)acceleration+=m_Gravity;acceleration+=rigidBody.Force*rigidBody.InverseMass;rigidBody.LinearVelocity+=acceleration*dt;EnsurePhysicsOrientation(transform,rigidBody);rigidBody.AngularVelocity+=(ComputeWorldInverseInertia(&transform,&rigidBody,&collider)*rigidBody.Torque)*dt;}}
void PhysicsWorld::IntegrateVelocities(Scene& scene,float dt){for(auto [entity,rigidBody]:scene.View<RigidBodyComponent>()){static_cast<void>(entity);if(rigidBody.Type!=BodyType::Dynamic||rigidBody.IsSleeping)continue;rigidBody.LinearVelocity*=1.0f/(1.0f+std::max(rigidBody.LinearDamping,0.0f)*dt);rigidBody.AngularVelocity*=1.0f/(1.0f+std::max(rigidBody.AngularDamping,0.0f)*dt);}}
void PhysicsWorld::IntegratePositions(Scene& scene,float dt){for(auto [entity,transform,rigidBody]:scene.View<TransformComponent,RigidBodyComponent>()){static_cast<void>(entity);if(rigidBody.Type==BodyType::Static||(rigidBody.Type==BodyType::Dynamic&&rigidBody.IsSleeping))continue;transform.Position+=rigidBody.LinearVelocity*dt;IntegratePhysicsOrientation(transform,rigidBody,dt);}}

void PhysicsWorld::DetectCollisions(Scene& scene)
{
    m_PreviousManifolds=std::move(m_Manifolds);m_Manifolds.clear();std::vector<BroadPhasePair> pairs;m_BroadPhase.ComputePairs(scene,pairs);
    for(const auto& pair:pairs){if(!scene.IsEntityAlive(pair.A)||!scene.IsEntityAlive(pair.B))continue;auto* ta=scene.TryGetComponent<TransformComponent>(pair.A.GetIndex());auto* tb=scene.TryGetComponent<TransformComponent>(pair.B.GetIndex());auto* ca=scene.TryGetComponent<ColliderComponent>(pair.A.GetIndex());auto* cb=scene.TryGetComponent<ColliderComponent>(pair.B.GetIndex());if(!ta||!tb||!ca||!cb)continue;ContactManifold m{};bool generated=false;if(ca->Type==ColliderType::Sphere&&cb->Type==ColliderType::Sphere)generated=GenerateSphereSphereManifold(pair.A,*ta,*ca,pair.B,*tb,*cb,m);else if(ca->Type==ColliderType::Sphere&&cb->Type==ColliderType::Box)generated=GenerateSphereBoxManifold(pair.A,*ta,*ca,pair.B,*tb,*cb,m);else if(ca->Type==ColliderType::Box&&cb->Type==ColliderType::Sphere)generated=GenerateSphereBoxManifold(pair.B,*tb,*cb,pair.A,*ta,*ca,m);else if(ca->Type==ColliderType::Box&&cb->Type==ColliderType::Box)generated=GenerateBoxBoxManifold(pair.A,*ta,*ca,pair.B,*tb,*cb,m);if(generated)m_Manifolds.push_back(m);}

    // Planeは無限形状なのでDynamic AABB Treeへ入れず、有限形状側から専用Narrow Phaseへ振り分けます。
    // ここでSphereだけに限定しないことが重要で、Boxも同じPlane走査経路を利用します。
    for(auto [shapeEntity,shapeTransform,shapeCollider]:scene.View<TransformComponent,ColliderComponent>())
    {
        if(shapeCollider.Type!=ColliderType::Sphere&&shapeCollider.Type!=ColliderType::Box)continue;
        for(auto [planeEntity,planeTransform,planeCollider]:scene.View<TransformComponent,ColliderComponent>())
        {
            if(planeCollider.Type!=ColliderType::Plane||shapeEntity==planeEntity)continue;
            ContactManifold manifold{};bool generated=false;
            if(shapeCollider.Type==ColliderType::Sphere)
                generated=GenerateSpherePlaneManifold(shapeEntity,shapeTransform,shapeCollider,planeEntity,planeTransform,planeCollider,manifold);
            else
                generated=GenerateBoxPlaneManifold(shapeEntity,shapeTransform,shapeCollider,planeEntity,planeTransform,planeCollider,manifold);
            if(generated)m_Manifolds.push_back(manifold);
        }
    }

    RestorePersistentContacts(scene);m_SolverDebugStatistics.ManifoldCount=static_cast<uint32_t>(m_Manifolds.size());for(const auto& manifold:m_Manifolds){m_SolverDebugStatistics.ContactPointCount+=static_cast<uint32_t>(manifold.PointCount);for(std::size_t i=0;i<manifold.PointCount;++i)m_SolverDebugStatistics.MaxPenetration=std::max(m_SolverDebugStatistics.MaxPenetration,std::max(manifold.Points[i].Penetration,0.0f));}
}

void PhysicsWorld::RestorePersistentContacts(Scene& scene)
{
    constexpr float WorldDistanceSq=0.05f*0.05f,LocalAnchorDistanceSq=0.075f*0.075f,NormalThreshold=0.9f;
    for(auto& current:m_Manifolds){if(current.IsTrigger||current.PointCount==0)continue;const ContactManifold* previous=nullptr;bool sameOrder=true;for(const auto& candidate:m_PreviousManifolds){if(candidate.IsTrigger||candidate.PointCount==0||!IsSamePair(current,candidate))continue;previous=&candidate;sameOrder=(current.A==candidate.A&&current.B==candidate.B);break;}if(!previous)continue;const auto* ta=scene.TryGetComponent<TransformComponent>(current.A.GetIndex());const auto* tb=scene.TryGetComponent<TransformComponent>(current.B.GetIndex());if(!ta||!tb)continue;const auto* ba=scene.TryGetComponent<RigidBodyComponent>(current.A.GetIndex());const auto* bb=scene.TryGetComponent<RigidBodyComponent>(current.B.GetIndex());if(math::Vec3::Dot(current.Normal,sameOrder?previous->Normal:-previous->Normal)<NormalThreshold)continue;for(std::size_t i=0;i<current.PointCount;++i){auto& cp=current.Points[i];const math::Vec3 la=ContactLocalAnchor(*ta,ba,cp.Position),lb=ContactLocalAnchor(*tb,bb,cp.Position);const ContactPoint* best=nullptr;float bestDist=std::numeric_limits<float>::max();for(std::size_t j=0;j<previous->PointCount;++j){const auto& pp=previous->Points[j];if(FeatureMatches(cp.Feature,pp.Feature,sameOrder)){best=&pp;break;}const float ds=(cp.Position-pp.Position).LengthSq();if(ds<bestDist&&ds<=WorldDistanceSq){best=&pp;bestDist=ds;}}if(best){cp.AccumulatedNormalImpulse=best->AccumulatedNormalImpulse;cp.AccumulatedTangentImpulse=best->AccumulatedTangentImpulse;cp.CachedTangent=best->CachedTangent;}cp.LocalAnchorA=la;cp.LocalAnchorB=lb;cp.InitialSeparation=-cp.Penetration;cp.PositionAnchorsInitialized=true;}}
}

void PhysicsWorld::Step(Scene& scene,float dt){if(dt<=0)return;m_SolverDebugStatistics={};ApplyForces(scene,dt);IntegrateVelocities(scene,dt);IntegratePositions(scene,dt);m_BroadPhase.Update(scene,dt);DetectCollisions(scene);SolveContactManifolds(scene,m_Manifolds,dt,m_SolverSettings,&m_SolverDebugStatistics);UpdateSleeping(scene,dt);ClearAccumulators(scene);}
void PhysicsWorld::ClearAccumulators(Scene& scene){for(auto [entity,body]:scene.View<RigidBodyComponent>()){static_cast<void>(entity);body.Force={};body.Torque={};}}
void PhysicsWorld::UpdateSleeping(Scene& scene,float dt){for(auto [entity,body]:scene.View<RigidBodyComponent>()){static_cast<void>(entity);if(body.Type!=BodyType::Dynamic||!body.AllowSleep){body.SleepTimer=0;continue;}if(body.LinearVelocity.LengthSq()<m_SleepLinearThreshold*m_SleepLinearThreshold&&body.AngularVelocity.LengthSq()<m_SleepAngularThreshold*m_SleepAngularThreshold){body.SleepTimer+=dt;if(body.SleepTimer>=m_TimeToSleep){body.IsSleeping=true;body.LinearVelocity={};body.AngularVelocity={};}}else{body.SleepTimer=0;body.IsSleeping=false;}}}
void PhysicsWorld::AddForce(Scene& scene,Entity entity,const math::Vec3& force){if(auto* b=scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex())){b->Force+=force;WakeRigidBody(*b);}}
void PhysicsWorld::AddTorque(Scene& scene,Entity entity,const math::Vec3& torque){if(auto* b=scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex())){b->Torque+=torque;WakeRigidBody(*b);}}
void PhysicsWorld::AddImpulse(Scene& scene,Entity entity,const math::Vec3& impulse){auto* b=scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());if(!b||b->Type!=BodyType::Dynamic||b->InverseMass<=0)return;b->LinearVelocity+=impulse*b->InverseMass;WakeRigidBody(*b);}
void PhysicsWorld::AddImpulseAtPoint(Scene& scene,Entity entity,const math::Vec3& impulse,const math::Vec3& point){auto* t=scene.TryGetComponent<TransformComponent>(entity.GetIndex());auto* b=scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());auto* c=scene.TryGetComponent<ColliderComponent>(entity.GetIndex());if(!t||!b||b->Type!=BodyType::Dynamic||b->InverseMass<=0)return;EnsurePhysicsOrientation(*t,*b);b->LinearVelocity+=impulse*b->InverseMass;b->AngularVelocity+=ComputeWorldInverseInertia(t,b,c)*math::Vec3::Cross(point-t->Position,impulse);WakeRigidBody(*b);}
void PhysicsWorld::AddForceAtPoint(Scene& scene,Entity entity,const math::Vec3& force,const math::Vec3& point){auto* t=scene.TryGetComponent<TransformComponent>(entity.GetIndex());auto* b=scene.TryGetComponent<RigidBodyComponent>(entity.GetIndex());if(!t||!b||b->Type!=BodyType::Dynamic)return;b->Force+=force;b->Torque+=math::Vec3::Cross(point-t->Position,force);WakeRigidBody(*b);}

bool PhysicsWorld::RayCast(Scene& scene,const math::Vec3& origin,const math::Vec3& direction,float maxFraction,PhysicsRayCastHit& outHit)const{outHit={};if(direction.LengthSq()<=1e-12f||maxFraction<0)return false;float closest=maxFraction;for(auto [entity,transform,collider]:scene.View<TransformComponent,ColliderComponent>()){float f=0;math::Vec3 n{};if(!RayCastCollider(origin,direction,closest,transform,collider,f,n))continue;closest=f;outHit.EntityHit=entity;outHit.Fraction=f;outHit.Point=origin+direction*f;outHit.Normal=n;}return static_cast<bool>(outHit.EntityHit);}
const std::vector<ContactManifold>& PhysicsWorld::GetContactManifolds()const{return m_Manifolds;}
const SolverDebugStatistics& PhysicsWorld::GetSolverDebugStatistics()const{return m_SolverDebugStatistics;}
void PhysicsWorld::SetSolverSettings(const ContactSolverSettings& settings){m_SolverSettings=settings;}
const ContactSolverSettings& PhysicsWorld::GetSolverSettings()const{return m_SolverSettings;}
void PhysicsWorld::SetSleepSettings(float linearThreshold,float angularThreshold,float timeToSleep){m_SleepLinearThreshold=std::max(linearThreshold,0.0f);m_SleepAngularThreshold=std::max(angularThreshold,0.0f);m_TimeToSleep=std::max(timeToSleep,0.0f);}

} // namespace ph
} // namespace Raven
