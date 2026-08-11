#pragma once

#include "Raven/Core/Base.h"

namespace Raven
{
class Window;

// ============================================================================
// ImGuiLayer
// ============================================================================
// Dear ImGui本体とRavenの境界を担当する薄い統合レイヤーです。
// SceneやRendererへImGui依存を漏らさず、Editor/Debug UIだけがこの層を利用する構成にします。
class ImGuiLayer
{
public:
    explicit ImGuiLayer(Window& window);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void BeginFrame();
    void EndFrame();

    // 最初の導入確認用Debug UIです。
    // 将来EditorLayer/StatisticsPanelを導入した際は、表示責務をPanel側へ移します。
    void RenderDebugStatistics(float deltaTime);

private:
    void Initialize();
    void Shutdown();

private:
    Window* m_Window = nullptr;
    bool m_Initialized = false;
};

} // namespace Raven
