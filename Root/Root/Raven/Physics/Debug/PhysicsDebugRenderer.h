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

class PhysicsWorld;

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
//   C : Contact Point
//   N : Contact Normal
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

    // Sceneが実際のシミュレーションで使用しているPhysicsWorldをDebugRendererへ関連付けます。
    // PhysicsDebugRenderer側で別のWorldを再構築せず、同じTree / Contact情報を読むための接続口です。
    static void BindPhysicsWorld(Scene& scene, const PhysicsWorld& physicsWorld);

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

    static void AddPointMarker(
        std::vector<DebugVertex>& vertices,
        std::vector<uint32_t>& indices,
        const math::Vec3& position,
        float radius,
        const math::Vec3& color);

private:
    Scene* m_Scene = nullptr;
    const PhysicsWorld* m_PhysicsWorld = nullptr;
    const math::Mat4* m_View = nullptr;
    const math::Mat4* m_Projection = nullptr;

    // Broad Phase Pair表示だけは既存機能を維持するためDebug専用BroadPhaseを残します。
    // Fat AABB / Dynamic Tree / Contactは必ずm_PhysicsWorld側の実データを参照します。
    // Pairも将来PhysicsWorld側へSnapshotを持たせれば完全に同一Stepへ統一できます。
    BroadPhase m_PairDebugBroadPhase;

    Ref<Material> m_Material;
    PhysicsDebugSettings m_Settings{};

    bool m_WasAABBKeyPressed = false;
    bool m_WasFatAABBKeyPressed = false;
    bool m_WasTreeKeyPressed = false;
    bool m_WasPairKeyPressed = false;
    bool m_WasContactPointKeyPressed = false;
    bool m_WasContactNormalKeyPressed = false;
};

} // namespace ph
} // namespace Raven
