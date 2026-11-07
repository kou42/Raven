#include "Raven/Scene/Tests/SceneGraphSelfTests.h"
#include "Raven/Scene/Scene.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

namespace Raven::tests
{
void RunSceneGraphSelfTests()
{
    Scene scene;
    Entity root = scene.CreateEntity("Root");
    Entity branch = scene.CreateEntity("Branch");
    Entity leaf = scene.CreateEntity("Leaf");
    Entity sibling = scene.CreateEntity("Sibling");
    assert(scene.SetEntityParent(branch.GetHandle(), root.GetHandle()) == true);
    assert(scene.SetEntityParent(leaf.GetHandle(), branch.GetHandle()) == true);
    assert(scene.SetEntityParent(sibling.GetHandle(), root.GetHandle()) == true);
    assert(scene.GetEntityChildren(root.GetHandle()).size() == 2u);
    const auto ancestors = scene.GetEntityAncestors(leaf.GetHandle());
    assert(ancestors.size() == 2u);
    assert(ancestors[0] == branch.GetHandle());
    assert(ancestors[1] == root.GetHandle());
    const auto descendants = scene.GetEntityDescendants(root.GetHandle());
    assert(descendants.size() == 3u);
    std::unordered_set<uint64_t> visited;
    visited.insert(root.GetValue());
    for (EntityHandle handle : descendants)
    {
        // 親は必ず先に出現するので、将来のTransform評価順にも利用できます。
        assert(visited.count(scene.GetEntityParent(handle).Value()) == 1u);
        assert(visited.insert(handle.Value()).second == true);
    }
    assert(scene.SetEntityParent(root.GetHandle(), leaf.GetHandle()) == false);
    assert(scene.SetEntityParent(leaf.GetHandle(), leaf.GetHandle()) == false);
    assert(scene.GetEntityDescendants(InvalidEntityHandle).empty() == true);
    assert(scene.GetEntityChildren(InvalidEntityHandle).empty() == true);
    assert(scene.GetEntityAncestors(InvalidEntityHandle).empty() == true);
    assert(scene.DetachEntity(InvalidEntityHandle) == false);

    // 行列・Camera/Physics入力はWorld-spaceのまま。親変更でTRSを分解/再合成しません。
    auto& transform = leaf.GetComponent<TransformComponent>();
    transform.Position = math::Vec3(3.0f, 4.0f, 5.0f);
    transform.Rotation = math::Vec3(0.2f, 0.5f, -0.1f);
    transform.Scale = math::Vec3(2.0f, 3.0f, 4.0f);
    root.GetComponent<TransformComponent>().Scale = math::Vec3(9.0f, 2.0f, 7.0f);
    leaf.AddComponent<CameraComponent>();
    leaf.AddComponent<RigidBodyComponent>().LinearVelocity = math::Vec3(1.0f, 2.0f, 3.0f);
    leaf.AddComponent<ColliderComponent>().Radius = 0.75f;
    const math::Mat4 before = transform.GetTransform();
    const float cameraFov = leaf.GetComponent<CameraComponent>().Camera.GetPerspectiveVerticalFov();
    assert(scene.SetEntityParent(leaf.GetHandle(), root.GetHandle()) == true);
    assert(scene.DetachEntity(leaf.GetHandle()) == true);
    const math::Mat4 after = transform.GetTransform();
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            assert(before.m[row][column] == after.m[row][column]);
        }
    }
    assert(leaf.GetComponent<CameraComponent>().Camera.GetPerspectiveVerticalFov() == cameraFov);
    assert(leaf.GetComponent<RigidBodyComponent>().LinearVelocity == math::Vec3(1.0f, 2.0f, 3.0f));
    assert(leaf.GetComponent<ColliderComponent>().Radius == 0.75f);

    assert(scene.SetEntityParent(leaf.GetHandle(), branch.GetHandle()) == true);
    const EntityHandle oldRoot = root.GetHandle();
    scene.DestroyEntity(root);
    assert(scene.GetEntityParent(branch.GetHandle()) == InvalidEntityHandle);
    assert(scene.GetEntityParent(sibling.GetHandle()) == InvalidEntityHandle);
    assert(scene.GetEntityParent(leaf.GetHandle()) == branch.GetHandle());
    Entity reused = scene.CreateEntity("Reused");
    assert(reused.GetIndex() == oldRoot.m_Index);
    assert(scene.GetEntityChildren(oldRoot).empty() == true);
    assert(scene.GetEntityDescendants(oldRoot).empty() == true);
    assert(scene.GetEntityAncestors(oldRoot).empty() == true);
    assert(scene.CanSetEntityParent(leaf.GetHandle(), oldRoot) == false);
    assert(scene.GetEntityChildren(reused.GetHandle()).empty() == true);

    // 直接Component編集で作られた破損データでも、走査は停止し重複しません。
    assert(scene.SetEntityParent(branch.GetHandle(), leaf.GetHandle()) == false);
    branch.GetComponent<RelationshipComponent>().Parent = leaf.GetHandle();
    assert(scene.GetEntityAncestors(leaf.GetHandle()).empty() == true);
    assert(scene.CanSetEntityParent(reused.GetHandle(), branch.GetHandle()) == false);
    const auto cyclic = scene.GetEntityDescendants(branch.GetHandle());
    assert(cyclic.size() == 1u && cyclic[0] == leaf.GetHandle());
    assert(scene.DetachEntity(branch.GetHandle()) == true);
    leaf.GetComponent<RelationshipComponent>().Parent = oldRoot;
    assert(scene.GetEntityParent(leaf.GetHandle()) == InvalidEntityHandle);
    assert(scene.GetEntityDescendants(reused.GetHandle()).empty() == true);
    assert(scene.DetachEntity(leaf.GetHandle()) == true);

    Scene deep;
    Entity deepRoot = deep.CreateEntity("DeepRoot");
    Entity previous = deepRoot;
    constexpr std::size_t Depth = 1024;
    for (std::size_t i = 0; i < Depth; ++i)
    {
        Entity current = deep.CreateEntity("DeepChild");
        assert(deep.SetEntityParent(current.GetHandle(), previous.GetHandle()) == true);
        previous = current;
    }
    assert(deep.GetEntityAncestors(previous.GetHandle()).size() == Depth);
    assert(deep.GetEntityDescendants(deepRoot.GetHandle()).size() == Depth);
    assert(deep.CanSetEntityParent(deepRoot.GetHandle(), previous.GetHandle()) == false);
    deep.OnDestroy();
    assert(deep.GetEntityDescendants(deepRoot.GetHandle()).empty() == true);
}

void RunSceneGraphMeasurements()
{
    // FullScanは各訪問NodeでGetChildrenを再実行。一時索引方式は構築・返却Vectorのコストも含めます。
    // 数値は診断用で、環境依存の閾値を合否条件にしません。常設キャッシュを採用する根拠とも区別します。
    constexpr std::size_t EntityCount = 1024;
    constexpr std::size_t Repetitions = 3;
    for (bool chain : {false, true})
    {
        Scene scene;
        Entity root = scene.CreateEntity("MeasureRoot");
        Entity parent = root;
        for (std::size_t i = 1; i < EntityCount; ++i)
        {
            Entity child = scene.CreateEntity("MeasureChild");
            if (scene.SetEntityParent(child.GetHandle(), parent.GetHandle()) == false)
            {
                throw std::runtime_error("Measurement hierarchy creation failed.");
            }
            if (chain == true)
            {
                parent = child;
            }
        }
        std::size_t scanned = 0;
        const auto scanStart = std::chrono::steady_clock::now();
        for (std::size_t repeat = 0; repeat < Repetitions; ++repeat)
        {
            std::vector<EntityHandle> pending{root.GetHandle()};
            for (std::size_t i = 0; i < pending.size(); ++i)
            {
                const auto children = scene.GetEntityChildren(pending[i]);
                pending.insert(pending.end(), children.begin(), children.end());
            }
            scanned += pending.size() - 1;
        }
        const auto scanEnd = std::chrono::steady_clock::now();
        std::size_t indexed = 0;
        const auto indexStart = std::chrono::steady_clock::now();
        for (std::size_t repeat = 0; repeat < Repetitions; ++repeat)
        {
            indexed += scene.GetEntityDescendants(root.GetHandle()).size();
        }
        const auto indexEnd = std::chrono::steady_clock::now();
        if (scanned != Repetitions * (EntityCount - 1) || scanned != indexed)
        {
            throw std::runtime_error("Measurement traversal results differ.");
        }
        const auto scanUs = std::chrono::duration_cast<std::chrono::microseconds>(scanEnd - scanStart).count();
        const auto indexUs = std::chrono::duration_cast<std::chrono::microseconds>(indexEnd - indexStart).count();
        std::cout << "SceneGraph shape=" << (chain == true ? "chain" : "wide")
            << " entities=" << EntityCount << " repetitions=" << Repetitions
            << " full_scan_us=" << scanUs << " temporary_index_us=" << indexUs
            << " visited=" << indexed << '\n';
    }
}
}
