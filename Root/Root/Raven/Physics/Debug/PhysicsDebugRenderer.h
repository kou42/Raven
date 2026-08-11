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

class PhysicsDebugRenderer
{
public:
    explicit PhysicsDebugRenderer(Scene& scene);

    // SceneGameのm_View/m_Projection撤去を段階的に行うための互換Constructorです。
    // Renderer Camera Contextが有効な描画ではContextを優先し、未移行経路だけこの参照をfallback利用します。
    PhysicsDebugRenderer(Scene& scene, const math::Mat4& view, const math::Mat4& projection);

    ~PhysicsDebugRenderer();
    PhysicsDebugRenderer(const PhysicsDebugRenderer&) = delete;
    PhysicsDebugRenderer& operator=(const PhysicsDebugRenderer&) = delete;

    static void RenderRegistered();
    static void BindPhysicsWorld(Scene& scene, const PhysicsWorld& physicsWorld);

    PhysicsWorld* GetBoundPhysicsWorld() const
    {
        return const_cast<PhysicsWorld*>(m_PhysicsWorld);
    }

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
    Scene* m_Scene = nullptr;
    const PhysicsWorld* m_PhysicsWorld = nullptr;

    // Renderer Camera Context導入前の経路だけで利用するfallbackです。
    // SceneGameのCameraミラー撤去が完了した段階で削除します。
    const math::Mat4* m_FallbackView = nullptr;
    const math::Mat4* m_FallbackProjection = nullptr;

    Ref<Material> m_Material;
    PhysicsDebugSettings m_Settings{};
    bool m_WasOverlayKeyPressed=false, m_WasAABBKeyPressed=false, m_WasOBBKeyPressed=false;
    bool m_WasFatAABBKeyPressed=false, m_WasTreeKeyPressed=false, m_WasPairKeyPressed=false;
    bool m_WasContactPointKeyPressed=false, m_WasContactNormalKeyPressed=false;
};
} // namespace ph
} // namespace Raven
