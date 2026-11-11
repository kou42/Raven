#include "Raven/Scene/Tests/SceneTransformSelfTests.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneCameraSystem.h"
#include "Raven/Math/MathUtility.h"
#include "Raven/Animation/AnimationSystem.h"
#include "Raven/Animation/Animator.h"
#include "Raven/Physics/RigidBodyDynamics.h"
#include "Raven/Physics/Tests/PhysicsSimulationWorldSelfTests.h"
#include "Raven/Physics/Tests/SoftBodyIntegratedStepSelfTests.h"
#include "Raven/Physics/Tests/StaticMeshTriangleBVHSelfTests.h"
#include "Raven/Animation/Tests/BlendTreeRuntimeSelfTests.h"
#include "Raven/Character/Tests/CharacterSprintLocomotionSelfTests.h"
#include "Raven/Renderer/Mesh/MeshGeometry.h"
#include "Raven/Core/CPUProfiler.h"

// 既存の独立テストには専用ヘッダーがないため、このCPU回帰集約入口で宣言します。
namespace Raven::tests
{
void RunCharacterMovingPlatformSelfTests();
void RunCharacterRagdollRecoverySelfTests();
void RunCharacterCapsuleCollisionSelfTests();
}
namespace Raven::ph::tests
{
void RunPhysicsCollisionSelfTests();
void RunCapsuleCollisionSelfTests();
}

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Raven::tests
{
void RunScenePhysicsCollisionSelfTests()
{
    ph::tests::RunPhysicsCollisionSelfTests();
}
void RunSceneSimulationSelfTests(const char* group)
{
    const struct TestCase
    {
        const char* Name;
        void (*Run)();
    } cases[] = {
        {"Transform", RunSceneTransformSelfTests},
        {"PhysicsSimulationWorld", ph::tests::RunPhysicsSimulationWorldSelfTests},
        {"CapsuleCollision", ph::tests::RunCapsuleCollisionSelfTests},
        {"StaticMeshBVH", ph::tests::RunStaticMeshTriangleBVHSelfTests},
        {"SoftBodyIntegratedStep", ph::tests::RunSoftBodyIntegratedStepSelfTests},
        {"MovingPlatform", RunCharacterMovingPlatformSelfTests},
        {"RagdollRecovery", RunCharacterRagdollRecoverySelfTests},
        {"CharacterCapsule", RunCharacterCapsuleCollisionSelfTests},
        {"CharacterLocomotion", RunCharacterSprintLocomotionSelfTests},
        {"BlendTree", RunBlendTreeRuntimeSelfTests}
    };
    bool selected = false;
    for (const auto& test : cases)
    {
        if (group != nullptr && std::strcmp(group, test.Name) != 0)
        {
            continue;
        }
        selected = true;
        std::fprintf(stderr, "Scene simulation: %s\n", test.Name);
        test.Run();
    }
    assert(selected == true);
}
namespace
{
void Equal(const math::Mat4& a, const math::Mat4& b)
{
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            assert(std::fabs(a.m[row][column] - b.m[row][column]) < 0.001f);
        }
    }
}
math::Mat4 World(Scene& scene, Entity entity)
{
    math::Mat4 result;
    const bool valid = scene.TryGetEntityWorldMatrix(entity.GetHandle(), result);
    assert(valid == true);
    return result;
}
void Near(float actual, float expected)
{
    assert(std::fabs(actual - expected) < 0.001f);
}

void RunSimulationTransformTests()
{
    constexpr float Step = 1.0f / 60.0f;
    Scene scene;
    // 子を先に生成し、Storage順序ではなく親→子の依存解決で同期することを確認します。
    Entity bodyEntity = scene.CreateEntity("Dynamic");
    Entity parent = scene.CreateEntity("Parent");
    parent.GetComponent<TransformComponent>().Position.x = 10;
    bodyEntity.GetComponent<TransformComponent>().Position.x = 12;
    auto& body = bodyEntity.AddComponent<RigidBodyComponent>();
    body.UseGravity = false;
    body.LinearDamping = 0;
    body.AllowSleep = false;
    body.LinearVelocity.x = 3;
    bodyEntity.AddComponent<ColliderComponent>();
    assert(scene.SetEntityParent(bodyEntity.GetHandle(), parent.GetHandle()) == true);
    assert(scene.EnableLocalTransform(bodyEntity.GetHandle()) == true);
    Near(bodyEntity.GetComponent<LocalTransformComponent>().Value.Position.x, 2);
    parent.GetComponent<TransformComponent>().Position.x = 20;
    scene.OnUpdate(0); // 0 StepでもDynamicは親に引きずられません。
    Near(World(scene, bodyEntity).m[0][3], 12);
    Near(bodyEntity.GetComponent<LocalTransformComponent>().Value.Position.x, -8);
    scene.OnUpdate(Step * 2);
    Near(World(scene, bodyEntity).m[0][3], 12.1f);
    Near(bodyEntity.GetComponent<LocalTransformComponent>().Value.Position.x, -7.9f);
    body.IsSleeping = true;
    parent.GetComponent<TransformComponent>().Rotation.y = 0.7f;
    scene.OnUpdate(0);
    Near(World(scene, bodyEntity).m[0][3], 12.1f);
    scene.GetPhysicsWorld().Teleport(scene, bodyEntity, math::Vec3(14, 0, 0));
    Near(World(scene, bodyEntity).m[0][3], 14);
    assert(body.IsSleeping == false);
    TransformComponent authored = bodyEntity.GetComponent<TransformComponent>();
    authored.Rotation = math::Vec3(0.2f, 0.3f, -0.4f);
    assert(scene.SetWorldTransform(bodyEntity.GetHandle(), authored) == true);
    const math::Mat3 orientation = body.Orientation.ToMat3();
    const math::Mat4 expectedRotation = authored.GetTransform();
    for (int r = 0; r < 3; ++r)
    {
        for (int c = 0; c < 3; ++c)
        {
            Near(orientation[r][c], expectedRotation.m[r][c]);
        }
    }
    // DynamicのLocal直接編集を暗黙にPhysics入力へ採用しません。
    bodyEntity.GetComponent<LocalTransformComponent>().Value.Position.x += 1;
    assert(scene.EvaluateWorldTransforms() == false);
    assert(scene.SetWorldTransform(bodyEntity.GetHandle(), authored) == true);
    const auto before = parent.GetComponent<TransformComponent>();
    auto nonuniform = before;
    nonuniform.Scale.x = 2;
    assert(scene.SetWorldTransform(parent.GetHandle(), nonuniform) == false);
    Equal(parent.GetComponent<TransformComponent>().GetTransform(), before.GetTransform());
    parent.GetComponent<TransformComponent>().Scale.x = 0;
    const auto frozen = bodyEntity.GetComponent<TransformComponent>();
    auto& profiler = CPUProfiler::Get();
    const bool profilerEnabled = profiler.IsEnabled();
    profiler.SetEnabled(true);
    profiler.BeginFrame();
    scene.OnUpdate(Step);
    profiler.BeginFrame();
    bool synchronizationFailureRecorded = false;
    for (const auto& counter : profiler.GetLastFrame().Counters)
    {
        if (counter.Name == "Scene.Transform.SynchronizationFailures")
        {
            synchronizationFailureRecorded = counter.Value == 1;
        }
    }
    assert(synchronizationFailureRecorded == true);
    profiler.SetEnabled(profilerEnabled);
    Equal(bodyEntity.GetComponent<TransformComponent>().GetTransform(), frozen.GetTransform());
    parent.GetComponent<TransformComponent>() = before;
    assert(scene.EvaluateWorldTransforms() == true);

    Entity nested = scene.CreateEntity("NestedDynamic");
    nested.GetComponent<TransformComponent>().Position = math::Vec3(14, 2, 0);
    auto& nestedBody = nested.AddComponent<RigidBodyComponent>();
    nestedBody.UseGravity = false;
    nestedBody.LinearDamping = 0;
    nestedBody.LinearVelocity.y = 1;
    assert(scene.SetEntityParent(nested.GetHandle(), bodyEntity.GetHandle()) == true);
    assert(scene.EnableLocalTransform(nested.GetHandle()) == true);
    scene.OnUpdate(Step);
    Near(World(scene, nested).m[0][3], 14); // 親Dynamicの速度はJoint拘束のように継承しません。
    Near(World(scene, nested).m[1][3], 2 + Step);
    const auto nestedWorld = World(scene, nested);
    scene.DestroyEntity(bodyEntity);
    Equal(World(scene, nested), nestedWorld);

    Scene kinematics;
    Entity rig = kinematics.CreateEntity("Rig");
    Entity moving = kinematics.CreateEntity("Kinematic");
    assert(kinematics.EnableLocalTransform(moving.GetHandle()) == true);
    auto& kinematic = moving.AddComponent<RigidBodyComponent>(); // 逆の追加順序
    kinematic.SetBodyType(BodyType::Kinematic);
    kinematic.LinearVelocity.x = 3;
    moving.AddComponent<ColliderComponent>();
    assert(kinematics.SetEntityParent(moving.GetHandle(), rig.GetHandle()) == true);
    rig.GetComponent<TransformComponent>().Position.x = 5;
    rig.GetComponent<TransformComponent>().Rotation.y = 0.5f;
    kinematics.OnUpdate(Step * 2);
    Near(World(kinematics, moving).m[0][3], 5.1f);
    Near(moving.GetComponent<TransformComponent>().Rotation.y, 0.5f);
    rig.GetComponent<TransformComponent>().Position.x = 7;
    kinematics.OnUpdate(0);
    Near(World(kinematics, moving).m[0][3], 7.1f);
    kinematics.GetPhysicsWorld().MovePosition(kinematics, moving, math::Vec3(9, 0, 0));
    Near(World(kinematics, moving).m[0][3], 9);

    Scene unsupported;
    Entity stretched = unsupported.CreateEntity("Stretched");
    stretched.GetComponent<TransformComponent>().Scale.x = 2;
    assert(unsupported.EnableLocalTransform(stretched.GetHandle()) == true);
    bool rejected = false;
    try
    {
        stretched.AddComponent<RigidBodyComponent>();
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    assert(rejected == true);
    assert(stretched.HasComponent<RigidBodyComponent>() == false);
    stretched.AddComponent<ColliderComponent>(); // Colliderのみなら従来の形状契約を維持
    stretched.AddComponent<MeshDeformationComponent>();
    Entity legacy = unsupported.CreateEntity("Legacy");
    legacy.GetComponent<TransformComponent>().Scale.x = 2;
    legacy.AddComponent<RigidBodyComponent>();
    assert(unsupported.EnableLocalTransform(legacy.GetHandle()) == false);
    assert(legacy.HasComponent<LocalTransformComponent>() == false);

    // Animation TrackはLocal、Physicsへ渡す入力はWorld。DynamicはAnimatorで上書きしません。
    Scene animated;
    Entity animationRig = animated.CreateEntity("AnimationRig");
    Entity animatedBody = animated.CreateEntity("AnimatedKinematic");
    animationRig.GetComponent<TransformComponent>().Position.x = 10;
    auto clip = std::make_shared<AnimationClip>(1.0f);
    clip->GetTransformTrack().PositionKeys = {{0, math::Vec3(2, 0, 0)}};
    const auto rotation = ph::PhysicsOrientationFromEuler(math::Vec3(0.2f, -0.3f, 0.4f));
    clip->GetTransformTrack().RotationKeys = {{0, rotation}};
    auto animator = std::make_shared<Animator>();
    animator->Play(clip);
    animatedBody.AddComponent<AnimatorComponent>().Instance = animator;
    auto& animatedRigid = animatedBody.AddComponent<RigidBodyComponent>();
    animatedRigid.SetBodyType(BodyType::Kinematic);
    assert(animated.EnableLocalTransform(animatedBody.GetHandle()) == true);
    assert(animated.SetEntityParent(animatedBody.GetHandle(), animationRig.GetHandle()) == true);
    animated.OnUpdate(Step);
    Near(World(animated, animatedBody).m[0][3], 12);
    TransformComponent expected;
    expected.Position.x = 12;
    expected.Rotation = math::Vec3(0.2f, -0.3f, 0.4f);
    Equal(World(animated, animatedBody), expected.GetTransform());
    animatedRigid.SetBodyType(BodyType::Dynamic);
    animatedRigid.UseGravity = false;
    animatedRigid.LinearVelocity.x = 1;
    animatedRigid.LinearDamping = 0;
    animationRig.GetComponent<TransformComponent>().Position.x = 50;
    animated.OnUpdate(Step);
    Near(World(animated, animatedBody).m[0][3], 12 + Step);
    animatedRigid.SetBodyType(BodyType::Static);
    animated.OnUpdate(Step);
    Near(World(animated, animatedBody).m[0][3], 52);

    // Colliderの座標/Offset/Scale規約を変えず、階層評価後のWorld入力をLegacyと比較します。
    for (ColliderType type : {ColliderType::Sphere, ColliderType::Box, ColliderType::Capsule, ColliderType::StaticMesh})
    {
        Scene localScene;
        Scene legacyScene;
        Entity colliderRig = localScene.CreateEntity("ColliderRig");
        Entity localShape = localScene.CreateEntity("LocalShape");
        Entity legacyShape = legacyScene.CreateEntity("LegacyShape");
        auto& rigTransform = colliderRig.GetComponent<TransformComponent>();
        rigTransform.Position = math::Vec3(7, 1, -2);
        rigTransform.Rotation = math::Vec3(0.2f, 0.3f, 0.1f);
        rigTransform.Scale = math::Vec3(2, 2, 2);
        assert(localScene.SetEntityParent(localShape.GetHandle(), colliderRig.GetHandle()) == true);
        assert(localScene.EnableLocalTransform(localShape.GetHandle(), ReparentTransformMode::KeepLocal) == true);
        ColliderComponent collider;
        collider.Type = type;
        collider.Offset = math::Vec3(0.1f, 0.2f, 0.3f);
        MeshVertex a;
        MeshVertex b;
        MeshVertex c;
        a.Position = math::Vec3(-3, 0, -3);
        b.Position = math::Vec3(0, 0, 3);
        c.Position = math::Vec3(3, 0, -3);
        collider.StaticMeshGeometry = std::make_shared<MeshGeometry>(
            std::vector<MeshVertex>{a, b, c}, std::vector<uint32_t>{0, 1, 2});
        localShape.AddComponent<ColliderComponent>(collider);
        legacyShape.AddComponent<ColliderComponent>(collider);
        legacyShape.GetComponent<TransformComponent>() = localShape.GetComponent<TransformComponent>();
        // Query直前の親変更を拾うことも確認します。
        rigTransform.Position.x += 5;
        legacyShape.GetComponent<TransformComponent>().Position.x += 5;
        const auto origin = legacyShape.GetComponent<TransformComponent>().Position + math::Vec3(0, 10, 0);
        ph::PhysicsRayCastFilter filter;
        filter.IncludeStatic = true;
        ph::PhysicsRayCastHit localHit;
        ph::PhysicsRayCastHit legacyHit;
        assert(localScene.GetPhysicsWorld().RayCast(localScene, origin, math::Vec3(0, -1, 0), 20, filter, localHit) == true);
        assert(legacyScene.GetPhysicsWorld().RayCast(legacyScene, origin, math::Vec3(0, -1, 0), 20, filter, legacyHit) == true);
        Near(localHit.Fraction, legacyHit.Fraction);
        Near(localHit.Normal.x, legacyHit.Normal.x);
        Near(localHit.Normal.y, legacyHit.Normal.y);
        Near(localHit.Normal.z, legacyHit.Normal.z);
    }
}
}

void RunSceneTransformSelfTests()
{
    RunSimulationTransformTests();
    Scene scene;
    Entity root = scene.CreateEntity("Root");
    Entity child = scene.CreateEntity("Child");
    root.GetComponent<TransformComponent>().Position.x = 10;
    child.GetComponent<TransformComponent>().Position.x = 3;
    assert(scene.SetEntityParent(child.GetHandle(), root.GetHandle()) == true);
    const auto legacy = child.GetComponent<TransformComponent>().GetTransform();
    Equal(World(scene, child), legacy); // opt-outなら親のTransformを乗算しません。
    assert(scene.EnableLocalTransform(child.GetHandle()) == true);
    Equal(World(scene, child), legacy);
    assert(root.RemoveComponent<TransformComponent>() == false);
    Near(child.GetComponent<LocalTransformComponent>().Value.Position.x, -7);
    root.GetComponent<TransformComponent>().Position.x = 20;
    assert(scene.EvaluateWorldTransforms() == true);
    Near(World(scene, child).m[0][3], 13);
    const uint64_t count = scene.GetWorldTransformRecomputationCount();
    assert(scene.EvaluateWorldTransforms() == true);
    World(scene, child);
    assert(scene.GetWorldTransformRecomputationCount() == count);
    math::Mat4 evaluated;
    assert(scene.TryGetEvaluatedWorldMatrix(child.GetHandle(), evaluated) == true);
    Equal(evaluated, World(scene, child));

    TransformComponent local;
    local.Position = math::Vec3(1, 2, 3);
    assert(scene.SetLocalTransform(child.GetHandle(), local) == true);
    Near(World(scene, child).m[0][3], 21);
    child.GetComponent<TransformComponent>().Position.x = 25; // 既存World編集窓口
    assert(scene.EvaluateWorldTransforms() == true);
    Near(child.GetComponent<LocalTransformComponent>().Value.Position.x, 5);
    child.GetComponent<LocalTransformComponent>().Value.Position.y = 8; // Local直接編集も検出
    assert(scene.EvaluateWorldTransforms() == true);
    Near(World(scene, child).m[1][3], 8);
    child.GetComponent<LocalTransformComponent>().Value.Position.x = 6;
    child.GetComponent<TransformComponent>().Position.x = 26;
    assert(scene.EvaluateWorldTransforms() == false); // 二重書込みの意図を推測しません。
    assert(scene.TryGetEvaluatedWorldMatrix(child.GetHandle(), evaluated) == false);
    assert(scene.SetLocalTransform(child.GetHandle(), local) == true); // 明示APIで修復

    Entity other = scene.CreateEntity("Other");
    other.GetComponent<TransformComponent>().Position.x = 100;
    assert(scene.SetEntityParent(child.GetHandle(), other.GetHandle(), ReparentTransformMode::KeepLocal) == true);
    Near(World(scene, child).m[0][3], 101);
    assert(scene.SetEntityParent(child.GetHandle(), root.GetHandle(), ReparentTransformMode::KeepWorld) == true);
    Near(World(scene, child).m[0][3], 101);
    Near(child.GetComponent<LocalTransformComponent>().Value.Position.x, 81);
    const auto beforeDisable = World(scene, child);
    assert(scene.DisableLocalTransform(child.GetHandle()) == true);
    root.GetComponent<TransformComponent>().Position.x = 200;
    Equal(World(scene, child), beforeDisable);
    assert(scene.EnableLocalTransform(child.GetHandle()) == true);
    const auto beforeDetach = World(scene, child);
    assert(child.RemoveComponent<RelationshipComponent>() == true);
    Equal(World(scene, child), beforeDetach);
    assert(scene.GetEntityParent(child.GetHandle()) == InvalidEntityHandle);
    assert(child.RemoveComponent<TransformComponent>() == false);
    assert(child.RemoveComponent<LocalTransformComponent>() == true);
    Equal(World(scene, child), beforeDetach);

    // 無効な変更が子孫へShearを作る場合も、親やLocalを変更せず拒否します。
    Scene shapes;
    Entity top = shapes.CreateEntity("Top");
    Entity rotated = shapes.CreateEntity("Rotated");
    rotated.GetComponent<TransformComponent>().Rotation.z = 0.5f;
    assert(shapes.EnableLocalTransform(top.GetHandle()) == true);
    assert(shapes.SetEntityParent(rotated.GetHandle(), top.GetHandle()) == true);
    assert(shapes.EnableLocalTransform(rotated.GetHandle(), ReparentTransformMode::KeepLocal) == true);
    const auto beforeShape = World(shapes, rotated);
    TransformComponent stretched;
    stretched.Scale = math::Vec3(2, 1, 1);
    assert(shapes.SetLocalTransform(top.GetHandle(), stretched) == false);
    Equal(World(shapes, rotated), beforeShape);
    Near(top.GetComponent<LocalTransformComponent>().Value.Scale.x, 1);
    Entity nonuniform = shapes.CreateEntity("Nonuniform");
    nonuniform.GetComponent<TransformComponent>() = stretched;
    assert(shapes.SetEntityParent(rotated.GetHandle(), nonuniform.GetHandle(), ReparentTransformMode::KeepLocal) == false);
    assert(shapes.SetEntityParent(rotated.GetHandle(), nonuniform.GetHandle(), ReparentTransformMode::KeepWorld) == false);
    assert(shapes.GetEntityParent(rotated.GetHandle()) == top.GetHandle());
    Equal(World(shapes, rotated), beforeShape);
    nonuniform.GetComponent<TransformComponent>().Scale.x = 0;
    assert(shapes.EnableLocalTransform(nonuniform.GetHandle()) == false);
    assert(nonuniform.HasComponent<LocalTransformComponent>() == false);
    assert(shapes.SetEntityParent(rotated.GetHandle(), nonuniform.GetHandle()) == false);
    assert(shapes.DisableLocalTransform(top.GetHandle()) == true);
    assert(shapes.SetWorldTransform(top.GetHandle(), stretched) == false);
    Near(top.GetComponent<TransformComponent>().Scale.x, 1);

    // 有限の逆行列でも、再構成時の丸めでWorld=3が0へ変わるような変更は拒否します。
    Scene precision;
    Entity huge = precision.CreateEntity("HugeParent");
    huge.GetComponent<TransformComponent>().Position.x = 1.0e30f;
    Entity small = precision.CreateEntity("SmallWorld");
    small.GetComponent<TransformComponent>().Position.x = 3;
    assert(precision.SetEntityParent(small.GetHandle(), huge.GetHandle()) == true);
    assert(precision.EnableLocalTransform(small.GetHandle()) == false);
    assert(small.HasComponent<LocalTransformComponent>() == false);
    Near(small.GetComponent<TransformComponent>().Position.x, 3);
    assert(precision.EnableLocalTransform(small.GetHandle(), ReparentTransformMode::KeepLocal) == true);
    TransformComponent requested;
    requested.Position.x = 3;
    assert(precision.SetWorldTransform(small.GetHandle(), requested) == false);
    assert(small.GetComponent<TransformComponent>().Position.x == 1.0e30f);
    Entity independent = precision.CreateEntity("IndependentLocal");
    independent.GetComponent<TransformComponent>().Position.x = 3;
    assert(precision.EnableLocalTransform(independent.GetHandle()) == true);
    assert(precision.SetEntityParent(independent.GetHandle(), huge.GetHandle()) == false);
    assert(precision.GetEntityParent(independent.GetHandle()) == InvalidEntityHandle);
    Near(World(precision, independent).m[0][3], 3);

    // Dirtyな親のまま破棄しても、子と孫のWorldを保持します。
    Scene lifetime;
    Entity parent = lifetime.CreateEntity("Parent");
    Entity localChild = lifetime.CreateEntity("LocalChild");
    Entity grandchild = lifetime.CreateEntity("Grandchild");
    parent.GetComponent<TransformComponent>().Position.x = 10;
    localChild.GetComponent<TransformComponent>().Position.x = 2;
    grandchild.GetComponent<TransformComponent>().Position.x = 1;
    assert(lifetime.SetEntityParent(localChild.GetHandle(), parent.GetHandle()) == true);
    assert(lifetime.EnableLocalTransform(localChild.GetHandle(), ReparentTransformMode::KeepLocal) == true);
    assert(lifetime.SetEntityParent(grandchild.GetHandle(), localChild.GetHandle()) == true);
    assert(lifetime.EnableLocalTransform(grandchild.GetHandle(), ReparentTransformMode::KeepLocal) == true);
    parent.GetComponent<TransformComponent>().Position.x = 20;
    lifetime.DestroyEntity(parent);
    Near(World(lifetime, localChild).m[0][3], 22);
    Near(World(lifetime, grandchild).m[0][3], 23);
    assert(lifetime.GetEntityParent(localChild.GetHandle()) == InvalidEntityHandle);
    const EntityHandle oldChild = localChild.GetHandle();
    lifetime.DestroyEntity(localChild);
    Near(World(lifetime, grandchild).m[0][3], 23);
    Entity reused = lifetime.CreateEntity("Reused");
    assert(reused.GetIndex() == oldChild.m_Index);
    Near(World(lifetime, reused).m[0][3], 0);
    assert(lifetime.TryGetEntityWorldMatrix(oldChild, evaluated) == false);

    assert(lifetime.SetEntityParent(grandchild.GetHandle(), reused.GetHandle()) == true);
    reused.GetComponent<TransformComponent>().Scale.x = 0;
    assert(lifetime.EvaluateWorldTransforms() == false);
    lifetime.DestroyEntity(reused); // 解決不能な親でも最後の有効Worldを引き継ぎます。
    Near(World(lifetime, grandchild).m[0][3], 23);

    // SG3ではComponentの追加順序に依存せずLocalとSimulationを併用できます。
    bool refused = false;
    try { grandchild.AddComponent<RigidBodyComponent>(); }
    catch (const std::invalid_argument&) { refused = true; }
    assert(refused == false);
    Entity physical = lifetime.CreateEntity("Physical");
    physical.AddComponent<ColliderComponent>();
    assert(lifetime.EnableLocalTransform(physical.GetHandle()) == true);
    assert(physical.RemoveComponent<LocalTransformComponent>() == true);
    refused = false;
    try { physical.AddComponent<LocalTransformComponent>(); }
    catch (const std::invalid_argument&) { refused = true; }
    assert(refused == false);

    Scene deep;
    Entity deepRoot = deep.CreateEntity("DeepRoot");
    Entity previous = deepRoot;
    constexpr int Depth = 512;
    for (int i = 0; i < Depth; ++i)
    {
        Entity next = deep.CreateEntity("DeepLocal");
        next.GetComponent<TransformComponent>().Position.x = 0.01f;
        assert(deep.SetEntityParent(next.GetHandle(), previous.GetHandle()) == true);
        assert(deep.EnableLocalTransform(next.GetHandle(), ReparentTransformMode::KeepLocal) == true);
        previous = next;
    }
    assert(deep.EvaluateWorldTransforms() == true);
    Near(World(deep, previous).m[0][3], Depth * 0.01f);
    deepRoot.GetComponent<TransformComponent>().Position.x = 2;
    assert(deep.EvaluateWorldTransforms() == true);
    Near(World(deep, previous).m[0][3], 2 + Depth * 0.01f);
    assert(deep.CanSetEntityParent(deepRoot.GetHandle(), previous.GetHandle()) == false);

    Scene corrupt;
    Entity cycleA = corrupt.CreateEntity("CycleA");
    Entity cycleB = corrupt.CreateEntity("CycleB");
    assert(corrupt.EnableLocalTransform(cycleA.GetHandle()) == true);
    assert(corrupt.EnableLocalTransform(cycleB.GetHandle()) == true);
    assert(corrupt.SetEntityParent(cycleB.GetHandle(), cycleA.GetHandle()) == true);
    cycleA.AddComponent<RelationshipComponent>(RelationshipComponent{cycleB.GetHandle()});
    assert(corrupt.EvaluateWorldTransforms() == false);
    assert(corrupt.TryGetEntityWorldMatrix(cycleA.GetHandle(), evaluated) == false);
    assert(corrupt.SetEntityParentAndLocal(cycleA.GetHandle(), InvalidEntityHandle, TransformComponent{}) == true);
    assert(corrupt.EvaluateWorldTransforms() == true);

    Scene cameras;
    Entity rig = cameras.CreateEntity("Rig");
    Entity camera = cameras.CreateEntity("Camera");
    rig.GetComponent<TransformComponent>().Position = math::Vec3(1, 2, 3);
    rig.GetComponent<TransformComponent>().Rotation.y = 0.8f;
    camera.GetComponent<TransformComponent>().Position.z = 5;
    camera.AddComponent<CameraComponent>();
    assert(cameras.SetEntityParent(camera.GetHandle(), rig.GetHandle()) == true);
    assert(cameras.EnableLocalTransform(camera.GetHandle(), ReparentTransformMode::KeepLocal) == true);
    const auto expectedWorld = rig.GetComponent<TransformComponent>().GetTransform()
        * camera.GetComponent<LocalTransformComponent>().Value.GetTransform();
    Equal(World(cameras, camera), expectedWorld);
    auto* resolvedCamera = SceneCameraSystem::UpdatePrimaryCamera(cameras, 1280, 720);
    assert(resolvedCamera != nullptr);
    const auto inversePose = math::Mat4::LookAt(
        math::Vec3(expectedWorld.m[0][3], expectedWorld.m[1][3], expectedWorld.m[2][3]),
        math::Vec3(expectedWorld.m[0][3] - expectedWorld.m[0][2], expectedWorld.m[1][3] - expectedWorld.m[1][2], expectedWorld.m[2][3] - expectedWorld.m[2][2]),
        math::Vec3(expectedWorld.m[0][1], expectedWorld.m[1][1], expectedWorld.m[2][1]));
    Equal(resolvedCamera->GetViewMatrix(), inversePose);
    const auto projection = resolvedCamera->GetProjectionMatrix();
    rig.GetComponent<TransformComponent>().Scale.x = 0;
    assert(SceneCameraSystem::UpdatePrimaryCamera(cameras, 1280, 720) == nullptr);
    rig.GetComponent<TransformComponent>().Scale.x = 1;
    assert(SceneCameraSystem::UpdatePrimaryCamera(cameras, 1280, 720) != nullptr);
    assert(cameras.DisableLocalTransform(camera.GetHandle()) == true);
    auto* legacyCamera = SceneCameraSystem::UpdatePrimaryCamera(cameras, 1280, 720);
    assert(legacyCamera != nullptr);
    Equal(legacyCamera->GetViewMatrix(), inversePose);
    Equal(legacyCamera->GetProjectionMatrix(), projection);
    Scene reflection;
    Entity reflectedCamera = reflection.CreateEntity("ReflectedCamera");
    reflectedCamera.AddComponent<CameraComponent>();
    reflectedCamera.GetComponent<TransformComponent>().Scale = math::Vec3(1, -1, -1);
    auto* beforeCamera = SceneCameraSystem::UpdatePrimaryCamera(reflection, 1280, 720);
    assert(beforeCamera != nullptr);
    const auto beforeView = beforeCamera->GetViewMatrix();
    assert(reflection.EnableLocalTransform(reflectedCamera.GetHandle()) == false);
    assert(reflectedCamera.HasComponent<LocalTransformComponent>() == false);
    Equal(SceneCameraSystem::UpdatePrimaryCamera(reflection, 1280, 720)->GetViewMatrix(), beforeView);
    reflectedCamera.GetComponent<TransformComponent>().Scale = math::Vec3(1, 1, 1);
    assert(reflection.EnableLocalTransform(reflectedCamera.GetHandle()) == true);
    auto reflectedRequest = reflectedCamera.GetComponent<TransformComponent>();
    reflectedRequest.Scale = math::Vec3(1, -1, -1);
    assert(reflection.SetWorldTransform(reflectedCamera.GetHandle(), reflectedRequest) == false);
    Equal(SceneCameraSystem::UpdatePrimaryCamera(reflection, 1280, 720)->GetViewMatrix(), beforeView);

}
}
