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
//     - 各PanelのOnImGuiRender()呼び出し
//
// 一方、FPS表示やHierarchy描画など個々のUI詳細はPanel側へ置きます。
// この境界を維持することで、Editor機能追加のたびにApplicationやEditorLayerが
// 巨大化することを避けます。
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
    void OnAttach() override;

    // Editor専用リソースやPanelの終了処理入口です。
    // Applicationの破棄順序により、OnDetach()中はImGui Contextがまだ有効です。
    void OnDetach() override;

    // Editor Cameraや将来のGizmo状態など、Runtime Sceneとは独立した更新を行います。
    void OnUpdate(float dt) override;

    // ImGui以外のEditor用描画が必要になった場合の入口です。
    // 将来的なGizmo / Editor debug primitive等を想定しています。
    void OnRender() override;

    // Dear ImGuiのBegin/End間でApplicationから呼ばれるEditor UI描画入口です。
    // DockSpaceと各Panelの描画は最終的にここから統括します。
    void OnImGuiRender(float dt) override;

    // Editor Camera / Gizmo / Shortcut等の入力処理入口です。
    // EditorがEventを消費した場合はevent.Handledを設定し、Runtime側への伝播を止めます。
    void OnEvent(Event& event) override;

private:
    // ========================================================================
    // Editor Root UI
    // ========================================================================
    // Main Viewport全体を覆うHost Windowを作り、その内部にDockSpaceを配置します。
    // Host Window自身はEditor背景/メニューバー/Docking領域の管理だけを担当し、
    // Statistics等の実際のEditor Windowは独立したPanelとしてDockSpaceへDockされます。
    void BeginDockSpace();
    void EndDockSpace();

    // Editor全体のMenuBar描画です。
    // 現段階ではViewメニューからStatistics Panelの表示/非表示を切り替えます。
    // 今後Hierarchy / Inspector / Animation Debug等も同じWindowメニューへ追加できます。
    void RenderMenuBar();

private:
    // ApplicationはEditorLayerより長生きするため非所有ポインタとして保持します。
    // Scene切り替え後もApplication::GetScene()を毎frame参照することで、古いSceneポインタを
    // Panel側へ保持し続けることを避けます。
    //
    // EditorLayer側でApplicationを所有しないことが重要です。
    // 所有関係は Application -> EditorLayer の一方向に保ち、循環所有を作りません。
    Application* m_Application = nullptr;

    // Debug / Statistics表示を独立Panelへ分離しています。
    // 今後のPanelも同じ方針でメンバとして追加し、EditorLayerは呼び出し順序と
    // Runtime状態の受け渡しだけを担当します。
    StatisticsPanel m_StatisticsPanel;

    // Panelの表示状態はEditorLayerが管理します。
    // Panel内部にEditor全体のWindow管理状態を持たせないことで、MenuBarや将来の
    // Workspace保存機能から一元的に表示状態を制御できるようにします。
    bool m_ShowStatisticsPanel = true;

    // BeginDockSpace()がHost WindowをBeginできたかに関係なく、ImGui::Begin()を呼んだ場合は
    // 必ず対応するImGui::End()が必要です。呼び出し構造を明確にするため状態を保持します。
    bool m_DockSpaceBegun = false;
};

} // namespace Raven
