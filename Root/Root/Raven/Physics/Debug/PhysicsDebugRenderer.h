#pragma once

#include <string>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Math/MathMatrix.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Collision/AABB.h"
#include "Raven/Physics/Collision/OBB.h"
#include "Raven/Physics/Debug/PhysicsDebugSettings.h"

namespace Raven
{
class Scene;
namespace ph
{
class PhysicsWorld;

// Physicsの3D可視化と画面左上のSolver Statistics Overlayを担当します。
// Keyboard: H=Overlay, B=AABB, O=OBB, F=Fat AABB, T=Tree, P=Pair, C=Contact, N=Normal
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
    struct DebugVertex { math::Vec3 Position{}; math::Vec3 Color{1,1,1}; math::Vec2 Texcoord{}; };
    static std::vector<PhysicsDebugRenderer*>& Registry();
    void EnsureInitialized();
    void UpdateToggleKeys();
    void Render();
    void RenderWorldDebug();
    void RenderOverlay();
    void SubmitLines(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const math::Mat4& view, const math::Mat4& projection);
    static void AddLine(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const math::Vec3& a, const math::Vec3& b, const math::Vec3& color);
    static void AddAABB(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const AABB& bounds, const math::Vec3& color);
    static void AddOBB(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const OBB& bounds, const math::Vec3& color);
    static void AddPointMarker(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const math::Vec3& position, float radius, const math::Vec3& color);
    static void AddOverlayText(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const std::string& text, float pixelX, float pixelY, float pixelScale,
        int viewportWidth, int viewportHeight, const math::Vec3& color);

private:
    Scene* m_Scene=nullptr; const PhysicsWorld* m_PhysicsWorld=nullptr;
    const math::Mat4* m_View=nullptr; const math::Mat4* m_Projection=nullptr;
    Ref<Material> m_Material; PhysicsDebugSettings m_Settings{};
    bool m_WasOverlayKeyPressed=false, m_WasAABBKeyPressed=false, m_WasOBBKeyPressed=false;
    bool m_WasFatAABBKeyPressed=false, m_WasTreeKeyPressed=false, m_WasPairKeyPressed=false;
    bool m_WasContactPointKeyPressed=false, m_WasContactNormalKeyPressed=false;
};
} // namespace ph
} // namespace Raven
