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

// Broad Phaseの内部状態を既存のLines Pipelineで可視化します。
// B: AABB表示、P: Broad Phase候補ペア表示。
// SceneGameの描画コードへ物理デバッグ処理を混ぜないため、インスタンスは
// Renderer::EndScene()からRenderRegistered()経由で描画されます。
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
        const math::Vec3& color
    );

    static void AddAABB(
        std::vector<DebugVertex>& vertices,
        std::vector<uint32_t>& indices,
        const AABB& b,
        const math::Vec3& color
    );
    

private:
    Scene* m_Scene = nullptr;
    const math::Mat4* m_View = nullptr;
    const math::Mat4* m_Projection = nullptr;
    BroadPhase m_BroadPhase;
    Ref<Material> m_Material;
    bool m_DrawAABBs = false;
    bool m_DrawPairs = false;
    bool m_WasAABBKeyPressed = false;
    bool m_WasPairKeyPressed = false;

}; // class PhysicsDebugRenderer

} // namespace ph

} // namespace Raven
