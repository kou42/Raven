#pragma once

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

// Physicsの3D可視化と画面左上のSolver Statistics Overlayを担当します。
// UIライブラリには依存せず、既存Lines PipelineだけでDebug文字も描画します。
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
