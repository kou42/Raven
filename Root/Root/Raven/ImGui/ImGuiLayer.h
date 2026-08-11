#pragma once

#include "Raven/Renderer/Layer/Layer.h"

namespace Raven
{
class Window;

// ============================================================================
// ImGuiLayer
// ============================================================================
// Dear ImGui本体とRavenの境界を担当する特殊なLayerです。
// Layerを継承することでRaven既存のAttach/Detachライフサイクルへ統合しつつ、
// Begin/Endは全LayerのOnImGuiRender()を囲むframe境界としてApplicationから明示的に呼びます。
//
// 重要:
// このクラスはDear ImGui Context / GLFW backend / OpenGL backendの管理だけを担当します。
// Hierarchy / Inspector / Statistics等のEditor機能はEditorLayer/Panel側へ置き、
// backend層へEditor固有UIを混在させない構成にします。
class ImGuiLayer : public Layer
{
public:
    explicit ImGuiLayer(Window& window);
    ~ImGuiLayer() override;

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void OnAttach() override;
    void OnDetach() override;

    void Begin();
    void End();

private:
    Window* m_Window = nullptr;
    bool m_Initialized = false;
};

} // namespace Raven
