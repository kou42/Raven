#pragma once

#include <string>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Collision/AABB.h"
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
// Physicsの3D可視化と画面左上のSolver Statistics Overlayを担当します。
//
// World DebugはSceneのView/Projectionを使用し、OverlayはIdentity行列 + NDC座標で
// 描画します。両者を分離することで、カメラ移動やProjection変更の影響をOverlayへ
// 持ち込まないようにしています。
class PhysicsDebugRenderer
{
public:
    PhysicsDebugRenderer(Scene& scene, const math::Mat4& view, const math::Mat4& projection);
    ~PhysicsDebugRenderer();

    PhysicsDebugRenderer(const PhysicsDebugRenderer&) = delete;
    PhysicsDebugRenderer& operator=(const PhysicsDebugRenderer&) = delete;

    static void RenderRegistered();
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
    void RenderWorldDebug();
    void RenderOverlay();

    // VertexBuffer / IndexBufferの既存APIが非const pointerを受け取るため、
    // 呼び出し元のローカルvectorも非const参照で受け取ります。
    // ここで実際にvectorの内容を書き換えることはありません。
    void SubmitLines(
        std::vector<DebugVertex>& vertices,
        std::vector<uint32_t>& indices,
        const math::Mat4& view,
        const math::Mat4& projection);

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

    // Debug用5x7 bitmap fontをNDC上のLine列へ変換します。
    static void AddOverlayText(
        std::vector<DebugVertex>& vertices,
        std::vector<uint32_t>& indices,
        const std::string& text,
        float pixelX,
        float pixelY,
        float pixelScale,
        int viewportWidth,
        int viewportHeight,
        const math::Vec3& color);

private:
    Scene* m_Scene = nullptr;
    const PhysicsWorld* m_PhysicsWorld = nullptr;
    const math::Mat4* m_View = nullptr;
    const math::Mat4* m_Projection = nullptr;

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
