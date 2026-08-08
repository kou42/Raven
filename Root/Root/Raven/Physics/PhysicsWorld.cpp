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
math::Quat GetContactOrientation(const TransformComponent&transform,const RigidBodyComponent*body){return(body&&body->OrientationInitialized)?body->Orientation.Normalized():PhysicsOrientationFromEuler(transform.Rotation);}
math::Vec3 ContactLocalAnchor(const TransformComponent&transform,const RigidBodyComponent*body,const math::Vec3&worldPoint){return GetContactOrientation(transform,body).Conjugate().Rotate(worldPoint-transform.Position);}

bool FeatureMatches(const ContactFeatureID&current,const ContactFeatureID&previous,bool sameOrder)
{
    if(!current.IsValid()||!previous.IsValid())return false;
    if(sameOrder)return current==previous;
    return current.TypeA==previous.TypeB&&current.IndexA==previous.IndexB&&current.TypeB==previous.TypeA&&current.IndexB==previous.IndexA;
}

bool RayCastSphere(const math::Vec3& origin,const math::Vec3& direction,float maxFraction,const math::Vec3& center,float radius,float& outFraction,math::Vec3& outNormal){const math::Vec3 m=origin-center;const float a=math::Vec3::Dot(direction,direction);if(a<=1e-12f)return false;const float c=math::Vec3::Dot(m,m)-radius*radius;if(c<=0){outFraction=0;const float ls=m.LengthSq();outNormal=ls>1e-12f?m/std::sqrt(ls):-direction.Normalized();return true;}const float b=math::Vec3::Dot(m,direction),disc=b*b-a*c;if(disc<0)return false;const float f=(-b-std::sqrt(disc))/a;if(f<0||f>maxFraction)return false;outFraction=f;outNormal=(origin+direction*f-center).Normalized();return true;}
bool RayCastPlane(const math::Vec3& origin,const math::Vec3& direction,float maxFraction,const TransformComponent& transform,const ColliderComponent& collider,float& outFraction,math::Vec3& outNormal){math::Vec3 n=collider.PlaneNormal.Normalized();if(n.LengthSq()<=1e-12f)return false;const float d=math::Vec3::Dot(n,direction),distance=math::Vec3::Dot(n,origin-(transform.Position+collider.Offset));if(std::abs(d)<=1e-8f){if(std::abs(distance)>1e-6f)return false;outFraction=0;outNormal=n;return true;}const float f=-distance/d;if(f<0||f>maxFraction)return false;outFraction=f;outNormal=d<0?n:-n;return true;}
bool RayCastCollider(const math::Vec3&o,const math::Vec3&d,float max,const TransformComponent&t,const ColliderComponent&c,float&f,math::Vec3&n){if(c.Type==ColliderType::Sphere)return RayCastSphere(o,d,max,t.Position+c.Offset,std::max(c.Radius,0.0f),f,n);if(c.Type==ColliderType::Box){OBB obb{};return ComputeBoxOBB(t,c,obb)&&obb.RayCast(o,d,max,f,&n);}if(c.Type==ColliderType::Plane)return RayCastPlane(o,d,max,t,c,f,n);return false;}
} // namespace

void PhysicsWorld::SetGravity(const math::Vec3&g){m_Gravity=g;}const math::Vec3&PhysicsWorld::GetGravity()const{return m_Gravity;}
void PhysicsWorld::ApplyForces(Scene&scene,float dt){for(auto[e,t,rb,c]:scene.View<TransformComponent,RigidBodyComponent,ColliderComponent>()){static_cast<void>(e);if(rb.Type!=BodyType::Dynamic||rb.IsSleeping||rb.InverseMass<=0)continue;math::Vec3 a{};if(rb.UseGravity)a+=m_Gravity;a+=rb.Force*rb.InverseMass;rb.LinearVelocity+=a*dt;EnsurePhysicsOrientation(t,rb);rb.AngularVelocity+=(ComputeWorldInverseInertia(&t,&rb,&c)*rb.Torque)*dt;}}
void PhysicsWorld::IntegrateVelocities(Scene&scene,float dt){for(auto[e,rb]:scene.View<RigidBodyComponent>()){static_cast<void>(e);if(rb.Type!=BodyType::Dynamic||rb.IsSleeping)continue;rb.LinearVelocity*=1.0f/(1.0f+std::max(rb.LinearDamping,0.0f)*dt);rb.AngularVelocity*=1.0f/(1.0f+std::max(rb.AngularDamping,0.0f)*dt);}}
void PhysicsWorld::IntegratePositions(Scene&scene,float dt){for(auto[e,t,rb]:scene.View<TransformComponent,RigidBodyComponent>()){static_cast<void>(e);if(rb.Type==BodyType::Static||(rb.Type==BodyType::Dynamic&&rb.IsSleeping))continue;t.Position+=rb.LinearVelocity*dt;IntegratePhysicsOrientation(t,rb,dt);}}
void PhysicsWorld::DetectCollisions(Scene&scene){m_PreviousManifolds=std::move(m_Manifolds);m_Manifolds.clear();std::vector<BroadPhasePair>pairs;m_BroadPhase.ComputePairs(scene,pairs);for(const auto&p:pairs){if(!scene.IsEntityAlive(p.A)||!scene.IsEntityAlive(p.B))continue;auto*ta=scene.TryGetComponent<TransformComponent>(p.A.GetIndex());auto*tb=scene.TryGetComponent<TransformComponent>(p.B.GetIndex());auto*ca=scene.TryGetComponent<ColliderComponent>(p.A.GetIndex());auto*cb=scene.TryGetComponent<ColliderComponent>(p.B.GetIndex());if(!ta||!tb||!ca||!cb)continue;ContactManifold m{};bool g=false;if(ca->Type==ColliderType::Sphere&&cb->Type==ColliderType::Sphere)g=GenerateSphereSphereManifold(p.A,*ta,*ca,p.B,*tb,*cb,m);else if(ca->Type==ColliderType::Sphere&&cb->Type==ColliderType::Box)g=GenerateSphereBoxManifold(p.A,*ta,*ca,p.B,*tb,*cb,m);else if(ca->Type==ColliderType::Box&&cb->Type==ColliderType::Sphere)g=GenerateSphereBoxManifold(p.B,*tb,*cb,p.A,*ta,*ca,m);else if(ca->Type==ColliderType::Box&&cb->Type==ColliderType::Box)g=GenerateBoxBoxManifold(p.A,*ta,*ca,p.B,*tb,*cb,m);if(g)m_Manifolds.push_back(m);}for(auto[se,st,sc]:scene.View<TransformComponent,ColliderComponent>()){if(sc.Type!=ColliderType::Sphere)continue;for(auto[pe,pt,pc]:scene.View<TransformComponent,ColliderComponent>()){if(pc.Type!=ColliderType::Plane||se==pe)continue;ContactManifold m{};if(GenerateSpherePlaneManifold(se,st,sc,pe,pt,pc,m))m_Manifolds.push_back(m);}}RestorePersistentContacts(scene);m_SolverDebugStatistics.ManifoldCount=static_cast<uint32_t>(m_Manifolds.size());for(const auto&m:m_Manifolds){m_SolverDebugStatistics.ContactPointCount+=static_cast<uint32_t>(m.PointCount);for(std::size_t i=0;i<m.PointCount;++i)m_SolverDebugStatistics.MaxPenetration=std::max(m_SolverDebugStatistics.MaxPenetration,std::max(m.Points[i].Penetration,0.0f));}}

void PhysicsWorld::RestorePersistentContacts(Scene& scene)
{
    constexpr float WorldDistanceSq=.05f*.05f;
    constexpr float LocalAnchorDistanceSq=.075f*.075f;
    constexpr float NormalThreshold=.9f;
    for(auto&cur:m_Manifolds)
    {
        if(cur.IsTrigger||cur.PointCount==0)continue;const ContactManifold*prev=nullptr;bool sameOrder=true;
        for(const auto&candidate:m_PreviousManifolds){if(candidate.IsTrigger||candidate.PointCount==0||!IsSamePair(cur,candidate))continue;const bool same=cur.A==candidate.A&&cur.B==candidate.B;const math::Vec3 pn=same?candidate.Normal:-candidate.Normal;if(math::Vec3::Dot(cur.Normal.Normalized(),pn.Normalized())>=NormalThreshold){prev=&candidate;sameOrder=same;break;}}
        if(!prev)continue;++m_SolverDebugStatistics.PersistentManifoldCount;
        auto*ta=scene.TryGetComponent<TransformComponent>(cur.A.GetIndex());auto*tb=scene.TryGetComponent<TransformComponent>(cur.B.GetIndex());auto*ba=scene.TryGetComponent<RigidBodyComponent>(cur.A.GetIndex());auto*bb=scene.TryGetComponent<RigidBodyComponent>(cur.B.GetIndex());
        bool used[ContactManifold::MaxContactPointCount]{};
        for(std::size_t i=0;i<cur.PointCount;++i)
        {
            std::size_t best=ContactManifold::MaxContactPointCount;float bestScore=std::numeric_limits<float>::max();
            const math::Vec3 currentLocalA=ta?ContactLocalAnchor(*ta,ba,cur.Points[i].Position):math::Vec3{};const math::Vec3 currentLocalB=tb?ContactLocalAnchor(*tb,bb,cur.Points[i].Position):math::Vec3{};
            for(std::size_t j=0;j<prev->PointCount;++j)
            {
                if(used[j])continue;const ContactPoint&old=prev->Points[j];float score=std::numeric_limits<float>::max();

                // 1) Contact Feature IDを最優先します。
                // Face/Edge/Vertexの組が一致していれば、回転でworld pointが大きく動いても
                // 同じ幾何feature上の接触として確実にWarm Startできます。
                if(FeatureMatches(cur.Points[i].Feature,old.Feature,sameOrder))
                {
                    score=0.0f;
                }

                // 2) Featureが無効・不一致ならLocal Anchorへfallback。
                if(score==std::numeric_limits<float>::max()&&old.PositionAnchorsInitialized&&ta&&tb)
                {
                    const math::Vec3 oldA=sameOrder?old.LocalAnchorA:old.LocalAnchorB;const math::Vec3 oldB=sameOrder?old.LocalAnchorB:old.LocalAnchorA;
                    const float da=(currentLocalA-oldA).LengthSq(),db=(currentLocalB-oldB).LengthSq();
                    if(da<=LocalAnchorDistanceSq&&db<=LocalAnchorDistanceSq)score=1.0f+da+db;
                }

                // 3) Sphere等Feature IDを持たないContact向けにworld distanceを最後に残します。
                if(score==std::numeric_limits<float>::max())
                {
                    const float d=(cur.Points[i].Position-old.Position).LengthSq();
                    if(d<=WorldDistanceSq)score=2.0f+d;
                }
                if(score<bestScore){bestScore=score;best=j;}
            }
            if(best==ContactManifold::MaxContactPointCount)continue;used[best]=true;const auto&src=prev->Points[best];auto&dst=cur.Points[i];dst.AccumulatedNormalImpulse=src.AccumulatedNormalImpulse;dst.AccumulatedTangentImpulse=sameOrder?src.AccumulatedTangentImpulse:-src.AccumulatedTangentImpulse;dst.CachedTangent=sameOrder?src.CachedTangent:-src.CachedTangent;++m_SolverDebugStatistics.PersistentContactPointCount;
        }
    }
}

bool PhysicsWorld::RayCast(Scene&scene,const math::Vec3&o,const math::Vec3&d,float max,PhysicsRayCastHit&out){if(max<0||d.LengthSq()<=1e-12f)return false;bool hit=false;float closest=max;PhysicsRayCastHit r{};m_BroadPhase.RayCast(scene,o,d,max,[&](Entity e,uint32_t,float,const math::Vec3&,float current)->float{auto*t=scene.TryGetComponent<TransformComponent>(e.GetIndex());auto*c=scene.TryGetComponent<ColliderComponent>(e.GetIndex());if(!t||!c||c->Type==ColliderType::Plane)return current;float f;math::Vec3 n;if(!RayCastCollider(o,d,current,*t,*c,f,n))return current;if(!hit||f<closest){hit=true;closest=f;r.HitEntity=e;r.Fraction=f;r.Point=o+d*f;r.Normal=n;}return closest;});for(auto[e,t,c]:scene.View<TransformComponent,ColliderComponent>()){if(c.Type!=ColliderType::Plane)continue;float f;math::Vec3 n;if(RayCastPlane(o,d,closest,t,c,f,n)&&(!hit||f<closest)){hit=true;closest=f;r.HitEntity=e;r.Fraction=f;r.Point=o+d*f;r.Normal=n;}}if(hit)out=r;return hit;}
void PhysicsWorld::QueryAABB(Scene&scene,const AABB&q,std::vector<Entity>&out){out.clear();if(!q.IsValid())return;m_BroadPhase.QueryAABB(scene,q,[&](Entity e,uint32_t)->bool{auto*t=scene.TryGetComponent<TransformComponent>(e.GetIndex());auto*c=scene.TryGetComponent<ColliderComponent>(e.GetIndex());if(!t||!c)return true;AABB b{};if(ComputeColliderAABB(*t,*c,b)&&b.Overlaps(q))out.push_back(e);return true;});}
void PhysicsWorld::SolveCollisions(Scene&scene,float dt){if(m_SolverSettings.EnableWarmStart)for(const auto&m:m_Manifolds)for(std::size_t i=0;i<m.PointCount;++i)if(!m.IsTrigger&&(m.Points[i].AccumulatedNormalImpulse>0||std::abs(m.Points[i].AccumulatedTangentImpulse)>1e-8f))++m_SolverDebugStatistics.WarmStartedConstraintCount;m_SolverDebugStatistics.VelocityIterations=std::max(m_SolverSettings.VelocityIterations,1u);SolveContactManifolds(scene,m_Manifolds,dt,m_SolverSettings);UpdateSolverDebugStatisticsAfterSolve();}
void PhysicsWorld::UpdateSolverDebugStatisticsAfterSolve(){for(const auto&m:m_Manifolds)for(std::size_t i=0;i<m.PointCount;++i){const auto&p=m.Points[i];m_SolverDebugStatistics.MaxNormalImpulse=std::max(m_SolverDebugStatistics.MaxNormalImpulse,std::max(p.AccumulatedNormalImpulse,0.0f));m_SolverDebugStatistics.MaxFrictionImpulse=std::max(m_SolverDebugStatistics.MaxFrictionImpulse,std::abs(p.AccumulatedTangentImpulse));}}
void PhysicsWorld::UpdateSleeping(Scene&scene,float dt){for(auto[e,rb]:scene.View<RigidBodyComponent>()){static_cast<void>(e);if(rb.Type!=BodyType::Dynamic)continue;if(!rb.AllowSleep){WakeRigidBody(rb);continue;}if(rb.IsSleeping){rb.LinearVelocity={};rb.AngularVelocity={};continue;}const float l=std::max(rb.SleepThreshold,0.0f),a=std::max(rb.AngularSleepThreshold,0.0f);if(rb.LinearVelocity.LengthSq()<=l*l&&rb.AngularVelocity.LengthSq()<=a*a){rb.SleepTimer+=dt;const float t=std::max(rb.SleepTimeThreshold,0.0f);if(rb.SleepTimer>=t){rb.IsSleeping=true;rb.LinearVelocity={};rb.AngularVelocity={};rb.SleepTimer=t;}}else rb.SleepTimer=0;}}
void PhysicsWorld::ClearForces(Scene&scene){for(auto[e,rb]:scene.View<RigidBodyComponent>()){static_cast<void>(e);rb.Force={};rb.Torque={};}}
void PhysicsWorld::AddForce(Scene&scene,Entity e,const math::Vec3&f){if(!scene.IsEntityAlive(e))return;auto*rb=scene.TryGetComponent<RigidBodyComponent>(e.GetIndex());if(!rb||rb->Type!=BodyType::Dynamic||rb->InverseMass<=0)return;WakeRigidBody(*rb);rb->Force+=f;}
void PhysicsWorld::AddImpulse(Scene&scene,Entity e,const math::Vec3&i){if(!scene.IsEntityAlive(e))return;auto*rb=scene.TryGetComponent<RigidBodyComponent>(e.GetIndex());if(!rb||rb->Type!=BodyType::Dynamic||rb->InverseMass<=0)return;WakeRigidBody(*rb);rb->LinearVelocity+=i*rb->InverseMass;}
void PhysicsWorld::WakeUp(Scene&scene,Entity e){if(!scene.IsEntityAlive(e))return;auto*rb=scene.TryGetComponent<RigidBodyComponent>(e.GetIndex());if(rb&&rb->Type==BodyType::Dynamic)WakeRigidBody(*rb);}
void PhysicsWorld::Step(Scene&scene,float dt){if(dt<=0)return;m_SolverDebugStatistics.Reset();ApplyForces(scene,dt);IntegrateVelocities(scene,dt);DetectCollisions(scene);SolveCollisions(scene,dt);IntegratePositions(scene,dt);UpdateSleeping(scene,dt);ClearForces(scene);}
} // namespace ph
} // namespace Raven
