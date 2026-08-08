#include <cassert>
#include <cmath>
#include <vector>

#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/DynamicAABBTreeValidation.h"
#include "Raven/Physics/Collision/OBB.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/RigidBodyDynamics.h"
#include "Raven/Physics/Solver/ContactSolver.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph::tests
{
namespace
{
bool NearlyEqual(float a,float b,float e=1e-5f){return std::abs(a-b)<=e;}
bool IsFinite(const math::Vec3&v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
Entity CreateBox(Scene&scene,const math::Vec3&position,BodyType type){Entity e=scene.CreateEntity("PhysicsSelfTestBox");auto&t=e.GetComponent<TransformComponent>();t.Position=position;auto&c=e.AddComponent<ColliderComponent>();c.Type=ColliderType::Box;c.HalfExtents={.5f,.5f,.5f};c.Restitution=0;c.StaticFriction=.7f;c.DynamicFriction=.5f;auto&b=e.AddComponent<RigidBodyComponent>();b.SetBodyType(type);b.LinearDamping=.01f;b.AngularDamping=.01f;b.AllowSleep=false;return e;}
struct StackResult{float MaxSpeed=0,TopHeight=0,MaximumPenetration=0;bool AllFinite=true;std::size_t PersistentImpulseFrames=0;};
StackResult RunBoxStackScenario(bool warm,uint32_t iterations){Scene scene;PhysicsWorld world;ContactSolverSettings s{};s.EnableWarmStart=warm;s.VelocityIterations=iterations;world.SetSolverSettings(s);Entity floor=CreateBox(scene,{0,-.5f,0},BodyType::Static);floor.GetComponent<ColliderComponent>().HalfExtents={5,.5f,5};std::vector<Entity> boxes;for(int i=0;i<8;++i)boxes.push_back(CreateBox(scene,{0,.5f+i*1.002f,0},BodyType::Dynamic));StackResult r{};for(int step=0;step<600;++step){world.Step(scene,1.0f/60);for(Entity e:boxes){const auto&t=e.GetComponent<TransformComponent>();const auto&b=e.GetComponent<RigidBodyComponent>();r.AllFinite=r.AllFinite&&IsFinite(t.Position)&&IsFinite(b.LinearVelocity)&&IsFinite(b.AngularVelocity);r.MaxSpeed=std::max(r.MaxSpeed,std::sqrt(b.LinearVelocity.LengthSq()));}bool found=false;for(const auto&m:world.GetContactManifolds())for(std::size_t i=0;i<m.PointCount;++i){r.MaximumPenetration=std::max(r.MaximumPenetration,m.Points[i].Penetration);if(m.Points[i].AccumulatedNormalImpulse>1e-5f)found=true;}if(found)++r.PersistentImpulseFrames;}r.TopHeight=boxes.back().GetComponent<TransformComponent>().Position.y;return r;}
}

void RunDynamicAABBTreeSelfTests(){DynamicAABBTree tree;const uint32_t a=tree.CreateProxy(AABB{{-1,-1,-1},{1,1,1}},Entity{}),b=tree.CreateProxy(AABB{{3,-1,-1},{5,1,1}},Entity{}),c=tree.CreateProxy(AABB{{7,-1,-1},{9,1,1}},Entity{});assert(ValidateDynamicAABBTree(tree).IsValid());assert(!tree.MoveProxy(a,AABB{{-.95f,-1,-1},{1.05f,1,1}},{.05f,0,0}));assert(tree.MoveProxy(b,AABB{{20,-1,-1},{22,1,1}},{17,0,0}));uint32_t count=0;tree.Query(AABB{{-2,-2,-2},{2,2,2}},[&](Entity,uint32_t p){if(p==a)++count;return true;});assert(count==1);tree.DestroyProxy(c);tree.DestroyProxy(b);tree.DestroyProxy(a);assert(ValidateDynamicAABBTree(tree).IsValid());}
void RunOBBFoundationSelfTests(){ColliderComponent c{};c.Type=ColliderType::Box;c.HalfExtents={1,.5f,.25f};TransformComponent t{};t.Rotation={0,0,.785398163f};OBB o{};assert(ComputeBoxOBB(t,c,o));assert(NearlyEqual(o.Axis[0].Length(),1,1e-4f));}
void RunOBBRayCastSelfTests(){TransformComponent t{};t.Rotation={0,0,.785398163f};ColliderComponent c{};c.Type=ColliderType::Box;c.HalfExtents={1,.25f,.5f};OBB o{};assert(ComputeBoxOBB(t,c,o));float f;math::Vec3 n;assert(o.RayCast({-2,-2,0},{1,1,0},10,f,&n));AABB b{};assert(ComputeColliderAABB(t,c,b));const math::Vec3 p{b.Max.x-.02f,-2,0};assert(b.RayCast(p,{0,1,0},10,f,&n));assert(!o.RayCast(p,{0,1,0},10,f,&n));}
void RunSphereBoxSelfTests(){ColliderComponent s{};s.Type=ColliderType::Sphere;s.Radius=1;ColliderComponent b{};b.Type=ColliderType::Box;b.HalfExtents={1,1,1};TransformComponent bt{},st{};ContactManifold m{};st.Position={1.5f,0,0};assert(GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));st.Position={4,0,0};assert(!GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));}
void RunBoxBoxSelfTests(){ColliderComponent a{};a.Type=ColliderType::Box;a.HalfExtents={1,1,1};ColliderComponent b=a;TransformComponent at{},bt{};ContactManifold m{};bt.Position={1.5f,0,0};assert(GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));bt.Position={3,0,0};assert(!GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));}
void RunAngularDynamicsSelfTests(){Scene scene;PhysicsWorld world;world.SetGravity({0,0,0});Entity e=CreateBox(scene,{0,0,0},BodyType::Dynamic);auto&b=e.GetComponent<RigidBodyComponent>();world.AddTorque(scene,e,{0,0,1});world.Step(scene,1.0f/60);assert(b.AngularVelocity.z>0);}
void RunPointImpulseSelfTests(){Scene scene;PhysicsWorld world;Entity e=CreateBox(scene,{0,0,0},BodyType::Dynamic);auto&b=e.GetComponent<RigidBodyComponent>();world.AddImpulseAtPoint(scene,e,{1,0,0},{0,.5f,0});world.AddImpulseAtPoint(scene,e,{-1,0,0},{0,-.5f,0});assert(b.LinearVelocity.LengthSq()<=1e-12f);assert(std::abs(b.AngularVelocity.z)>1e-5f);}

void RunAngularPositionSolverSelfTests()
{
    ContactSolverSettings s{};s.EnableWarmStart=false;s.VelocityIterations=1;s.PositionIterations=4;s.PositionCorrectionPercent=.8f;s.PenetrationSlop=0;
    {
        Scene scene;Entity a=CreateBox(scene,{0,0,0},BodyType::Dynamic),b=CreateBox(scene,{0,0,0},BodyType::Static);auto&body=a.GetComponent<RigidBodyComponent>();auto&t=a.GetComponent<TransformComponent>();ContactManifold m{};m.A=a;m.B=b;m.Normal={1,0,0};m.PointCount=1;m.Points[0].Position={0,.5f,0};m.Points[0].Penetration=.2f;std::vector<ContactManifold> ms{m};SolveContactManifolds(scene,ms,1.0f/60,s);assert(t.Position.x<0);assert(body.OrientationInitialized);assert(std::abs(t.Rotation.z)>1e-5f);assert(body.LinearVelocity.LengthSq()<=1e-12f);assert(body.AngularVelocity.LengthSq()<=1e-12f);assert(ms[0].Points[0].Penetration<.2f);
    }
    {
        Scene scene;Entity a=CreateBox(scene,{0,0,0},BodyType::Dynamic),b=CreateBox(scene,{0,0,0},BodyType::Static);auto&body=a.GetComponent<RigidBodyComponent>();auto&t=a.GetComponent<TransformComponent>();ContactManifold m{};m.A=a;m.B=b;m.Normal={1,0,0};m.PointCount=1;m.Points[0].Position={0,0,0};m.Points[0].Penetration=.2f;std::vector<ContactManifold> ms{m};SolveContactManifolds(scene,ms,1.0f/60,s);assert(t.Position.x<0);assert(std::abs(t.Rotation.z)<=1e-6f);assert(body.AngularVelocity.LengthSq()<=1e-12f);
    }
}

void RunContactAnchorPositionSelfTests()
{
    // 1 iterationと複数iterationを比較します。Anchor方式が現在Poseを再評価していれば、
    // 2回目以降も同じ古いpenetrationを機械的に引くのではなく、残差だけを解きます。
    auto solve=[](uint32_t iterations)
    {
        Scene scene;Entity a=CreateBox(scene,{0,0,0},BodyType::Dynamic),b=CreateBox(scene,{0,0,0},BodyType::Static);
        ContactManifold m{};m.A=a;m.B=b;m.Normal={1,0,0};m.PointCount=1;m.Points[0].Position={0,.5f,0};m.Points[0].Penetration=.25f;
        ContactSolverSettings s{};s.EnableWarmStart=false;s.VelocityIterations=1;s.PositionIterations=iterations;s.PositionCorrectionPercent=.5f;s.PenetrationSlop=0;
        std::vector<ContactManifold> ms{m};SolveContactManifolds(scene,ms,1.0f/60,s);
        assert(ms[0].Points[0].PositionAnchorsInitialized);assert(IsFinite(a.GetComponent<TransformComponent>().Position));assert(IsFinite(a.GetComponent<RigidBodyComponent>().AngularVelocity));
        return ms[0].Points[0].Penetration;
    };
    const float afterOne=solve(1),afterFour=solve(4);
    assert(afterOne<.25f);assert(afterFour<afterOne);assert(afterFour>=0.0f);
}

void RunContactPersistenceWarmStartSelfTests(){const StackResult cold=RunBoxStackScenario(false,1),warm=RunBoxStackScenario(true,1);assert(cold.AllFinite&&warm.AllFinite);assert(warm.PersistentImpulseFrames>0);assert(warm.TopHeight>6.5f&&warm.TopHeight<8.5f);assert(warm.MaximumPenetration<=cold.MaximumPenetration+.05f);}
void RunBoxStackStressTest(){const StackResult r=RunBoxStackScenario(true,8);assert(r.AllFinite);assert(r.PersistentImpulseFrames>100);assert(r.TopHeight>6.5f&&r.TopHeight<8.5f);assert(r.MaximumPenetration<.25f);}
void RunPhysicsCollisionSelfTests(){RunDynamicAABBTreeSelfTests();RunOBBFoundationSelfTests();RunOBBRayCastSelfTests();RunSphereBoxSelfTests();RunBoxBoxSelfTests();RunAngularDynamicsSelfTests();RunPointImpulseSelfTests();RunAngularPositionSolverSelfTests();RunContactAnchorPositionSelfTests();RunContactPersistenceWarmStartSelfTests();RunBoxStackStressTest();}

} // namespace Raven::ph::tests
