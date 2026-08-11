#include "Raven/ImGui/ImGuiLayer.h"

#include "Raven/Core/Window.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

namespace Raven
{

ImGuiLayer::ImGuiLayer(Window& window)
    : m_Window(&window)
{
}

ImGuiLayer::~ImGuiLayer()
{
    // Application側でOnDetach()を呼ぶのが通常経路ですが、
    // 途中初期化失敗や将来の所有方法変更でもbackendを残さないようdestructorでも安全に後始末します。
    OnDetach();
}

void ImGuiLayer::OnAttach()
{
    if (m_Initialized)
    {
        return;
    }

    if (m_Window == nullptr)
    {
        return;
    }

    GLFWwindow* nativeWindow = static_cast<GLFWwindow*>(m_Window->GetNativeWindow());
    if (nativeWindow == nullptr)
    {
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Docking版を固定して導入しているため、今後Editor Panelを増やした際に
    // Hierarchy / Inspector / Statistics等をDock可能な構成へそのまま拡張できます。
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // install_callbacks=true とすることでGLFW callbackをDear ImGui側にも連結します。
    // backendは既存callbackを保持してチェーンするため、RavenのWindow Event callbackと共存できます。
    if (ImGui_ImplGlfw_InitForOpenGL(nativeWindow, true) == false)
    {
        ImGui::DestroyContext();
        return;
    }

    if (ImGui_ImplOpenGL3_Init("#version 330 core") == false)
    {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return;
    }

    m_Initialized = true;
}

void ImGuiLayer::OnDetach()
{
    if (m_Initialized == false)
    {
        return;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_Initialized = false;
}

void ImGuiLayer::Begin()
{
    if (m_Initialized == false)
    {
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::End()
{
    if (m_Initialized == false)
    {
        return;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiLayer::OnImGuiRender(float dt)
{
    // 現段階では導入確認用StatisticsのみをImGuiLayer自身が描画します。
    // EditorLayer導入後はこの表示をStatisticsPanelへ移し、ImGuiLayerはbackend管理へ専念させます。
    RenderDebugStatistics(dt);
}

void ImGuiLayer::RenderDebugStatistics(float deltaTime)
{
    if (m_Initialized == false || m_Window == nullptr)
    {
        return;
    }

    const float frameTimeMs = deltaTime * 1000.0f;
    const float fps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;

    ImGui::Begin("Raven Debug / Statistics");
    ImGui::TextUnformatted("Runtime");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("Frame Time: %.3f ms", frameTimeMs);
    ImGui::Text("Window: %u x %u", m_Window->GetWidth(), m_Window->GetHeight());
    ImGui::Separator();
    ImGui::TextDisabled("Statistics Panel bootstrap - more engine counters will be added later.");
    ImGui::End();
}

} // namespace Raven
