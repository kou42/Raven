#pragma once

#include "Raven/Renderer/Layer/Layer.h"

namespace Raven
{
class Window;

// ============================================================================
// ImGuiLayer
// ============================================================================
// Dear ImGui本体とRavenの境界を担当する特殊なLayerです。
// Layerを継承することでRaven既存のAttach/Detach/UI描画ライフサイクルへ統合しつつ、
// Begin/Endは全LayerのOnImGuiRender()を囲むframe境界としてApplicationから明示的に呼びます。
//
// 将来EditorLayerを追加した後も、このクラスはDear ImGui backend管理だけを担当し、
// Hierarchy / Inspector / Statistics等のEditor機能はEditorLayer/Panel側へ分離します。
class ImGuiLayer : public Layer
{
public:
    explicit ImGuiLayer(Window& window);
    ~ImGuiLayer() override;

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void OnAttach() override;
    void OnDetach() override;
    void OnImGuiRender(float dt) override;

    void Begin();
    void End();

private:
    void RenderDebugStatistics(float deltaTime);

private:
    Window* m_Window = nullptr;
    bool m_Initialized = false;
};

} // namespace Raven
