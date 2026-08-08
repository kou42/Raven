#include <algorithm>
#include <cmath>

#include "Raven/Physics/RigidBodyDynamics.h"
#include "Raven/Physics/Solver/ContactSolver.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{
namespace ph
{
namespace
{
float GetInverseMass(const RigidBodyComponent* body){if(!body||body->Type!=BodyType::Dynamic)return 0.0f;return std::max(body->InverseMass,0.0f);}
void WakeIfSleeping(RigidBodyComponent* body){if(body&&body->Type==BodyType::Dynamic&&body->IsSleeping){body->IsSleeping=false;body->SleepTimer=0.0f;}}
struct SolverBodies{TransformComponent* TransformA=nullptr;TransformComponent* TransformB=nullptr;RigidBodyComponent* BodyA=nullptr;RigidBodyComponent* BodyB=nullptr;ColliderComponent* ColliderA=nullptr;ColliderComponent* ColliderB=nullptr;float InverseMassA=0;float InverseMassB=0;float InverseMassSum=0;math::Mat3 InverseInertiaA{};math::Mat3 InverseInertiaB{};};
bool GetSolverBodies(Scene&scene,const ContactManifold&m,SolverBodies&out){if(!scene.IsEntityAlive(m.A)||!scene.IsEntityAlive(m.B))return false;out.TransformA=scene.TryGetComponent<TransformComponent>(m.A.GetIndex());out.TransformB=scene.TryGetComponent<TransformComponent>(m.B.GetIndex());if(!out.TransformA||!out.TransformB)return false;out.BodyA=scene.TryGetComponent<RigidBodyComponent>(m.A.GetIndex());out.BodyB=scene.TryGetComponent<RigidBodyComponent>(m.B.GetIndex());out.ColliderA=scene.TryGetComponent<ColliderComponent>(m.A.GetIndex());out.ColliderB=scene.TryGetComponent<ColliderComponent>(m.B.GetIndex());out.InverseMassA=GetInverseMass(out.BodyA);out.InverseMassB=GetInverseMass(out.BodyB);out.InverseMassSum=out.InverseMassA+out.InverseMassB;out.InverseInertiaA=ComputeWorldInverseInertia(out.TransformA,out.BodyA,out.ColliderA);out.InverseInertiaB=ComputeWorldInverseInertia(out.TransformB,out.BodyB,out.ColliderB);return out.InverseMassSum>0;}
bool GetNormal(const ContactManifold&m,math::Vec3&n){const float l=m.Normal.LengthSq();if(l<=1e-12f)return false;n=m.Normal/std::sqrt(l);return true;}

math::Vec3 RotateByOrientation(const RigidBodyComponent* body,const TransformComponent* transform,const math::Vec3& local)
{
    if(body&&body->OrientationInitialized)return body->Orientation.Rotate(local);
    // Orientation未初期化Body（Static等）でもTransformのEuler姿勢を反映するため、
    // Physics側と同じEuler順序から一時Quaternionを作ります。
    const math::Quat q=PhysicsOrientationFromEuler(transform->Rotation);
    return q.Rotate(local);
}
math::Vec3 InverseRotateByOrientation(const RigidBodyComponent* body,const TransformComponent* transform,const math::Vec3& world)
{
    math::Quat q=(body&&body->OrientationInitialized)?body->Orientation:PhysicsOrientationFromEuler(transform->Rotation);
    q=q.Normalized();return q.Conjugate().Rotate(world);
}
void InitializePositionAnchors(SolverBodies&b,ContactManifold&m,const math::Vec3&n)
{
    for(std::size_t i=0;i<m.PointCount;++i){ContactPoint&cp=m.Points[i];if(cp.PositionAnchorsInitialized)continue;cp.LocalAnchorA=InverseRotateByOrientation(b.BodyA,b.TransformA,cp.Position-b.TransformA->Position);cp.LocalAnchorB=InverseRotateByOrientation(b.BodyB,b.TransformB,cp.Position-b.TransformB->Position);cp.InitialSeparation=-std::max(cp.Penetration,0.0f);cp.PositionAnchorsInitialized=true;static_cast<void>(n);}
}
math::Vec3 WorldAnchorA(const SolverBodies&b,const ContactPoint&cp){return b.TransformA->Position+RotateByOrientation(b.BodyA,b.TransformA,cp.LocalAnchorA);}
math::Vec3 WorldAnchorB(const SolverBodies&b,const ContactPoint&cp){return b.TransformB->Position+RotateByOrientation(b.BodyB,b.TransformB,cp.LocalAnchorB);}

void ApplyImpulseAtPoint(SolverBodies&b,const math::Vec3&p,const math::Vec3&i){if(b.BodyA&&b.InverseMassA>0){WakeIfSleeping(b.BodyA);b.BodyA->LinearVelocity-=i*b.InverseMassA;b.BodyA->AngularVelocity-=b.InverseInertiaA*math::Vec3::Cross(p-b.TransformA->Position,i);}if(b.BodyB&&b.InverseMassB>0){WakeIfSleeping(b.BodyB);b.BodyB->LinearVelocity+=i*b.InverseMassB;b.BodyB->AngularVelocity+=b.InverseInertiaB*math::Vec3::Cross(p-b.TransformB->Position,i);}}
float ComputeEffectiveMassDenominator(const SolverBodies&b,const math::Vec3&p,const math::Vec3&d){const math::Vec3 ra=p-b.TransformA->Position,rb=p-b.TransformB->Position;const math::Vec3 aa=math::Vec3::Cross(b.InverseInertiaA*math::Vec3::Cross(ra,d),ra);const math::Vec3 ab=math::Vec3::Cross(b.InverseInertiaB*math::Vec3::Cross(rb,d),rb);return b.InverseMassSum+math::Vec3::Dot(d,aa+ab);}
void ApplyOrientationCorrection(TransformComponent*t,RigidBodyComponent*b,const math::Vec3&delta){if(!t||!b||b->Type!=BodyType::Dynamic||delta.LengthSq()<=1e-16f)return;EnsurePhysicsOrientation(*t,*b);const math::Quat dq{delta.x,delta.y,delta.z,0};b->Orientation=(b->Orientation+(dq*b->Orientation)*.5f).Normalized();t->Rotation=PhysicsEulerFromOrientation(b->Orientation);}

void SolvePositionConstraint(Scene&scene,ContactManifold&m,const ContactSolverSettings&s)
{
    if(m.IsTrigger||m.PointCount==0)return;SolverBodies b{};math::Vec3 n{};if(!GetSolverBodies(scene,m,b)||!GetNormal(m,n))return;InitializePositionAnchors(b,m,n);const float percent=std::clamp(s.PositionCorrectionPercent,0.0f,1.0f),slop=std::max(s.PenetrationSlop,0.0f);
    for(std::size_t i=0;i<m.PointCount;++i)
    {
        ContactPoint&cp=m.Points[i];const math::Vec3 anchorA=WorldAnchorA(b,cp),anchorB=WorldAnchorB(b,cp);
        // 初期separation(-penetration)に、Solver開始後の2アンカー間の相対移動を加えます。
        // SAT+Clippingを毎iteration再実行せずとも、現在Poseに追従したpenetrationを得られます。
        const float separation=cp.InitialSeparation+math::Vec3::Dot(anchorB-anchorA,n);
        const float penetration=std::max(-separation,0.0f);cp.Penetration=penetration;
        const float error=std::max(penetration-slop,0.0f);if(error<=0)continue;
        const math::Vec3 contactPoint=(anchorA+anchorB)*.5f;const math::Vec3 ra=contactPoint-b.TransformA->Position,rb=contactPoint-b.TransformB->Position;
        // Poseがiteration中に変わるのでworld inertiaも更新します。
        b.InverseInertiaA=ComputeWorldInverseInertia(b.TransformA,b.BodyA,b.ColliderA);b.InverseInertiaB=ComputeWorldInverseInertia(b.TransformB,b.BodyB,b.ColliderB);
        const float denominator=ComputeEffectiveMassDenominator(b,contactPoint,n);if(denominator<=1e-12f)continue;const float lambda=error*percent/denominator;
        if(b.BodyA&&b.InverseMassA>0){b.TransformA->Position-=n*(lambda*b.InverseMassA);ApplyOrientationCorrection(b.TransformA,b.BodyA,-(b.InverseInertiaA*math::Vec3::Cross(ra,n))*lambda);}
        if(b.BodyB&&b.InverseMassB>0){b.TransformB->Position+=n*(lambda*b.InverseMassB);ApplyOrientationCorrection(b.TransformB,b.BodyB,(b.InverseInertiaB*math::Vec3::Cross(rb,n))*lambda);}
    }
}

void WarmStartConstraint(Scene&scene,ContactManifold&m){if(m.IsTrigger||m.PointCount==0)return;SolverBodies b{};math::Vec3 n{};if(!GetSolverBodies(scene,m,b)||!GetNormal(m,n))return;for(std::size_t i=0;i<m.PointCount;++i){ContactPoint&p=m.Points[i];math::Vec3 impulse=n*std::max(p.AccumulatedNormalImpulse,0.0f);if(p.CachedTangent.LengthSq()>1e-12f)impulse+=p.CachedTangent.Normalized()*p.AccumulatedTangentImpulse;ApplyImpulseAtPoint(b,p.Position,impulse);}}
void SolvePointVelocityConstraint(SolverBodies&b,ContactManifold&m,ContactPoint&c,const math::Vec3&n,const ContactSolverSettings&s){math::Vec3 rv=GetVelocityAtPoint(b.BodyB,b.TransformB,c.Position)-GetVelocityAtPoint(b.BodyA,b.TransformA,c.Position);const float vn=math::Vec3::Dot(rv,n);float restitution=0;if(vn<-std::max(s.RestitutionVelocityThreshold,0.0f))restitution=std::clamp(m.Restitution,0.0f,1.0f);const float nd=ComputeEffectiveMassDenominator(b,c.Position,n);if(nd<=1e-12f)return;const float dj=-(1.0f+restitution)*vn/nd;const float old=c.AccumulatedNormalImpulse;c.AccumulatedNormalImpulse=std::max(old+dj,0.0f);ApplyImpulseAtPoint(b,c.Position,n*(c.AccumulatedNormalImpulse-old));rv=GetVelocityAtPoint(b.BodyB,b.TransformB,c.Position)-GetVelocityAtPoint(b.BodyA,b.TransformA,c.Position);math::Vec3 t=rv-n*math::Vec3::Dot(rv,n);if(t.LengthSq()<=1e-12f)return;t=t.Normalized();c.CachedTangent=t;const float td=ComputeEffectiveMassDenominator(b,c.Position,t);if(td<=1e-12f)return;const float dt=-math::Vec3::Dot(rv,t)/td;const float oldT=c.AccumulatedTangentImpulse,candidate=oldT+dt,staticLimit=c.AccumulatedNormalImpulse*std::max(m.StaticFriction,0.0f);float newT;if(std::abs(candidate)<=staticLimit)newT=candidate;else{const float dynamicLimit=c.AccumulatedNormalImpulse*std::max(m.DynamicFriction,0.0f);newT=std::clamp(candidate,-dynamicLimit,dynamicLimit);}c.AccumulatedTangentImpulse=newT;ApplyImpulseAtPoint(b,c.Position,t*(newT-oldT));}
void SolveVelocityConstraint(Scene&scene,ContactManifold&m,const ContactSolverSettings&s){if(m.IsTrigger||m.PointCount==0)return;SolverBodies b{};math::Vec3 n{};if(!GetSolverBodies(scene,m,b)||!GetNormal(m,n))return;for(std::size_t i=0;i<m.PointCount;++i)SolvePointVelocityConstraint(b,m,m.Points[i],n,s);}
} // namespace

void SolveContactManifolds(Scene&scene,std::vector<ContactManifold>&manifolds,float dt,const ContactSolverSettings&settings)
{
    if(dt<=0)return;if(settings.EnableWarmStart)for(ContactManifold&m:manifolds)WarmStartConstraint(scene,m);const uint32_t vi=std::max(settings.VelocityIterations,1u);for(uint32_t it=0;it<vi;++it)for(ContactManifold&m:manifolds)SolveVelocityConstraint(scene,m,settings);
    const uint32_t pi=std::max(settings.PositionIterations,1u);for(uint32_t it=0;it<pi;++it)for(ContactManifold&m:manifolds)SolvePositionConstraint(scene,m,settings);
}
void SolveContactManifold(Scene&scene,ContactManifold&manifold,float dt){std::vector<ContactManifold> one{manifold};SolveContactManifolds(scene,one,dt);manifold=one.front();}
} // namespace ph
} // namespace Raven
