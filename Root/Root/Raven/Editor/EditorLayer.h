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
class Material;

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
    // Scene Viewで現在使用するTransform操作です。
    // EditorLayerがモードだけを管理し、Translate/Rotateの具体的な描画・Drag計算は
    // それぞれのEditor専用実装へ分離します。
    enum class GizmoOperation
    {
        Translate = 0,
        Rotate
    };

    void BeginDockSpace();
    void EndDockSpace();
    void RenderMenuBar();
    void ValidateSelectedEntity();

    void RenderSceneView();
    void RenderGameView();
    void RenderSceneToFramebuffer(Framebuffer& framebuffer);
    void RenderSceneToFramebuffer(
        Framebuffer& framebuffer,
        const Camera& camera);
    void RenderEntityPickingPass(
        Framebuffer& framebuffer,
        const Camera& camera);
    void RenderSelectionOutlinePass(
        Framebuffer& framebuffer,
        const Camera& camera);

private:
    // ApplicationはEditorLayerより長生きするため非所有ポインタとして保持します。
    Application* m_Application = nullptr;

    // Hierarchy / Inspector / Scene View / Gizmoが共有する現在選択中のEntityです。
    Entity m_SelectedEntity{};

    StatisticsPanel m_StatisticsPanel;
    AnimationDebugPanel m_AnimationDebugPanel;
    SceneHierarchyPanel m_SceneHierarchyPanel;
    InspectorPanel m_InspectorPanel;

    // Scene View専用Cameraです。Runtime Cameraとは独立しています。
    EditorCamera m_EditorCamera;
    bool m_SceneViewportHovered = false;
    bool m_SceneViewportFocused = false;

    // 初期状態は従来どおりTranslateにして、既存操作感を変更しません。
    // RotateはMenuBarのGizmoメニューから切り替えます。
    GizmoOperation m_GizmoOperation = GizmoOperation::Translate;

    std::unique_ptr<Framebuffer> m_SceneFramebuffer;
    std::unique_ptr<Framebuffer> m_GameFramebuffer;

    Ref<Material> m_EntityPickingMaterial;
    Ref<Material> m_SelectionOutlineMaterial;

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
