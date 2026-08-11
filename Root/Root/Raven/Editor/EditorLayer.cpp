#include "Raven/Editor/EditorLayer.h"

#include "Raven/Core/Application.h"

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
    // 現在のStatisticsPanelは追加初期化不要ですが、Panel固有リソースが必要になった場合も
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

    EndDockSpace();
}

void EditorLayer::OnEvent(Event& event)
{
    (void)event;

    // Editor Camera操作やGizmo操作などの入力は今後ここで処理します。
    // Editor側で入力を消費した場合はevent.Handled = trueとしてRuntime側への伝播を止めます。
}

void EditorLayer::BeginDockSpace()
{
    // ========================================================================
    // Fullscreen DockSpace Host Window
    // ========================================================================
    // Main Viewport全体を覆う「見えない親Window」を作ります。
    // このWindow自体をEditor Panelとして見せるのではなく、MenuBarとDockSpaceを保持する
    // Root Containerとして利用します。
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

    ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_None;
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
    // 今後Animation Debug / Scene Hierarchy / Inspector等を追加する場合も、このメニューへ
    // 同じ形式で追加すれば、Panelクラス側へWindow管理責務を持ち込まずに済みます。
    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Statistics", nullptr, &m_ShowStatisticsPanel);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

} // namespace Raven
