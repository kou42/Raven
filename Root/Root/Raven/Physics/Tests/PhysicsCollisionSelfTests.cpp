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

Entity CreateBox(Scene& scene,const math::Vec3& position,BodyType type)
{
    Entity entity=scene.CreateEntity("PhysicsSelfTestBox");
    auto& transform=entity.GetComponent<TransformComponent>();transform.Position=position;
    auto& collider=entity.AddComponent<ColliderComponent>();collider.Type=ColliderType::Box;collider.HalfExtents={0.5f,0.5f,0.5f};collider.Restitution=0.0f;collider.StaticFriction=0.7f;collider.DynamicFriction=0.5f;
    auto& body=entity.AddComponent<RigidBodyComponent>();body.SetBodyType(type);body.LinearDamping=0.01f;body.AngularDamping=0.01f;body.AllowSleep=false;return entity;
}
struct StackResult{float MaxSpeed=0.0f;float TopHeight=0.0f;float MaximumPenetration=0.0f;bool AllFinite=true;std::size_t PersistentImpulseFrames=0;};
StackResult RunBoxStackScenario(bool warmStart,uint32_t iterations){Scene scene;PhysicsWorld world;ContactSolverSettings settings{};settings.EnableWarmStart=warmStart;settings.VelocityIterations=iterations;world.SetSolverSettings(settings);Entity floor=CreateBox(scene,{0,-.5f,0},BodyType::Static);floor.GetComponent<ColliderComponent>().HalfExtents={5,.5f,5};constexpr int BoxCount=8;std::vector<Entity> boxes;for(int i=0;i<BoxCount;++i)boxes.push_back(CreateBox(scene,{0,.5f+i*1.002f,0},BodyType::Dynamic));StackResult r{};for(int step=0;step<600;++step){world.Step(scene,1.0f/60.0f);for(Entity box:boxes){const auto&t=box.GetComponent<TransformComponent>();const auto&rb=box.GetComponent<RigidBodyComponent>();r.AllFinite=r.AllFinite&&IsFinite(t.Position)&&IsFinite(rb.LinearVelocity)&&IsFinite(rb.AngularVelocity);r.MaxSpeed=std::max(r.MaxSpeed,std::sqrt(rb.LinearVelocity.LengthSq()));}bool found=false;for(const auto&m:world.GetContactManifolds())for(std::size_t i=0;i<m.PointCount;++i){r.MaximumPenetration=std::max(r.MaximumPenetration,m.Points[i].Penetration);if(m.Points[i].AccumulatedNormalImpulse>1e-5f)found=true;}if(found)++r.PersistentImpulseFrames;}r.TopHeight=boxes.back().GetComponent<TransformComponent>().Position.y;return r;}
}

void RunDynamicAABBTreeSelfTests(){DynamicAABBTree tree;const uint32_t a=tree.CreateProxy(AABB{{-1,-1,-1},{1,1,1}},Entity{});const uint32_t b=tree.CreateProxy(AABB{{3,-1,-1},{5,1,1}},Entity{});const uint32_t c=tree.CreateProxy(AABB{{7,-1,-1},{9,1,1}},Entity{});assert(ValidateDynamicAABBTree(tree).IsValid());assert(!tree.MoveProxy(a,AABB{{-.95f,-1,-1},{1.05f,1,1}},{.05f,0,0}));assert(tree.MoveProxy(b,AABB{{20,-1,-1},{22,1,1}},{17,0,0}));uint32_t count=0;tree.Query(AABB{{-2,-2,-2},{2,2,2}},[&](Entity,uint32_t p){if(p==a)++count;return true;});assert(count==1);tree.DestroyProxy(c);tree.DestroyProxy(b);tree.DestroyProxy(a);assert(ValidateDynamicAABBTree(tree).IsValid());}
void RunOBBFoundationSelfTests(){constexpr float Pi=3.14159265358979323846f;ColliderComponent c{};c.Type=ColliderType::Box;c.HalfExtents={1,.5f,.25f};c.Offset={.5f,0,0};TransformComponent t{};t.Position={2,3,4};t.Rotation={0,0,Pi*.25f};OBB o{};assert(ComputeBoxOBB(t,c,o));assert(NearlyEqual(o.Axis[0].Length(),1,1e-4f));assert(NearlyEqual(math::Vec3::Dot(o.Axis[0],o.Axis[1]),0,1e-4f));AABB bounds{};assert(ComputeColliderAABB(t,c,bounds));}
void RunOBBRayCastSelfTests(){constexpr float Pi=3.14159265358979323846f;TransformComponent t{};t.Rotation={0,0,Pi*.25f};ColliderComponent c{};c.Type=ColliderType::Box;c.HalfExtents={1,.25f,.5f};OBB o{};assert(ComputeBoxOBB(t,c,o));float f=0;math::Vec3 n{};assert(o.RayCast({-2,-2,0},{1,1,0},10,f,&n));AABB bounds{};assert(ComputeColliderAABB(t,c,bounds));const math::Vec3 p{bounds.Max.x-.02f,-2,0};assert(bounds.RayCast(p,{0,1,0},10,f,&n));assert(!o.RayCast(p,{0,1,0},10,f,&n));assert(o.RayCast(o.Center,{1,0,0},10,f,&n));assert(NearlyEqual(f,0));}
void RunSphereBoxSelfTests(){ColliderComponent s{};s.Type=ColliderType::Sphere;s.Radius=1;ColliderComponent b{};b.Type=ColliderType::Box;b.HalfExtents={1,1,1};TransformComponent bt{},st{};ContactManifold m{};st.Position={1.5f,0,0};assert(GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));st.Position={4,0,0};assert(!GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));}
void RunBoxBoxSelfTests(){ColliderComponent a{};a.Type=ColliderType::Box;a.HalfExtents={1,1,1};ColliderComponent b=a;TransformComponent at{},bt{};ContactManifold m{};bt.Position={1.5f,0,0};assert(GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));assert(m.PointCount>=1&&m.PointCount<=4);bt.Position={3,0,0};assert(!GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));}
void RunAngularDynamicsSelfTests(){Scene scene;PhysicsWorld world;world.SetGravity({0,0,0});Entity box=CreateBox(scene,{0,0,0},BodyType::Dynamic);auto& body=box.GetComponent<RigidBodyComponent>();auto& transform=box.GetComponent<TransformComponent>();body.LinearDamping=body.AngularDamping=0;world.AddTorque(scene,box,{0,0,1});world.Step(scene,1.0f/60.0f);assert(body.AngularVelocity.z>0);assert(body.OrientationInitialized);assert(NearlyEqual(body.Orientation.Length(),1,1e-4f));assert(std::abs(transform.Rotation.z)>0);}
void RunPointImpulseSelfTests(){Scene scene;PhysicsWorld world;Entity box=CreateBox(scene,{0,0,0},BodyType::Dynamic);auto& body=box.GetComponent<RigidBodyComponent>();world.AddImpulseAtPoint(scene,box,{1,0,0},{0,.5f,0});world.AddImpulseAtPoint(scene,box,{-1,0,0},{0,-.5f,0});assert(body.LinearVelocity.LengthSq()<=1e-12f);assert(std::abs(body.AngularVelocity.z)>1e-5f);}

void RunAngularPositionSolverSelfTests()
{
    ContactSolverSettings settings{};settings.EnableWarmStart=false;settings.VelocityIterations=1;settings.PositionIterations=4;settings.PositionCorrectionPercent=.8f;settings.PenetrationSlop=0.0f;

    // 角接触: r x n != 0。Position Solverは重心移動だけでなく姿勢も補正します。
    // この補正はpenetration除去専用なので、Linear/AngularVelocityへエネルギーを加えません。
    {
        Scene scene;Entity dynamicBox=CreateBox(scene,{0,0,0},BodyType::Dynamic);Entity staticBox=CreateBox(scene,{0,0,0},BodyType::Static);
        auto& body=dynamicBox.GetComponent<RigidBodyComponent>();auto& transform=dynamicBox.GetComponent<TransformComponent>();body.LinearVelocity={};body.AngularVelocity={};
        ContactManifold m{};m.A=dynamicBox;m.B=staticBox;m.Normal={1,0,0};m.PointCount=1;m.Points[0].Position={0,.5f,0};m.Points[0].Penetration=.2f;
        std::vector<ContactManifold> manifolds{m};SolveContactManifolds(scene,manifolds,1.0f/60.0f,settings);
        assert(transform.Position.x<0.0f);assert(body.OrientationInitialized);assert(std::abs(transform.Rotation.z)>1e-5f);assert(body.LinearVelocity.LengthSq()<=1e-12f);assert(body.AngularVelocity.LengthSq()<=1e-12f);assert(manifolds[0].Points[0].Penetration<.2f);
    }

    // 中央接触: r x n == 0。並進補正だけが必要で、不要な回転を生成してはいけません。
    {
        Scene scene;Entity dynamicBox=CreateBox(scene,{0,0,0},BodyType::Dynamic);Entity staticBox=CreateBox(scene,{0,0,0},BodyType::Static);
        auto& body=dynamicBox.GetComponent<RigidBodyComponent>();auto& transform=dynamicBox.GetComponent<TransformComponent>();
        ContactManifold m{};m.A=dynamicBox;m.B=staticBox;m.Normal={1,0,0};m.PointCount=1;m.Points[0].Position={0,0,0};m.Points[0].Penetration=.2f;
        std::vector<ContactManifold> manifolds{m};SolveContactManifolds(scene,manifolds,1.0f/60.0f,settings);
        assert(transform.Position.x<0.0f);assert(body.OrientationInitialized);assert(std::abs(transform.Rotation.x)<=1e-6f);assert(std::abs(transform.Rotation.y)<=1e-6f);assert(std::abs(transform.Rotation.z)<=1e-6f);assert(body.AngularVelocity.LengthSq()<=1e-12f);
    }
}

void RunContactPersistenceWarmStartSelfTests(){const StackResult cold=RunBoxStackScenario(false,1);const StackResult warm=RunBoxStackScenario(true,1);assert(cold.AllFinite);assert(warm.AllFinite);assert(warm.PersistentImpulseFrames>0);assert(warm.TopHeight>6.5f&&warm.TopHeight<8.5f);assert(warm.MaximumPenetration<=cold.MaximumPenetration+.05f);}
void RunBoxStackStressTest(){const StackResult result=RunBoxStackScenario(true,8);assert(result.AllFinite);assert(result.PersistentImpulseFrames>100);assert(result.TopHeight>6.5f&&result.TopHeight<8.5f);assert(result.MaximumPenetration<.25f);}
void RunPhysicsCollisionSelfTests(){RunDynamicAABBTreeSelfTests();RunOBBFoundationSelfTests();RunOBBRayCastSelfTests();RunSphereBoxSelfTests();RunBoxBoxSelfTests();RunAngularDynamicsSelfTests();RunPointImpulseSelfTests();RunAngularPositionSolverSelfTests();RunContactPersistenceWarmStartSelfTests();RunBoxStackStressTest();}

} // namespace Raven::ph::tests
