#include "Raven/Scene/Tests/SceneTransformSelfTests.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneCameraSystem.h"
#include "Raven/Math/MathUtility.h"

#include <cassert>
#include <cmath>

namespace Raven::tests
{
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
}

void RunSceneTransformSelfTests()
{
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

    // SG3以前の二重書込みをComponent追加時にも拒否します。
    bool refused = false;
    try { grandchild.AddComponent<RigidBodyComponent>(); }
    catch (const std::invalid_argument&) { refused = true; }
    assert(refused == true);
    Entity physical = lifetime.CreateEntity("Physical");
    physical.AddComponent<ColliderComponent>();
    assert(lifetime.EnableLocalTransform(physical.GetHandle()) == false);
    refused = false;
    try { physical.AddComponent<LocalTransformComponent>(); }
    catch (const std::invalid_argument&) { refused = true; }
    assert(refused == true);

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
}
}
