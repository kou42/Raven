#pragma once

#include "Raven/Editor/Panels/StatisticsPanel.h"
#include "Raven/Renderer/Layer/Layer.h"

namespace Raven
{
class Application;

// ============================================================================
// EditorLayer
// ============================================================================
// Raven Editor全体の入口となるLayerです。
// EditorLayer自身へ各Panelの描画詳細を詰め込まず、Panelを束ねてRuntime状態との橋渡しをします。
class EditorLayer : public Layer
{
public:
    explicit EditorLayer(Application& application);
    ~EditorLayer() override = default;

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float dt) override;
    void OnRender() override;
    void OnImGuiRender(float dt) override;
    void OnEvent(Event& event) override;

private:
    // ApplicationはEditorLayerより長生きするため非所有ポインタとして保持します。
    // Scene切り替え後もApplication::GetScene()を毎frame参照することで、古いSceneポインタを
    // Panel側へ保持し続けることを避けます。
    Application* m_Application = nullptr;
    StatisticsPanel m_StatisticsPanel;
};

} // namespace Raven
