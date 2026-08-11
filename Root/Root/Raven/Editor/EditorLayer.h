#pragma once

#include "Raven/Editor/Panels/AnimationDebugPanel.h"
#include "Raven/Editor/Panels/InspectorPanel.h"
#include "Raven/Editor/Panels/SceneHierarchyPanel.h"
#include "Raven/Editor/Panels/StatisticsPanel.h"
#include "Raven/Renderer/Framebuffer.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Entity.h"

#include <memory>

namespace Raven
{
class Application;

// ============================================================================
// EditorLayer
// ============================================================================
// Raven Editor全体の入口となるLayerです。
// ApplicationはRuntimeのライフサイクルを担当し、Editor固有UI・選択状態・Viewportは
// このLayer以下へ分離します。
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
    void BeginDockSpace();
    void EndDockSpace();
    void RenderMenuBar();
    void ValidateSelectedEntity();

    // Scene/Game Viewは独立したImGui Windowとして扱います。
    // WindowのContentRegionサイズを保存し、次のOnRender()でFramebufferを同じ大きさへResizeします。
    void RenderSceneView();
    void RenderGameView();
    void RenderSceneToFramebuffer(Framebuffer& framebuffer);

private:
    // ApplicationはEditorLayerより長生きするため非所有ポインタとして保持します。
    Application* m_Application = nullptr;

    // Hierarchy / Inspector / 将来のGizmoが共有する現在選択中のEntityです。
    Entity m_SelectedEntity{};

    StatisticsPanel m_StatisticsPanel;
    AnimationDebugPanel m_AnimationDebugPanel;
    SceneHierarchyPanel m_SceneHierarchyPanel;
    InspectorPanel m_InspectorPanel;

    // ========================================================================
    // Editor Viewports
    // ========================================================================
    // Scene ViewとGame Viewを最初から別Framebufferにします。
    // 現段階では両方ともRuntime SceneのCameraで描画しますが、Framebufferを分離しておくことで
    // 次段階ではScene ViewだけEditor Camera / Grid / Gizmoを重ねられます。
    std::unique_ptr<Framebuffer> m_SceneFramebuffer;
    std::unique_ptr<Framebuffer> m_GameFramebuffer;
    float m_SceneViewportWidth = 1280.0f;
    float m_SceneViewportHeight = 720.0f;
    float m_GameViewportWidth = 1280.0f;
    float m_GameViewportHeight = 720.0f;

    bool m_ShowStatisticsPanel = true;
    bool m_ShowAnimationDebugPanel = true;
    bool m_ShowSceneHierarchyPanel = true;
    bool m_ShowInspectorPanel = true;
    bool m_ShowSceneView = true;
    bool m_ShowGameView = true;

    bool m_DockSpaceBegun = false;
};

} // namespace Raven
