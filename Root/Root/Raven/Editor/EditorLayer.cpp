#include "Raven/Editor/EditorLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/Scene/Scene.h"

#include <imgui.h>

namespace Raven
{

EditorLayer::EditorLayer(Application& application)
    : m_Application(&application)
{
}

void EditorLayer::OnAttach()
{
    // 各PanelはEditorLayerが所有します。
    // 現在のPanelは追加初期化不要ですが、Panel固有リソースが必要になった場合も
    // ApplicationではなくEditorLayerのライフサイクルから管理します。
}

void EditorLayer::OnDetach()
{
    // 現段階ではEditorLayer自身が明示的に解放するリソースはありません。
    // 将来Framebuffer / Editor Camera用GPU Resource / Panel固有Resource等を所有した場合は、
    // ImGui ContextとWindowがまだ有効なこのタイミングで破棄します。
}

void EditorLayer::OnUpdate(float dt)
{
    (void)dt;

    // Scene View用Editor CameraやEditor独自の更新処理はRuntime Scene更新と分離してここへ追加します。
}

void EditorLayer::OnRender()
{
    // GizmoやEditor用debug primitive等、通常Renderer経由で描画する処理の入口です。
}

void EditorLayer::OnImGuiRender(float dt)
{
    if (m_Application == nullptr)
    {
        return;
    }

    // ========================================================================
    // Editor selection validation
    // ========================================================================
    // Hierarchyが非表示でもRuntime側ではEntityが破棄されたりSceneが差し替わる可能性があります。
    // 選択状態はEditor全体で共有するため、Panel描画前に毎frame検証して無効参照を残しません。
    ValidateSelectedEntity();

    // ========================================================================
    // Editor root window / DockSpace
    // ========================================================================
    // 各Panelを描画する前にDockSpaceを作ります。
    // Dear ImGuiのDockingでは、Dock可能Windowより先にDockSpace Nodeが存在している必要があるため、
    // Editor frameの先頭でHost Windowを構築します。
    BeginDockSpace();

    // EditorLayerはPanelとApplicationの橋渡しだけを行います。
    // SceneをPanel内へ永続保持しないため、将来SetScene()でSceneを切り替えても
    // 常にそのframe時点のActive Sceneを表示できます。
    if (m_ShowStatisticsPanel)
    {
        m_StatisticsPanel.OnImGuiRender(
            dt,
            m_Application->GetWindow(),
            m_Application->GetScene());
    }

    // Animation Debugも同じActive Sceneを参照します。
    // AnimationDebugPanel自身はSceneやAnimatorのLifetimeを所有せず、このframeで必要な
    // Runtime Snapshotだけを構築するため、Scene切り替え時にも古い参照を残しません。
    if (m_ShowAnimationDebugPanel)
    {
        m_AnimationDebugPanel.OnImGuiRender(m_Application->GetScene());
    }

    // ========================================================================
    // Scene Hierarchy
    // ========================================================================
    // HierarchyはActive SceneのEntity一覧を表示し、EditorLayerが所有する選択Entityだけを更新します。
    // 選択状態をPanelの外へ置くことで、次のInspectorや将来のGizmoが同じEntityを利用できます。
    if (m_ShowSceneHierarchyPanel)
    {
        m_SceneHierarchyPanel.OnImGuiRender(
            m_Application->GetScene(),
            m_SelectedEntity);
    }

    EndDockSpace();
}

void EditorLayer::OnEvent(Event& event)
{
    (void)event;

    // Editor Camera操作やGizmo操作などの入力は今後ここで処理します。
    // Editor側で入力を消費した場合はevent.Handled = trueとしてRuntime側への伝播を止めます。
}

void EditorLayer::ValidateSelectedEntity()
{
    if (m_Application == nullptr)
    {
        m_SelectedEntity = Entity{};
        return;
    }

    Scene* activeScene = m_Application->GetScene();
    if (activeScene == nullptr)
    {
        m_SelectedEntity = Entity{};
        return;
    }

    if (m_SelectedEntity == false)
    {
        return;
    }

    // Entity自身が保持するSceneと現在のActive Sceneを明示比較します。
    // 同じIndex/GenerationのEntityが別Sceneに存在しても、それはEditor上では別Entityです。
    if (m_SelectedEntity.GetScene() != activeScene)
    {
        m_SelectedEntity = Entity{};
        return;
    }

    // Generationまで含めた生存確認を行います。
    // Destroy後に同じIndexが再利用されても旧選択Entityはここで無効化されます。
    if (activeScene->IsEntityAlive(m_SelectedEntity) == false)
    {
        m_SelectedEntity = Entity{};
    }
}

void EditorLayer::BeginDockSpace()
{
    // ========================================================================
    // Fullscreen DockSpace Host Window
    // ========================================================================
    // Main Viewport全体を覆う「見えない親Window」を作ります。
    // このWindow自体をEditor Panelとして見せるのではなく、MenuBarとDockSpaceを保持する
    // Root Containerとして利用します。
    //
    // 重要:
    // 現段階ではScene/Game View用Framebufferをまだ導入しておらず、ゲーム画面はMain
    // Framebufferへ直接描画されています。そのためHost Windowが通常のWindow背景を描くと、
    // Scene描画後にImGuiの背景矩形が重なり、ゲーム画面全体を覆い隠してしまいます。
    // Step 7でScene View / Game ViewをFramebuffer Textureとして表示するまでは、Host Windowの
    // 背景を描画せず、既存ゲーム画面をDockSpaceの背後から見える状態に保ちます。
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return;
    }

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // Host WindowはMain Viewportと完全に一体化させるため、通常Windowにある
    // TitleBar / Resize / Move / Collapse等の操作を無効にします。
    // Dockされた子Window側は通常のImGui Windowなので、ユーザーは自由に移動・分割できます。
    ImGuiWindowFlags windowFlags = 0;
    windowFlags |= ImGuiWindowFlags_MenuBar;
    windowFlags |= ImGuiWindowFlags_NoDocking;
    windowFlags |= ImGuiWindowFlags_NoTitleBar;
    windowFlags |= ImGuiWindowFlags_NoCollapse;
    windowFlags |= ImGuiWindowFlags_NoResize;
    windowFlags |= ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    windowFlags |= ImGuiWindowFlags_NoNavFocus;

    // Main framebufferへ直接描かれているゲーム画面をHost Window背景で隠さないための設定です。
    // NoBackgroundはWindowの背景だけを無効化し、MenuBarやDockされたPanelは通常どおり描画されます。
    // Scene/Game ViewをFramebuffer化した後は、Editor背景を明示的に描く構成へ変更できます。
    windowFlags |= ImGuiWindowFlags_NoBackground;

    // Main Viewportを余白なしでDock領域として使うため、Host WindowだけWindowPaddingを0にします。
    // Push/Popを必ず対にし、他PanelのStyleへ影響を残さないことが重要です。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("RavenEditorDockSpaceHost", nullptr, windowFlags);
    m_DockSpaceBegun = true;

    ImGui::PopStyleVar(3);

    // ========================================================================
    // DockSpace Node
    // ========================================================================
    // GetID()で安定したDockSpace IDを生成します。
    // ini保存が有効な場合、Dear ImGuiはこのIDを基準にDock Layoutを保存・復元します。
    const ImGuiID dockSpaceId = ImGui::GetID("RavenEditorDockSpace");

    // PassthruCentralNodeを指定すると、Dockされていない中央領域についてDockSpace自身も
    // 背景を描画しません。Host WindowのNoBackgroundと組み合わせることで、中央領域から
    // 既存のMain framebuffer（現在のゲーム画面）がそのまま見えるようになります。
    ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockSpace(
        dockSpaceId,
        ImVec2(0.0f, 0.0f),
        dockSpaceFlags);

    // MenuBarもDockSpace Hostに持たせます。
    // これにより各Panelが個別にMenuを持つのではなく、Editor全体の操作を一箇所へ集約できます。
    RenderMenuBar();
}

void EditorLayer::EndDockSpace()
{
    if (m_DockSpaceBegun == false)
    {
        return;
    }

    // ImGui::Begin()の戻り値に関係なくBeginを呼んだらEndが必要です。
    // DockSpace Hostは常時表示するため、毎frame必ずここで閉じます。
    ImGui::End();
    m_DockSpaceBegun = false;
}

void EditorLayer::RenderMenuBar()
{
    if (ImGui::BeginMenuBar() == false)
    {
        return;
    }

    // ========================================================================
    // View menu
    // ========================================================================
    // Editor Panelの表示状態はEditorLayer側で一元管理します。
    // Panelを増やしても各Panel同士を依存させず、このMenuから表示状態だけを切り替えます。
    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Statistics", nullptr, &m_ShowStatisticsPanel);
        ImGui::MenuItem("Animation Debug", nullptr, &m_ShowAnimationDebugPanel);
        ImGui::MenuItem("Scene Hierarchy", nullptr, &m_ShowSceneHierarchyPanel);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

} // namespace Raven
