// Raven/Animation/Debug/AnimationDebugOverlayRenderer.h
#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Raven
{
class Material;
class Scene;

// ============================================================================
// AnimationDebugOverlayRenderer
// ============================================================================
// 現在のAnimator State / Blend Tree Runtime情報を画面右上へ表示するbootstrap Debug UIです。
// Dear ImGui等のEditor基盤がまだ無い段階でもRuntime情報の正しさを目視確認できるようにし、
// 将来Editor導入時はAnimationRuntimeDebugのSnapshotをそのままPanel描画へ渡せる構成にします。
class AnimationDebugOverlayRenderer
{
public:
    explicit AnimationDebugOverlayRenderer(Scene& scene);
    ~AnimationDebugOverlayRenderer();

    AnimationDebugOverlayRenderer(const AnimationDebugOverlayRenderer&) = delete;
    AnimationDebugOverlayRenderer& operator=(const AnimationDebugOverlayRenderer&) = delete;

    // Renderer::EndScene()から登録済みOverlayをまとめて描画します。
    static void RenderRegistered();

    void SetVisible(bool visible) { m_Visible = visible; }
    bool IsVisible() const { return m_Visible; }

private:
    struct DebugVertex
    {
        math::Vec3 Position{};
        math::Vec3 Color{ 1.0f, 1.0f, 1.0f };
        math::Vec2 Texcoord{};
    };

    static std::vector<AnimationDebugOverlayRenderer*>& Registry();

    void EnsureInitialized();
    void UpdateToggleKey();
    void Render();

    void SubmitLines(
        std::vector<DebugVertex>& vertices,
        std::vector<uint32_t>& indices);

    static void AddLine(
        std::vector<DebugVertex>& vertices,
        std::vector<uint32_t>& indices,
        const math::Vec3& start,
        const math::Vec3& end,
        const math::Vec3& color);

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
    Ref<Material> m_Material;

    // YキーでAnimation OverlayをON/OFFします。
    bool m_Visible = true;
    bool m_WasToggleKeyPressed = false;
};

} // namespace Raven
