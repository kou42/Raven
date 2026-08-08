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

void ApplyImpulseAtPoint(SolverBodies&b,const math::Vec3&p,const math::Vec3&i){if(b.BodyA&&b.InverseMassA>0){WakeIfSleeping(b.BodyA);b.BodyA->LinearVelocity-=i*b.InverseMassA;b.BodyA->AngularVelocity-=b.InverseInertiaA*math::Vec3::Cross(p-b.TransformA->Position,i);}if(b.BodyB&&b.InverseMassB>0){WakeIfSleeping(b.BodyB);b.BodyB->LinearVelocity+=i*b.InverseMassB;b.BodyB->AngularVelocity+=b.InverseInertiaB*math::Vec3::Cross(p-b.TransformB->Position,i);}}
float ComputeEffectiveMassDenominator(const SolverBodies&b,const math::Vec3&p,const math::Vec3&d){const math::Vec3 ra=p-b.TransformA->Position,rb=p-b.TransformB->Position;const math::Vec3 aa=math::Vec3::Cross(b.InverseInertiaA*math::Vec3::Cross(ra,d),ra);const math::Vec3 ab=math::Vec3::Cross(b.InverseInertiaB*math::Vec3::Cross(rb,d),rb);return b.InverseMassSum+math::Vec3::Dot(d,aa+ab);}

void ApplyOrientationCorrection(TransformComponent* transform,RigidBodyComponent* body,const math::Vec3& angularDelta)
{
    if(!transform||!body||body->Type!=BodyType::Dynamic||angularDelta.LengthSq()<=1e-16f)return;
    EnsurePhysicsOrientation(*transform,*body);

    // Position Solver専用の微小回転です。AngularVelocityは変更しません。
    // q' = normalize(q + 1/2 * deltaTheta * q)
    // とすることで、penetration correctionが運動エネルギーを生成しないようにします。
    const math::Quat dq{angularDelta.x,angularDelta.y,angularDelta.z,0.0f};
    body->Orientation=(body->Orientation+(dq*body->Orientation)*0.5f).Normalized();
    transform->Rotation=PhysicsEulerFromOrientation(body->Orientation);
}

void SolvePositionConstraint(Scene&scene,ContactManifold&m,const ContactSolverSettings&s)
{
    if(m.IsTrigger||m.PointCount==0)return;SolverBodies b{};math::Vec3 n{};if(!GetSolverBodies(scene,m,b)||!GetNormal(m,n))return;
    const float percent=std::clamp(s.PositionCorrectionPercent,0.0f,1.0f);const float slop=std::max(s.PenetrationSlop,0.0f);

    // 各Contact Pointを独立したposition constraintとして解きます。
    // effective massにI^-1を含めるため、OBBの角/辺接触では重心移動だけでなく
    // penetrationを減らす方向へ姿勢も少し回転します。
    for(std::size_t i=0;i<m.PointCount;++i)
    {
        const ContactPoint&cp=m.Points[i];const float error=std::max(cp.Penetration-slop,0.0f);if(error<=0.0f)continue;
        const math::Vec3 ra=cp.Position-b.TransformA->Position,rb=cp.Position-b.TransformB->Position;
        const float denominator=ComputeEffectiveMassDenominator(b,cp.Position,n);if(denominator<=1e-12f)continue;
        const float lambda=error*percent/denominator;

        if(b.BodyA&&b.InverseMassA>0){b.TransformA->Position-=n*(lambda*b.InverseMassA);ApplyOrientationCorrection(b.TransformA,b.BodyA,-(b.InverseInertiaA*math::Vec3::Cross(ra,n))*lambda);}
        if(b.BodyB&&b.InverseMassB>0){b.TransformB->Position+=n*(lambda*b.InverseMassB);ApplyOrientationCorrection(b.TransformB,b.BodyB,(b.InverseInertiaB*math::Vec3::Cross(rb,n))*lambda);}

        // 同一Manifoldを複数回反復するため、今回解消した法線方向量を近似的に
        // penetrationから減算します。毎iteration Narrow Phaseを再生成するより軽量です。
        m.Points[i].Penetration=std::max(cp.Penetration-error*percent,0.0f);
    }
}

void WarmStartConstraint(Scene&scene,ContactManifold&m){if(m.IsTrigger||m.PointCount==0)return;SolverBodies b{};math::Vec3 n{};if(!GetSolverBodies(scene,m,b)||!GetNormal(m,n))return;for(std::size_t i=0;i<m.PointCount;++i){ContactPoint&p=m.Points[i];math::Vec3 impulse=n*std::max(p.AccumulatedNormalImpulse,0.0f);if(p.CachedTangent.LengthSq()>1e-12f)impulse+=p.CachedTangent.Normalized()*p.AccumulatedTangentImpulse;ApplyImpulseAtPoint(b,p.Position,impulse);}}
void SolvePointVelocityConstraint(SolverBodies&b,ContactManifold&m,ContactPoint&c,const math::Vec3&n,const ContactSolverSettings&s){math::Vec3 rv=GetVelocityAtPoint(b.BodyB,b.TransformB,c.Position)-GetVelocityAtPoint(b.BodyA,b.TransformA,c.Position);const float vn=math::Vec3::Dot(rv,n);float restitution=0;if(vn<-std::max(s.RestitutionVelocityThreshold,0.0f))restitution=std::clamp(m.Restitution,0.0f,1.0f);const float nd=ComputeEffectiveMassDenominator(b,c.Position,n);if(nd<=1e-12f)return;const float dj=-(1.0f+restitution)*vn/nd;const float old=c.AccumulatedNormalImpulse;c.AccumulatedNormalImpulse=std::max(old+dj,0.0f);ApplyImpulseAtPoint(b,c.Position,n*(c.AccumulatedNormalImpulse-old));rv=GetVelocityAtPoint(b.BodyB,b.TransformB,c.Position)-GetVelocityAtPoint(b.BodyA,b.TransformA,c.Position);math::Vec3 t=rv-n*math::Vec3::Dot(rv,n);if(t.LengthSq()<=1e-12f)return;t=t.Normalized();c.CachedTangent=t;const float td=ComputeEffectiveMassDenominator(b,c.Position,t);if(td<=1e-12f)return;const float dt=-math::Vec3::Dot(rv,t)/td;const float oldT=c.AccumulatedTangentImpulse,candidate=oldT+dt,staticLimit=c.AccumulatedNormalImpulse*std::max(m.StaticFriction,0.0f);float newT;if(std::abs(candidate)<=staticLimit)newT=candidate;else{const float dynamicLimit=c.AccumulatedNormalImpulse*std::max(m.DynamicFriction,0.0f);newT=std::clamp(candidate,-dynamicLimit,dynamicLimit);}c.AccumulatedTangentImpulse=newT;ApplyImpulseAtPoint(b,c.Position,t*(newT-oldT));}
void SolveVelocityConstraint(Scene&scene,ContactManifold&m,const ContactSolverSettings&s){if(m.IsTrigger||m.PointCount==0)return;SolverBodies b{};math::Vec3 n{};if(!GetSolverBodies(scene,m,b)||!GetNormal(m,n))return;for(std::size_t i=0;i<m.PointCount;++i)SolvePointVelocityConstraint(b,m,m.Points[i],n,s);}
} // namespace

void SolveContactManifolds(Scene&scene,std::vector<ContactManifold>&manifolds,float dt,const ContactSolverSettings&settings)
{
    if(dt<=0.0f)return;

    // Phase 1: velocity constraints。Warm Startもvelocity側だけに属します。
    if(settings.EnableWarmStart)for(ContactManifold&m:manifolds)WarmStartConstraint(scene,m);
    const uint32_t velocityIterations=std::max(settings.VelocityIterations,1u);
    for(uint32_t iteration=0;iteration<velocityIterations;++iteration)for(ContactManifold&m:manifolds)SolveVelocityConstraint(scene,m,settings);

    // Phase 2: position constraints。LinearVelocity/AngularVelocityには触れず、
    // Transform + Orientationだけを補正します。これが簡易split-impulse相当の役割です。
    const uint32_t positionIterations=std::max(settings.PositionIterations,1u);
    for(uint32_t iteration=0;iteration<positionIterations;++iteration)for(ContactManifold&m:manifolds)SolvePositionConstraint(scene,m,settings);
}

void SolveContactManifold(Scene&scene,ContactManifold&manifold,float dt){std::vector<ContactManifold> single{manifold};SolveContactManifolds(scene,single,dt);manifold=single.front();}
} // namespace ph
} // namespace Raven
