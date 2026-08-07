#pragma once

#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/BroadPhase.h"
#include "Raven/Physics/Debug/PhysicsDebugSettings.h"

namespace Raven
{

class Scene;

namespace ph
{

// ============================================================================
// PhysicsDebugRenderer
// ============================================================================
// Physicsの内部状態を既存Lines Pipelineで可視化します。
//
// 現在のKeyboard Toggle:
//   B : Colliderのtight AABB
//   F : Dynamic Tree LeafのFat AABB
//   T : Dynamic Tree Branch AABB
//   P : Broad Phase候補Pair
//
// 表示状態をPhysicsDebugSettingsへ集約しているため、将来Dear ImGuiを導入した際は
// Checkboxから同じSettingsを書き換えるだけでKeyboard/UI双方を共存できます。
class PhysicsDebugRenderer
{
public:
    PhysicsDebugRenderer(Scene& scene, const math::Mat4& view, const math::Mat4& projection);
    ~PhysicsDebugRenderer();

    PhysicsDebugRenderer(const PhysicsDebugRenderer&) = delete;
    PhysicsDebugRenderer& operator=(const PhysicsDebugRenderer&) = delete;

    static void RenderRegistered();

    PhysicsDebugSettings& GetSettings() { return m_Settings; }
    const PhysicsDebugSettings& GetSettings() const { return m_Settings; }

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

    // TODO(Physics Debug Overlay):
    // 現行実装ではDebugRenderer自身がBroadPhaseを同期しています。
    // Contact Point/NormalとSolver Statisticsを追加する段階で、Sceneから
    // PhysicsWorldの読み取り専用参照を取得できるAPIを追加し、実際のSimulation Treeを
    // 直接参照する構造へ移行します。
    BroadPhase m_BroadPhase;

    Ref<Material> m_Material;
    PhysicsDebugSettings m_Settings{};

    bool m_WasAABBKeyPressed = false;
    bool m_WasFatAABBKeyPressed = false;
    bool m_WasTreeKeyPressed = false;
    bool m_WasPairKeyPressed = false;
};

} // namespace ph
} // namespace Raven
