#include <cassert>
#include <cmath>
#include <vector>

#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/DynamicAABBTreeValidation.h"
#include "Raven/Physics/Collision/OBB.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/RigidBodyDynamics.h"
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
    auto& transform=entity.GetComponent<TransformComponent>();
    transform.Position=position;
    auto& collider=entity.AddComponent<ColliderComponent>();
    collider.Type=ColliderType::Box;collider.HalfExtents={0.5f,0.5f,0.5f};
    collider.Restitution=0.0f;collider.StaticFriction=0.7f;collider.DynamicFriction=0.5f;
    auto& body=entity.AddComponent<RigidBodyComponent>();
    body.SetBodyType(type);body.LinearDamping=0.01f;body.AngularDamping=0.01f;
    body.AllowSleep=false;
    return entity;
}

struct StackResult
{
    float MaxSpeed=0.0f;
    float TopHeight=0.0f;
    float MaximumPenetration=0.0f;
    bool AllFinite=true;
    std::size_t PersistentImpulseFrames=0;
};

StackResult RunBoxStackScenario(bool warmStart,uint32_t iterations)
{
    Scene scene;
    PhysicsWorld world;
    ContactSolverSettings settings{};
    settings.EnableWarmStart=warmStart;
    settings.VelocityIterations=iterations;
    world.SetSolverSettings(settings);

    Entity floor=CreateBox(scene,{0.0f,-0.5f,0.0f},BodyType::Static);
    floor.GetComponent<ColliderComponent>().HalfExtents={5.0f,0.5f,5.0f};

    constexpr int BoxCount=8;
    std::vector<Entity> boxes;
    boxes.reserve(BoxCount);
    for(int i=0;i<BoxCount;++i)
        boxes.push_back(CreateBox(scene,{0.0f,0.5f+static_cast<float>(i)*1.002f,0.0f},BodyType::Dynamic));

    constexpr float Dt=1.0f/60.0f;
    constexpr int StepCount=600;
    StackResult result{};
    for(int step=0;step<StepCount;++step)
    {
        world.Step(scene,Dt);
        for(Entity box:boxes)
        {
            const auto& t=box.GetComponent<TransformComponent>();
            const auto& rb=box.GetComponent<RigidBodyComponent>();
            result.AllFinite=result.AllFinite&&IsFinite(t.Position)&&IsFinite(rb.LinearVelocity)&&IsFinite(rb.AngularVelocity);
            result.MaxSpeed=std::max(result.MaxSpeed,std::sqrt(rb.LinearVelocity.LengthSq()));
        }
        bool foundCachedImpulse=false;
        for(const ContactManifold& manifold:world.GetContactManifolds())
        {
            for(std::size_t i=0;i<manifold.PointCount;++i)
            {
                result.MaximumPenetration=std::max(result.MaximumPenetration,manifold.Points[i].Penetration);
                if(manifold.Points[i].AccumulatedNormalImpulse>1.0e-5f)foundCachedImpulse=true;
            }
        }
        if(foundCachedImpulse)++result.PersistentImpulseFrames;
    }
    result.TopHeight=boxes.back().GetComponent<TransformComponent>().Position.y;
    return result;
}
}

void RunDynamicAABBTreeSelfTests()
{
    DynamicAABBTree tree;const uint32_t a=tree.CreateProxy(AABB{{-1,-1,-1},{1,1,1}},Entity{});const uint32_t b=tree.CreateProxy(AABB{{3,-1,-1},{5,1,1}},Entity{});const uint32_t c=tree.CreateProxy(AABB{{7,-1,-1},{9,1,1}},Entity{});assert(ValidateDynamicAABBTree(tree).IsValid());assert(!tree.MoveProxy(a,AABB{{-.95f,-1,-1},{1.05f,1,1}},{.05f,0,0}));assert(ValidateDynamicAABBTree(tree).IsValid());assert(tree.MoveProxy(b,AABB{{20,-1,-1},{22,1,1}},{17,0,0}));assert(ValidateDynamicAABBTree(tree).IsValid());uint32_t count=0;tree.Query(AABB{{-2,-2,-2},{2,2,2}},[&](Entity,uint32_t p){if(p==a)++count;return true;});assert(count==1);tree.DestroyProxy(c);tree.DestroyProxy(b);tree.DestroyProxy(a);assert(ValidateDynamicAABBTree(tree).IsValid());
}

void RunOBBFoundationSelfTests()
{
    constexpr float Pi=3.14159265358979323846f;
    ColliderComponent boxCollider{};boxCollider.Type=ColliderType::Box;boxCollider.HalfExtents={1.0f,0.5f,0.25f};boxCollider.Offset={0.5f,0.0f,0.0f};
    TransformComponent transform{};transform.Position={2.0f,3.0f,4.0f};transform.Rotation={0.0f,0.0f,Pi*0.25f};
    OBB obb{};assert(ComputeBoxOBB(transform,boxCollider,obb));
    assert(NearlyEqual(obb.Axis[0].Length(),1.0f,1.0e-4f));assert(NearlyEqual(obb.Axis[1].Length(),1.0f,1.0e-4f));assert(NearlyEqual(obb.Axis[2].Length(),1.0f,1.0e-4f));
    assert(NearlyEqual(math::Vec3::Dot(obb.Axis[0],obb.Axis[1]),0.0f,1.0e-4f));assert(NearlyEqual(math::Vec3::Dot(obb.Axis[1],obb.Axis[2]),0.0f,1.0e-4f));assert(NearlyEqual(math::Vec3::Dot(obb.Axis[2],obb.Axis[0]),0.0f,1.0e-4f));
    const float rotatedOffset=0.5f/std::sqrt(2.0f);assert(NearlyEqual(obb.Center.x,2.0f+rotatedOffset,1.0e-4f));assert(NearlyEqual(obb.Center.y,3.0f+rotatedOffset,1.0e-4f));assert(NearlyEqual(obb.Center.z,4.0f,1.0e-4f));
    AABB bounds{};assert(ComputeColliderAABB(transform,boxCollider,bounds));const float expectedXY=(1.0f+0.5f)/std::sqrt(2.0f);assert(NearlyEqual(bounds.GetExtents().x,expectedXY,1.0e-4f));assert(NearlyEqual(bounds.GetExtents().y,expectedXY,1.0e-4f));assert(NearlyEqual(bounds.GetExtents().z,0.25f,1.0e-4f));
}

void RunSphereBoxSelfTests()
{
    constexpr float Pi=3.14159265358979323846f;
    ColliderComponent s{};s.Type=ColliderType::Sphere;s.Radius=1;ColliderComponent b{};b.Type=ColliderType::Box;b.HalfExtents={1,1,1};TransformComponent bt{},st{};ContactManifold m{};st.Position={1.5f,0,0};assert(GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));assert(m.PointCount==1);assert(NearlyEqual(m.Points[0].Penetration,.5f));assert(NearlyEqual(m.Normal.x,-1));st.Position={};assert(GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));assert(NearlyEqual(m.Points[0].Penetration,2));assert(NearlyEqual(m.Normal.x,1));st.Position={4,0,0};assert(!GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));
    b.HalfExtents={1.0f,0.25f,0.5f};bt.Rotation={0.0f,0.0f,Pi*0.25f};s.Radius=0.2f;st.Position={0.70f,0.70f,0.0f};assert(GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));assert(m.PointCount==1);assert(IsFinite(m.Normal));st.Position={1.5f,1.5f,0.0f};assert(!GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));
}

void RunBoxBoxSelfTests()
{
    constexpr float Pi=3.14159265358979323846f;
    ColliderComponent a{};a.Type=ColliderType::Box;a.HalfExtents={1,1,1};ColliderComponent b=a;TransformComponent at{},bt{};ContactManifold m{};bt.Position={1.5f,0,0};assert(GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));assert(m.PointCount>=1&&m.PointCount<=4);assert(NearlyEqual(m.Normal.x,1));assert(m.Points[0].Penetration>=0.0f);bt.Position={0,1.75f,0};assert(GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));assert(NearlyEqual(m.Normal.y,1));bt.Position={3,0,0};assert(!GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));
    at={};bt={};a.HalfExtents={1.0f,1.0f,1.0f};b.HalfExtents={1.0f,0.6f,1.0f};bt.Position={1.15f,0.0f,0.0f};bt.Rotation={0.0f,0.0f,Pi*0.25f};assert(GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));assert(m.PointCount>=1&&m.PointCount<=4);assert(IsFinite(m.Normal));assert(m.Normal.x>0.0f);for(std::size_t i=0;i<m.PointCount;++i){assert(IsFinite(m.Points[i].Position));assert(m.Points[i].Penetration>=0.0f);}bt.Position={2.1f,2.1f,0.0f};assert(!GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));
    a.HalfExtents={1.25f,0.15f,0.15f};b.HalfExtents={1.25f,0.15f,0.15f};at.Rotation={0.0f,0.0f,Pi*0.25f};bt.Rotation={0.0f,Pi*0.25f,-Pi*0.25f};bt.Position={0.0f,0.0f,0.20f};assert(GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));assert(m.PointCount>=1&&m.PointCount<=4);assert(IsFinite(m.Normal));
}

void RunAngularDynamicsSelfTests()
{
    // ------------------------------------------------------------------------
    // Torque -> AngularVelocity -> Quaternion Orientation
    // ------------------------------------------------------------------------
    Scene scene;
    PhysicsWorld world;
    world.SetGravity({0.0f,0.0f,0.0f});
    Entity box=CreateBox(scene,{0.0f,0.0f,0.0f},BodyType::Dynamic);
    auto& body=box.GetComponent<RigidBodyComponent>();
    auto& transform=box.GetComponent<TransformComponent>();
    body.AllowSleep=false;
    body.LinearDamping=0.0f;
    body.AngularDamping=0.0f;

    world.AddTorque(scene,box,{0.0f,0.0f,1.0f});
    world.Step(scene,1.0f/60.0f);
    assert(body.AngularVelocity.z>0.0f);
    assert(body.OrientationInitialized);
    assert(NearlyEqual(body.Orientation.Length(),1.0f,1.0e-4f));
    assert(std::abs(transform.Rotation.z)>0.0f);

    // ------------------------------------------------------------------------
    // 回転中はlinear velocityが0でもSleepへ入ってはいけません。
    // ------------------------------------------------------------------------
    body.AllowSleep=true;
    body.SleepTimeThreshold=0.05f;
    body.SleepThreshold=0.01f;
    body.AngularSleepThreshold=0.01f;
    body.LinearVelocity={0.0f,0.0f,0.0f};
    body.AngularVelocity={0.0f,0.0f,1.0f};
    body.IsSleeping=false;
    body.SleepTimer=0.0f;
    for(int i=0;i<10;++i) world.Step(scene,1.0f/60.0f);
    assert(!body.IsSleeping);

    // ------------------------------------------------------------------------
    // 偏心衝突: 重心から外れた接触点にImpulseが入るとAngularVelocityが発生する。
    // SphereをBox上端付近へ横から当て、Contact Solverの r x J を検証します。
    // ------------------------------------------------------------------------
    Scene collisionScene;
    PhysicsWorld collisionWorld;
    collisionWorld.SetGravity({0.0f,0.0f,0.0f});

    Entity target=CreateBox(collisionScene,{0.0f,0.0f,0.0f},BodyType::Dynamic);
    auto& targetBody=target.GetComponent<RigidBodyComponent>();
    auto& targetCollider=target.GetComponent<ColliderComponent>();
    targetCollider.HalfExtents={0.5f,0.5f,0.5f};
    targetBody.AllowSleep=false;
    targetBody.LinearDamping=0.0f;
    targetBody.AngularDamping=0.0f;

    Entity sphere=collisionScene.CreateEntity("AngularImpactSphere");
    auto& sphereTransform=sphere.GetComponent<TransformComponent>();
    sphereTransform.Position={-0.9f,0.35f,0.0f};
    auto& sphereCollider=sphere.AddComponent<ColliderComponent>();
    sphereCollider.Type=ColliderType::Sphere;
    sphereCollider.Radius=0.45f;
    sphereCollider.Restitution=0.0f;
    sphereCollider.StaticFriction=0.0f;
    sphereCollider.DynamicFriction=0.0f;
    auto& sphereBody=sphere.AddComponent<RigidBodyComponent>();
    sphereBody.SetBodyType(BodyType::Dynamic);
    sphereBody.UseGravity=false;
    sphereBody.AllowSleep=false;
    sphereBody.LinearDamping=0.0f;
    sphereBody.AngularDamping=0.0f;
    sphereBody.LinearVelocity={3.0f,0.0f,0.0f};

    collisionWorld.Step(collisionScene,1.0f/60.0f);
    assert(IsFinite(targetBody.AngularVelocity));
    assert(std::abs(targetBody.AngularVelocity.z)>1.0e-5f);
}

void RunContactPersistenceWarmStartSelfTests()
{
    const StackResult cold=RunBoxStackScenario(false,1);const StackResult warm=RunBoxStackScenario(true,1);assert(cold.AllFinite);assert(warm.AllFinite);assert(warm.PersistentImpulseFrames>0);assert(warm.TopHeight>6.5f);assert(warm.TopHeight<8.5f);assert(warm.MaximumPenetration<=cold.MaximumPenetration+0.05f);
}

void RunBoxStackStressTest()
{
    const StackResult result=RunBoxStackScenario(true,8);assert(result.AllFinite);assert(result.PersistentImpulseFrames>100);assert(result.TopHeight>6.5f&&result.TopHeight<8.5f);assert(result.MaximumPenetration<0.25f);
}

void RunPhysicsCollisionSelfTests()
{
    RunDynamicAABBTreeSelfTests();
    RunOBBFoundationSelfTests();
    RunSphereBoxSelfTests();
    RunBoxBoxSelfTests();
    RunAngularDynamicsSelfTests();
    RunContactPersistenceWarmStartSelfTests();
    RunBoxStackStressTest();
}

} // namespace Raven::ph::tests
