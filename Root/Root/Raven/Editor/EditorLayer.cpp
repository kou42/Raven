#include "Raven/Editor/EditorLayer.h"

#include "Raven/Core/Application.h"
#include "Raven/Scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>

namespace Raven
{

EditorLayer::EditorLayer(Application& application)
    : m_Application(&application)
{
}

void EditorLayer::OnAttach()
{
    // ========================================================================
    // Editor Viewport GPU resources
    // ========================================================================
    // 初期サイズは仮値です。実際のDock Layoutが確定した後、OnImGuiRender()で取得した
    // ContentRegionサイズへ次frameのOnRender()から追従します。
    m_SceneFramebuffer = std::make_unique<Framebuffer>(
        static_cast<std::uint32_t>(m_SceneViewportWidth),
        static_cast<std::uint32_t>(m_SceneViewportHeight));
    m_GameFramebuffer = std::make_unique<Framebuffer>(
        static_cast<std::uint32_t>(m_GameViewportWidth),
        static_cast<std::uint32_t>(m_GameViewportHeight));
}

void EditorLayer::OnDetach()
{
    // Applicationの破棄順序により、ここではOpenGL Contextがまだ有効です。
    // Framebufferのdestructor内でOpenGL Resourceを解放するため、Window破棄より先にresetします。
    m_SceneFramebuffer.reset();
    m_GameFramebuffer.reset();
}

void EditorLayer::OnUpdate(float dt)
{
    (void)dt;

    // Editor Cameraは次段階でここへ追加します。
    // Scene/Game ViewのFramebuffer ResizeはGPU Resource操作なのでOnRender()側で行います。
}

void EditorLayer::OnRender()
{
    if (m_Application == nullptr)
    {
        return;
    }

    Scene* activeScene = m_Application->GetScene();
    if (activeScene == nullptr)
    {
        return;
    }

    // ========================================================================
    // Off-screen Scene rendering
    // ========================================================================
    // Applicationが通常のRuntime SceneをMain Framebufferへ描画した後、Editor用Viewportへ
    // 同じSceneを再描画します。現段階では既存Sceneの描画責務を壊さず導入することを優先します。
    //
    // 次段階でEditor Cameraを導入するとScene ViewだけCameraを差し替えるため、最初から
    // Scene/GameでFramebufferを分離しています。
    if (m_ShowSceneView && m_SceneFramebuffer != nullptr)
    {
        const std::uint32_t width = static_cast<std::uint32_t>(std::max(m_SceneViewportWidth, 1.0f));
        const std::uint32_t height = static_cast<std::uint32_t>(std::max(m_SceneViewportHeight, 1.0f));
        m_SceneFramebuffer->Resize(width, height);
        RenderSceneToFramebuffer(*m_SceneFramebuffer);
    }

    if (m_ShowGameView && m_GameFramebuffer != nullptr)
    {
        const std::uint32_t width = static_cast<std::uint32_t>(std::max(m_GameViewportWidth, 1.0f));
        const std::uint32_t height = static_cast<std::uint32_t>(std::max(m_GameViewportHeight, 1.0f));
        m_GameFramebuffer->Resize(width, height);
        RenderSceneToFramebuffer(*m_GameFramebuffer);
    }
}

void EditorLayer::RenderSceneToFramebuffer(Framebuffer& framebuffer)
{
    if (m_Application == nullptr)
    {
        return;
    }

    Scene* activeScene = m_Application->GetScene();
    if (activeScene == nullptr)
    {
        return;
    }

    // Framebuffer::Bind()はFBOだけでなくglViewportもAttachmentサイズへ合わせます。
    // Scene::OnRender()内部のClearもこのFBOへ作用するため、Main Framebufferを消しません。
    framebuffer.Bind();
    activeScene->OnRender();
    framebuffer.Unbind();
}

void EditorLayer::OnImGuiRender(float dt)
{
    if (m_Application == nullptr)
    {
        return;
    }

    ValidateSelectedEntity();
    BeginDockSpace();

    // Scene/Game Viewを先に構築しても各Windowは独立しているため、Dock順序には依存しません。
    if (m_ShowSceneView)
    {
        RenderSceneView();
    }

    if (m_ShowGameView)
    {
        RenderGameView();
    }

    if (m_ShowStatisticsPanel)
    {
        m_StatisticsPanel.OnImGuiRender(
            dt,
            m_Application->GetWindow(),
            m_Application->GetScene());
    }

    if (m_ShowAnimationDebugPanel)
    {
        m_AnimationDebugPanel.OnImGuiRender(m_Application->GetScene());
    }

    if (m_ShowSceneHierarchyPanel)
    {
        m_SceneHierarchyPanel.OnImGuiRender(
            m_Application->GetScene(),
            m_SelectedEntity);
    }

    if (m_ShowInspectorPanel)
    {
        m_InspectorPanel.OnImGuiRender(m_SelectedEntity);
    }

    EndDockSpace();
}

void EditorLayer::RenderSceneView()
{
    // ImageをWindow端まで広げるため、このWindowだけContent Paddingを0にします。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Scene View", &m_ShowSceneView);
    ImGui::PopStyleVar();

    if (visible)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x > 0.0f && available.y > 0.0f)
        {
            m_SceneViewportWidth = available.x;
            m_SceneViewportHeight = available.y;

            if (m_SceneFramebuffer != nullptr)
            {
                // OpenGL Textureは左下原点、Dear ImGuiのImage UVは左上から扱うため
                // UV0/UV1のYを反転して上下が正しい向きになるよう表示します。
                const ImTextureID textureID = reinterpret_cast<ImTextureID>(
                    static_cast<std::uintptr_t>(m_SceneFramebuffer->GetColorAttachmentRendererID()));
                ImGui::Image(
                    textureID,
                    available,
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));
            }
        }
    }

    ImGui::End();
}

void EditorLayer::RenderGameView()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("Game View", &m_ShowGameView);
    ImGui::PopStyleVar();

    if (visible)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x > 0.0f && available.y > 0.0f)
        {
            m_GameViewportWidth = available.x;
            m_GameViewportHeight = available.y;

            if (m_GameFramebuffer != nullptr)
            {
                const ImTextureID textureID = reinterpret_cast<ImTextureID>(
                    static_cast<std::uintptr_t>(m_GameFramebuffer->GetColorAttachmentRendererID()));
                ImGui::Image(
                    textureID,
                    available,
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));
            }
        }
    }

    ImGui::End();
}

void EditorLayer::OnEvent(Event& event)
{
    (void)event;

    // Editor Camera / Gizmo入力はScene Viewのhover/focus状態と組み合わせて次段階で処理します。
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

    if (static_cast<bool>(m_SelectedEntity) == false)
    {
        return;
    }

    if (m_SelectedEntity.GetScene() != activeScene)
    {
        m_SelectedEntity = Entity{};
        return;
    }

    if (activeScene->IsEntityAlive(m_SelectedEntity) == false)
    {
        m_SelectedEntity = Entity{};
    }
}

void EditorLayer::BeginDockSpace()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return;
    }

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags = 0;
    windowFlags |= ImGuiWindowFlags_MenuBar;
    windowFlags |= ImGuiWindowFlags_NoDocking;
    windowFlags |= ImGuiWindowFlags_NoTitleBar;
    windowFlags |= ImGuiWindowFlags_NoCollapse;
    windowFlags |= ImGuiWindowFlags_NoResize;
    windowFlags |= ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
    windowFlags |= ImGuiWindowFlags_NoNavFocus;

    // Scene/Gameの映像は専用ImGui Windowへ表示するようになったため、以前必要だった
    // NoBackgroundは外します。Editor中央部も通常のDockSpace背景として描画されます。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("RavenEditorDockSpaceHost", nullptr, windowFlags);
    m_DockSpaceBegun = true;

    ImGui::PopStyleVar(3);

    const ImGuiID dockSpaceId = ImGui::GetID("RavenEditorDockSpace");

    // Scene/Game ViewがTextureとして独立したためPassthruCentralNodeも不要です。
    // DockSpace自身が通常背景を持つことで、Viewport Windowを閉じてもMain Sceneが透けません。
    const ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_None;
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), dockSpaceFlags);

    RenderMenuBar();
}

void EditorLayer::EndDockSpace()
{
    if (m_DockSpaceBegun == false)
    {
        return;
    }

    ImGui::End();
    m_DockSpaceBegun = false;
}

void EditorLayer::RenderMenuBar()
{
    if (ImGui::BeginMenuBar() == false)
    {
        return;
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Scene View", nullptr, &m_ShowSceneView);
        ImGui::MenuItem("Game View", nullptr, &m_ShowGameView);
        ImGui::Separator();
        ImGui::MenuItem("Statistics", nullptr, &m_ShowStatisticsPanel);
        ImGui::MenuItem("Animation Debug", nullptr, &m_ShowAnimationDebugPanel);
        ImGui::MenuItem("Scene Hierarchy", nullptr, &m_ShowSceneHierarchyPanel);
        ImGui::MenuItem("Inspector", nullptr, &m_ShowInspectorPanel);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

} // namespace Raven
