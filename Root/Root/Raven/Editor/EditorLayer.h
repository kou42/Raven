#pragma once

#include "Raven/Editor/EditorCamera.h"
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
//
// ApplicationはWindow / Scene / Layer更新 / ImGui frame境界といった
// 「アプリケーションを動かすための基盤処理」だけを担当し、Editor固有のUIや操作は
// このLayer以下へ分離します。
//
// EditorLayer自身へ全UI処理を詰め込むのではなく、
// StatisticsPanel / AnimationDebugPanel / SceneHierarchyPanel / InspectorPanel等を
// 個別クラスとして追加し、EditorLayerはそれらを束ねる役割に留めます。
//
// 今後の責務イメージ:
//   EditorLayer
//     - Editor全体の更新・入力の入口
//     - Active SceneなどRuntime状態と各Panelの橋渡し
//     - DockSpace / MenuBarなどEditor全体UIの管理
//     - Entity選択状態の共有
//     - Scene View / Game Viewのoff-screen描画管理
//     - Editor Camera / Gizmo等のEditor専用機能の統括
//     - 各PanelのOnImGuiRender()呼び出し
//
// 一方、FPS表示やHierarchy描画など個々のUI詳細はPanel側へ置きます。
// Framebufferの具体的なOpenGL処理もPlatform/OpenGL側へ置き、EditorLayerは
// Renderer共通のFramebufferインターフェースだけを利用します。
// この境界を維持することで、Editor機能追加のたびにApplicationやEditorLayerが
// 巨大化したり、Editor層へ特定Graphics API依存が漏れることを避けます。
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

    void RenderSceneView();
    void RenderGameView();

    // Runtime Cameraをそのまま利用してActive Sceneを指定Framebufferへ描画します。
    // Game Viewではこの経路を使用します。
    void RenderSceneToFramebuffer(Framebuffer& framebuffer);

    // 指定Cameraを利用してActive SceneをFramebufferへ描画します。
    // Scene ViewではEditorCameraをCamera基底参照としてそのまま渡し、Renderer側へ
    // Editor固有型やView/Projectionの個別引数を漏らさない構造にします。
    // SceneがSceneViewportRendererを実装していない場合は通常OnRender()へfallbackします。
    void RenderSceneToFramebuffer(
        Framebuffer& framebuffer,
        const Camera& camera);

private:
    Application* m_Application = nullptr;

    Entity m_SelectedEntity{};

    StatisticsPanel m_StatisticsPanel;
    AnimationDebugPanel m_AnimationDebugPanel;
    SceneHierarchyPanel m_SceneHierarchyPanel;
    InspectorPanel m_InspectorPanel;

    // ========================================================================
    // Editor Camera
    // ========================================================================
    // Scene View専用Cameraです。Runtime SceneのCamera状態とは分離しているため、Editorで視点を
    // 移動・回転してもGame Viewやゲームロジック側のCameraには影響しません。
    // Scene描画時はCamera基底参照としてSceneViewportRendererへ渡します。
    EditorCamera m_EditorCamera;

    bool m_SceneViewportHovered = false;
    bool m_SceneViewportFocused = false;

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
