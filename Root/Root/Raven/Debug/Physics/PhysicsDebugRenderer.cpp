#include "Raven/Debug/Physics/PhysicsDebugRenderer.h"

#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Renderer/Debug/DebugRenderer.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{

// Debug表示の意味を視覚的に区別するための色です。
// 色自体にPhysics上の意味はなく、将来Debug UIから変更可能にしても構いません。
const math::Vec3 ContactPointColor{ 1.0f, 0.2f, 0.2f };
const math::Vec3 ContactNormalColor{ 1.0f, 1.0f, 0.2f };
const math::Vec3 TightAABBColor{ 0.2f, 1.0f, 0.2f };
const math::Vec3 FatAABBColor{ 0.2f, 0.7f, 1.0f };
const math::Vec3 TreeBranchColor{ 0.7f, 0.3f, 1.0f };

void DrawContacts(
    const PhysicsWorld& physicsWorld,
    const PhysicsDebugSettings& settings)
{
    if (!settings.ShowContactPoints && !settings.ShowContactNormals)
    {
        return;
    }

    // ContactManifoldは1組のColliderに最大4点を保持します。
    // NormalはManifold共通でA -> B方向なので、各Contact Pointを始点として
    // 同じNormalを描画します。
    for (const ContactManifold& manifold : physicsWorld.GetContactManifolds())
    {
        for (std::size_t pointIndex = 0; pointIndex < manifold.PointCount; ++pointIndex)
        {
            const ContactPoint& point = manifold.Points[pointIndex];

            if (settings.ShowContactPoints)
            {
                DebugRenderer::DrawPoint(
                    point.Position,
                    settings.ContactPointRadius,
                    ContactPointColor);
            }

            if (settings.ShowContactNormals)
            {
                DebugRenderer::DrawLine(
                    point.Position,
                    point.Position + manifold.Normal * settings.ContactNormalLength,
                    ContactNormalColor);
            }
        }
    }
}

void DrawTightAABBs(Scene& scene)
{
    // Dynamic Treeに格納されているのはFat AABBです。
    // Tight AABBは現在のTransform + Colliderから毎描画時に再計算することで、
    // 「実形状を包むAABB」と「Treeが保持している余裕付きAABB」の差を確認できます。
    //
    // Scene::View()は現時点ではnon-const APIですが、このループではComponentを
    // 一切変更せず読み取りだけを行います。
    for (auto [entity, transform, collider] :
        scene.View<TransformComponent, ColliderComponent>())
    {
        static_cast<void>(entity);

        AABB bounds{};
        if (ComputeColliderAABB(transform, collider, bounds))
        {
            DebugRenderer::DrawAABB(bounds, TightAABBColor);
        }
    }
}

void DrawTree(
    const PhysicsWorld& physicsWorld,
    const PhysicsDebugSettings& settings)
{
    if (!settings.ShowFatAABB && !settings.ShowDynamicAABBTree)
    {
        return;
    }

    const DynamicAABBTree& tree = physicsWorld.GetBroadPhase().GetTree();
    const auto& nodes = tree.GetNodes();

    for (const DynamicAABBTreeNode& node : nodes)
    {
        // DynamicAABBTreeではfree nodeのHeightを-1として扱う設計です。
        // Free List上の未使用Nodeまで描画すると古いBoundsが残って見えるため除外します。
        if (node.Height < 0)
        {
            continue;
        }

        if (node.IsLeaf())
        {
            if (settings.ShowFatAABB || settings.ShowDynamicAABBTree)
            {
                DebugRenderer::DrawAABB(node.Bounds, FatAABBColor);
            }
        }
        else if (settings.ShowDynamicAABBTree)
        {
            // Branch Boundsは2つの子を包むAABBです。
            // Leafとは色を分けることで、Treeの階層構造や過剰に大きいBranchを
            // ワールド上から確認しやすくします。
            DebugRenderer::DrawAABB(node.Bounds, TreeBranchColor);
        }
    }
}

} // namespace

void PhysicsDebugRenderer::Draw(
    Scene& scene,
    const PhysicsWorld& physicsWorld,
    const PhysicsDebugSettings& settings)
{
    DrawContacts(physicsWorld, settings);

    if (settings.ShowAABB)
    {
        DrawTightAABBs(scene);
    }

    DrawTree(physicsWorld, settings);
}

} // namespace Raven::ph
