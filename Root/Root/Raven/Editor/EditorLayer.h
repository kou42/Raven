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
    // Applicationそのものを所有するのではなく参照だけ受け取ります。
    // EditorはApplicationが管理するWindow / Active Scene等を参照する必要がありますが、
    // それらのLifetime管理は引き続きApplication側の責務です。
    explicit EditorLayer(Application& application);
    ~EditorLayer() override = default;

    // Editor専用リソースやPanelの初期化入口です。
    // ImGui Contextの生成はImGuiLayerの責務なので、ここでは行いません。
    // 現在はScene View / Game View用Framebufferもここで生成します。
    void OnAttach() override;

    // Editor専用リソースやPanelの終了処理入口です。
    // Applicationの破棄順序により、この時点ではGraphics Contextがまだ有効なので、
    // Framebuffer等のGPU Resourceもここで安全に解放します。
    void OnDetach() override;

    // Editor Cameraや将来のGizmo状態など、Runtime Sceneとは独立した更新を行います。
    // 現在はScene Viewがhoverされている間だけEditorCameraへ入力を渡します。
    void OnUpdate(float dt) override;

    // ImGuiそのものではなく、Editorが必要とする3D描画の入口です。
    // 現在はScene/Game View用Framebufferへのoff-screen描画を担当し、将来的には
    // Scene View専用のGrid / Selection Outline / Gizmo等もこの描画経路へ追加します。
    void OnRender() override;

    // Dear ImGuiのBegin/End間でApplicationから呼ばれるEditor UI描画入口です。
    // DockSpace、Scene/Game View、各Panelの描画を最終的にここから統括します。
    void OnImGuiRender(float dt) override;

    // Editor Camera / Gizmo / Shortcut等の入力処理入口です。
    // EditorがEventを消費した場合はevent.Handledを設定し、Runtime側への伝播を止めます。
    // Scene Viewのhover/focus状態と組み合わせた入力ルーティングを今後ここへ追加します。
    void OnEvent(Event& event) override;

private:
    // ========================================================================
    // Editor Root UI
    // ========================================================================
    // Main Viewport全体を覆うHost Windowを作り、その内部にDockSpaceを配置します。
    // Host Window自身はEditor背景/メニューバー/Docking領域の管理だけを担当し、
    // StatisticsやScene View等の実際のEditor Windowは独立したWindowとしてDockされます。
    void BeginDockSpace();
    void EndDockSpace();

    // Editor全体のMenuBar描画です。
    // Viewメニューから各Editor PanelおよびScene/Game Viewの表示/非表示を切り替えます。
    // Window数が増えても表示状態の管理はここへ集約し、Panel自身には他Panelの存在を知らせません。
    void RenderMenuBar();

    // 選択Entityが現在のActive Sceneに属し、かつGenerationまで含めて生存しているかを検証します。
    // Runtime側でDestroyされた場合やApplication::SetScene()でSceneが差し替わった場合に、
    // Inspector/Gizmoが古いEntityを参照し続けないためのEditor共通ガードです。
    void ValidateSelectedEntity();

    // ========================================================================
    // Editor Viewports
    // ========================================================================
    // Scene View / Game ViewはMain framebufferを直接透過表示するのではなく、
    // それぞれ専用FramebufferのColor Attachment TextureをImGui Window内へ表示します。
    //
    // WindowのContentRegionサイズを保存し、次のOnRender()でFramebufferを同じ大きさへ
    // ResizeすることでDock/Window Resizeへ追従します。
    void RenderSceneView();
    void RenderGameView();

    // Runtime Cameraをそのまま利用してActive Sceneを指定Framebufferへ描画します。
    // Game Viewではこの経路を使用します。
    void RenderSceneToFramebuffer(Framebuffer& framebuffer);

    // 指定Cameraを利用してActive SceneをFramebufferへ描画します。
    // Scene ViewではEditorCameraをCamera基底参照としてそのまま渡し、Runtime Cameraとは
    // 独立した視点で描画します。View/Projectionを個別引数として渡さないことで、
    // EditorLayer側も具体的なCamera内部表現へ依存しない構造にします。
    // SceneがSceneViewportRendererを実装していない場合は通常OnRender()へfallbackします。
    void RenderSceneToFramebuffer(
        Framebuffer& framebuffer,
        const Camera& camera);

    // Scene View専用のEntity Picking Passです。
    // 通常Scene描画後のDepth Bufferを再利用し、Color Attachment 1(R32I)だけへEntityIndexを書き込みます。
    // 既存Material/ShaderへPicking出力を追加しないため、ゲーム描画ShaderとEditor機能を分離できます。
    void RenderEntityPickingPass(
        Framebuffer& framebuffer,
        const Camera& camera);

private:
    // ========================================================================
    // Application reference
    // ========================================================================
    // ApplicationはEditorLayerより長生きするため非所有ポインタとして保持します。
    // Scene切り替え後もApplication::GetScene()を毎frame参照することで、古いSceneポインタを
    // Panel側へ保持し続けることを避けます。
    //
    // EditorLayer側でApplicationを所有しないことが重要です。
    // 所有関係は Application -> EditorLayer の一方向に保ち、循環所有を作りません。
    Application* m_Application = nullptr;

    // ========================================================================
    // Editor Selection
    // ========================================================================
    // Hierarchy / Inspector / Scene View / Gizmoが共有する「現在選択中のEntity」です。
    // EntityはIndex + Generation + Sceneを持つため、単なるEntityIndexだけを保存するよりも
    // Index再利用やScene切替に対して安全に選択状態を検証できます。
    //
    // 選択状態をSceneHierarchyPanel内部へ閉じ込めないことが重要です。
    // Inspectorはこの同じEntityを受け取り、さらにScene ViewのSelection表示やGizmoも
    // 同じ選択Entityを利用することでEditor全体の選択状態を一元化できます。
    Entity m_SelectedEntity{};

    // ========================================================================
    // Editor Panels
    // ========================================================================
    // PanelはEditorLayerが所有しますが、Active Scene等のRuntimeオブジェクトは所有しません。
    // 必要なRuntime参照をOnImGuiRender()時に渡すことでScene差し替えにも追従します。
    StatisticsPanel m_StatisticsPanel;
    AnimationDebugPanel m_AnimationDebugPanel;
    SceneHierarchyPanel m_SceneHierarchyPanel;
    InspectorPanel m_InspectorPanel;

    // ========================================================================
    // Editor Camera
    // ========================================================================
    // Scene View専用Cameraです。Runtime SceneのCamera状態とは分離しているため、Editorで視点を
    // 移動・回転してもGame Viewやゲームロジック側のCameraには影響しません。
    // Scene描画時にはCamera基底参照としてSceneViewportRendererへ渡します。
    EditorCamera m_EditorCamera;

    // RenderSceneView()で得たImGuiのhover/focus状態です。
    // OnUpdate()はImGui frame構築より先に呼ばれるため、前frameの状態をCamera入力判定に使います。
    // 1frame遅延はありますが、Window境界で入力を誤ってRuntime側へ渡すより安全な構成です。
    bool m_SceneViewportHovered = false;
    bool m_SceneViewportFocused = false;

    // ========================================================================
    // Editor Viewport resources
    // ========================================================================
    // Scene ViewとGame Viewを最初から別Framebufferにします。
    // Scene ViewはEditor Camera、Game ViewはRuntime Cameraで描画し、さらに今後Scene Viewだけに
    // Grid / Selection Outline / Gizmo等を追加できる構造にしています。
    //
    // std::unique_ptr<Framebuffer>として抽象クラスを所有し、生成はFramebuffer::Create()へ
    // 委譲します。そのためEditorLayerは現在のRendererAPIがOpenGLかDirectXかを知りません。
    std::unique_ptr<Framebuffer> m_SceneFramebuffer;
    std::unique_ptr<Framebuffer> m_GameFramebuffer;

    // Picking Pass専用Materialです。
    // ゲーム内Materialを変更せず、EditorだけがR32I AttachmentへEntityIndexを書き込むために使用します。
    Ref<Material> m_EntityPickingMaterial;

    // ImGui::GetContentRegionAvail()で取得した各Viewport Windowの表示領域です。
    // floatのままUI側の最新サイズを保持し、OnRender()でGPUへ渡す際にuint32_tへ変換します。
    // 初回Dock Layout確定前は1280x720を仮サイズとして使用します。
    float m_SceneViewportWidth = 1280.0f;
    float m_SceneViewportHeight = 720.0f;
    float m_GameViewportWidth = 1280.0f;
    float m_GameViewportHeight = 720.0f;

    // ========================================================================
    // Editor Window visibility
    // ========================================================================
    // Editor Windowの表示状態はEditorLayerが一元管理します。
    // Panel内部にEditor全体のWindow管理状態を持たせないことで、MenuBarや将来の
    // Workspace保存機能から一元的に表示状態を制御できるようにします。
    bool m_ShowStatisticsPanel = true;
    bool m_ShowAnimationDebugPanel = true;
    bool m_ShowSceneHierarchyPanel = true;
    bool m_ShowInspectorPanel = true;
    bool m_ShowSceneView = true;
    bool m_ShowGameView = true;

    // BeginDockSpace()でHost Windowに対してImGui::Begin()を呼んだ場合は、
    // 必ず対応するImGui::End()が必要です。Begin/Endの対応関係を明確に保つため状態を保持します。
    bool m_DockSpaceBegun = false;
};

} // namespace Raven