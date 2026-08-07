#include <cassert>
#include <cmath>
#include <vector>

#include "Raven/Physics/Collision/CollisionDetection.h"
#include "Raven/Physics/Collision/DynamicAABBTreeValidation.h"
#include "Raven/Physics/PhysicsWorld.h"
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
    body.SetBodyType(type);body.LinearDamping=0.01f;
    // Stress TestではSolver自体の収束を見るためSleepを切ります。
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

    // 床もBox Colliderにすることで、現時点のBox-Box narrow phase / manifold /
    // Dynamic AABB Tree / SolverをまとめてStress Testします。
    Entity floor=CreateBox(scene,{0.0f,-0.5f,0.0f},BodyType::Static);
    floor.GetComponent<ColliderComponent>().HalfExtents={5.0f,0.5f,5.0f};

    constexpr int BoxCount=8;
    std::vector<Entity> boxes;
    boxes.reserve(BoxCount);
    for(int i=0;i<BoxCount;++i)
    {
        // ごく小さい初期隙間を設け、最初の数Stepで自然落下して接触させます。
        boxes.push_back(CreateBox(scene,{0.0f,0.5f+static_cast<float>(i)*1.002f,0.0f},BodyType::Dynamic));
    }

    constexpr float Dt=1.0f/60.0f;
    constexpr int StepCount=600; // 10秒相当。長時間静止で発散しないことも確認。
    StackResult result{};

    for(int step=0;step<StepCount;++step)
    {
        world.Step(scene,Dt);
        for(Entity box:boxes)
        {
            const auto& t=box.GetComponent<TransformComponent>();
            const auto& rb=box.GetComponent<RigidBodyComponent>();
            result.AllFinite=result.AllFinite&&IsFinite(t.Position)&&IsFinite(rb.LinearVelocity);
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

void RunSphereBoxSelfTests()
{
    ColliderComponent s{};s.Type=ColliderType::Sphere;s.Radius=1;ColliderComponent b{};b.Type=ColliderType::Box;b.HalfExtents={1,1,1};TransformComponent bt{},st{};ContactManifold m{};st.Position={1.5f,0,0};assert(GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));assert(m.PointCount==1);assert(NearlyEqual(m.Points[0].Penetration,.5f));assert(NearlyEqual(m.Normal.x,-1));st.Position={};assert(GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));assert(NearlyEqual(m.Points[0].Penetration,2));assert(NearlyEqual(m.Normal.x,1));st.Position={4,0,0};assert(!GenerateSphereBoxManifold(Entity{},st,s,Entity{},bt,b,m));
}

void RunBoxBoxSelfTests()
{
    ColliderComponent a{};a.Type=ColliderType::Box;a.HalfExtents={1,1,1};ColliderComponent b=a;TransformComponent at{},bt{};ContactManifold m{};bt.Position={1.5f,0,0};assert(GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));assert(m.PointCount==4);assert(NearlyEqual(m.Normal.x,1));assert(NearlyEqual(m.Points[0].Penetration,.5f));assert(NearlyEqual(m.Points[0].Position.y,-1));assert(NearlyEqual(m.Points[0].Position.z,-1));assert(NearlyEqual(m.Points[2].Position.y,1));assert(NearlyEqual(m.Points[2].Position.z,1));bt.Position={0,1.75f,0};assert(GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));assert(NearlyEqual(m.Normal.y,1));assert(NearlyEqual(m.Points[0].Penetration,.25f));bt.Position={3,0,0};assert(!GenerateBoxBoxManifold(Entity{},at,a,Entity{},bt,b,m));
}

void RunContactPersistenceWarmStartSelfTests()
{
    // iteration=1はWarm Startの差を意図的に見えやすくする設定です。
    const StackResult cold=RunBoxStackScenario(false,1);
    const StackResult warm=RunBoxStackScenario(true,1);

    assert(cold.AllFinite);
    assert(warm.AllFinite);
    assert(warm.PersistentImpulseFrames>0);

    // 8個積みの理想的な最上段中心Yは7.5付近です。
    // Rotation未対応・Position Correction方式なので厳密一致ではなく、崩壊/落下を
    // 検出するための十分広い範囲で確認します。
    assert(warm.TopHeight>6.5f);
    assert(warm.TopHeight<8.5f);

    // Warm Startによって悪化していないことを回帰条件にします。
    // 数値誤差を考慮して少量のmarginを許容します。
    assert(warm.MaximumPenetration<=cold.MaximumPenetration+0.05f);
}

void RunBoxStackStressTest()
{
    // 通常設定相当の8 iteration + Warm Startで10秒間の8段積みを実行します。
    const StackResult result=RunBoxStackScenario(true,8);
    assert(result.AllFinite);
    assert(result.PersistentImpulseFrames>100);
    assert(result.TopHeight>6.5f&&result.TopHeight<8.5f);

    // 大きくめり込む場合はPosition Correction / Persistence / Solverのどこかに
    // 回帰が入った可能性が高いです。
    assert(result.MaximumPenetration<0.25f);
}

void RunPhysicsCollisionSelfTests()
{
    RunDynamicAABBTreeSelfTests();
    RunSphereBoxSelfTests();
    RunBoxBoxSelfTests();
    RunContactPersistenceWarmStartSelfTests();
    RunBoxStackStressTest();
}

} // namespace Raven::ph::tests
