#pragma once

#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/BroadPhase.h"

namespace Raven
{

class Scene;

namespace ph
{

// ============================================================================
// PhysicsDebugRenderer
// ============================================================================
// 物理Broad Phaseの内部状態を既存Lines Pipelineで可視化します。
//
// Toggle:
//   B : Colliderのtight AABB
//   F : Dynamic Tree LeafのFat AABB
//   T : Dynamic Tree Branch AABB
//   P : Broad Phase候補Pair
//
// Fat AABBとBranchを分離して表示できるため、MoveProxyの再挿入タイミングと
// SAH/Balance後のTree階層をそれぞれ確認できます。
class PhysicsDebugRenderer
{
public:
    PhysicsDebugRenderer(Scene& scene, const math::Mat4& view, const math::Mat4& projection);
    ~PhysicsDebugRenderer();

    PhysicsDebugRenderer(const PhysicsDebugRenderer&) = delete;
    PhysicsDebugRenderer& operator=(const PhysicsDebugRenderer&) = delete;

    static void RenderRegistered();

private:
    struct DebugVertex
    {
        math::Vec3 Position{};
        math::Vec3 Color{ 1.0f, 1.0f, 1.0f };
        math::Vec2 Texcoord{};
    };

    static std::vector<PhysicsDebugRenderer*>& Registry();

    void EnsureInitialized();
    void UpdateToggleKeys();
    void Render();

    static void AddLine(
        std::vector<DebugVertex>& vertices,
        std::vector<uint32_t>& indices,
        const math::Vec3& a,
        const math::Vec3& b,
        const math::Vec3& color);

    static void AddAABB(
        std::vector<DebugVertex>& vertices,
        std::vector<uint32_t>& indices,
        const AABB& bounds,
        const math::Vec3& color);

private:
    Scene* m_Scene = nullptr;
    const math::Mat4* m_View = nullptr;
    const math::Mat4* m_Projection = nullptr;

    // Debug Renderer側にもBroadPhaseを永続保持します。
    // 毎Renderで作り直すとFat AABBが常に初期化され、MoveProxyの挙動を
    // 観察できなくなるためです。
    BroadPhase m_BroadPhase;
    Ref<Material> m_Material;

    bool m_DrawAABBs = false;
    bool m_DrawFatAABBs = false;
    bool m_DrawTree = false;
    bool m_DrawPairs = false;

    bool m_WasAABBKeyPressed = false;
    bool m_WasFatAABBKeyPressed = false;
    bool m_WasTreeKeyPressed = false;
    bool m_WasPairKeyPressed = false;
};

} // namespace ph
} // namespace Raven
