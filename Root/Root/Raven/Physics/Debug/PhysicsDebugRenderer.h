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
    // Sceneへの参照だけを保持します。
    // 3D Debug描画に必要なView/ProjectionはRenderer::BeginScene(camera)で確定した
    // Renderer Camera Contextから描画時に取得するため、Scene固有のCamera行列を保持しません。
    explicit PhysicsDebugRenderer(Scene& scene);

    ~PhysicsDebugRenderer();
    PhysicsDebugRenderer(const PhysicsDebugRenderer&) = delete;
    PhysicsDebugRenderer& operator=(const PhysicsDebugRenderer&) = delete;

    // 登録済みインスタンスをまとめて描画します。
    static void RenderRegistered();
    // 同じSceneにぶら下がるRendererへPhysicsWorld参照を配布します。
    static void BindPhysicsWorld(Scene& scene, const PhysicsWorld& physicsWorld);

    // ========================================================================
    // Bound PhysicsWorld access
    // ========================================================================
    // 現在のSceneはPhysicsWorldをprivate所有しているため、SceneGameからRayCast等の
    // Physics query APIへ直接到達できません。PhysicsDebugRendererはScene::OnUpdatePhysics
    // から実際にStepされたWorldを既にBindされているため、マウス操作などScene側の
    // デバッグ/インタラクション用途に限ってそのWorldを返します。
    //
    // m_PhysicsWorldは描画用途ではconst参照ですが、返却先ではPhysicsWorldの正式な
    // AddImpulseAtPoint/WakeUp等の制御APIだけを使用します。将来的にはScene自身に
    // GetPhysicsWorld()/RayCast wrapperを設け、この橋渡しを削除するのが望ましいです。
    PhysicsWorld* GetBoundPhysicsWorld() const
    {
        return const_cast<PhysicsWorld*>(m_PhysicsWorld);
    }

    PhysicsDebugSettings& GetSettings() { return m_Settings; }
    const PhysicsDebugSettings& GetSettings() const { return m_Settings; }

private:
    // 線描画専用頂点。TexcoordはOverlayフォント描画の将来拡張用に保持します。
    struct DebugVertex { math::Vec3 Position{}; math::Vec3 Color{1,1,1}; math::Vec2 Texcoord{}; };

    // Renderer::EndScene()から描画できるよう、生成済みPhysicsDebugRendererを登録します。
    // 所有権はRegistryへ移さず、各Rendererのconstructor/destructorで登録・解除します。
    static std::vector<PhysicsDebugRenderer*>& Registry();

    // 初回描画時にPipeline/Materialを遅延生成します。
    void EnsureInitialized();
    // キー入力の立ち上がりのみで表示フラグを反転します。
    void UpdateToggleKeys();
    // 有効なDebug表示だけを判定し、3D World Debugと2D Overlayへ振り分けます。
    void Render();
    // 3Dワイヤ表示群（AABB/OBB/Tree/Contact）を収集して描画します。
    void RenderWorldDebug();
    // 2D OverlayにSolver統計と表示設定を描画します。
    void RenderOverlay();

    // CPU側で収集したDebug Lineを一時Vertex/Index Bufferへ変換して描画します。
    // 3D DebugではCamera ContextのView/Projection、OverlayではIdentityを渡します。
    void SubmitLines(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const math::Mat4& view, const math::Mat4& projection);

    // 以下はDebug表示用の線分データをCPU側で構築するHelper群です。
    static void AddLine(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const math::Vec3& a, const math::Vec3& b, const math::Vec3& color);
    static void AddAABB(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const AABB& bounds, const math::Vec3& color);
    static void AddOBB(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const OBB& bounds, const math::Vec3& color);
    static void AddPointMarker(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const math::Vec3& position, float radius, const math::Vec3& color);

    // 5x7 Debug FontをLine Primitiveへ展開し、NDC座標でOverlay文字列を構築します。
    static void AddOverlayText(std::vector<DebugVertex>& vertices, std::vector<uint32_t>& indices,
        const std::string& text, float pixelX, float pixelY, float pixelScale,
        int viewportWidth, int viewportHeight, const math::Vec3& color);

private:
    // Scene / PhysicsWorldは所有せず参照のみです。
    // Sceneはconstructorで設定し、PhysicsWorldはScene::OnUpdatePhysics経由のBindPhysicsWorld()で
    // 実際にStepされているWorldへ接続されます。呼び出し側が両者の寿命を管理します。
    Scene* m_Scene = nullptr;
    const PhysicsWorld* m_PhysicsWorld = nullptr;

    // Debug Line描画用Materialと表示設定です。
    // MaterialはEnsureInitialized()で必要になった時点に遅延生成します。
    Ref<Material> m_Material;
    PhysicsDebugSettings m_Settings{};

    // 各キーの前frame状態を保持し、押しっぱなしで毎frameトグルされることを防ぎます。
    bool m_WasOverlayKeyPressed=false, m_WasAABBKeyPressed=false, m_WasOBBKeyPressed=false;
    bool m_WasFatAABBKeyPressed=false, m_WasTreeKeyPressed=false, m_WasPairKeyPressed=false;
    bool m_WasContactPointKeyPressed=false, m_WasContactNormalKeyPressed=false;
};
} // namespace ph
} // namespace Raven
