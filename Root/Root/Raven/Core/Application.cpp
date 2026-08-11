#include "Application.h"
#include "../Renderer/Renderer.h"
#include "Raven/ImGui/ImGuiLayer.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>

namespace Raven
{

Application::Application()
{
    m_Window = Window::Create();

    m_Window->SetEventCallback([this](Event& event)
        {
            OnEvent(event);
        });

    Renderer::Init();

    // OpenGL Context生成後にDear ImGui backendを初期化します。
    // Renderer/SceneへImGui依存を持ち込まず、Applicationはframe境界だけを管理します。
    m_ImGuiLayer = CreateScope<ImGuiLayer>(*m_Window);
}

Application::~Application()
{
    // ImGui OpenGL backendは有効なContextを必要とするためWindowより先に破棄します。
    m_ImGuiLayer.reset();
}

void Application::PushLayer(Layer* layer)
{
#if 0
    m_Layers.push_back(layer);
    layer->OnAttach();
#endif
}

void Application::PushLayer(Scope<Layer> layer)
{
    layer->OnAttach();
    m_Layers.push_back(std::move(layer));
}

void Application::SetScene(Scope<Scene> scene)
{
    if (m_scene != nullptr)
    {
        m_scene->OnDestroy();
    }

    m_scene = std::move(scene);
    if (m_scene != nullptr)
    {
        m_scene->OnCreate();
    }
}

void Application::Run()
{
    double previousTime = glfwGetTime();

    while (m_Running)
    {
        if (Input::IsKeyPressed(Key::Escape))
        {
            m_Running = false;
        }

        const double currentTime = glfwGetTime();
        float frameDeltaTime = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        // デバッグ停止やウィンドウ移動後の巨大dtを制限します。
        frameDeltaTime = std::min(frameDeltaTime, 0.25f);

        if (m_scene != nullptr)
        {
            m_scene->OnUpdate(frameDeltaTime);
            m_scene->OnRender();
        }

        // Scene描画後、SwapBuffers前にEditor/Debug UIを重ねます。
        // 今回は導入確認としてStatistics Windowのみを描画し、将来EditorLayerへ責務を移します。
        if (m_ImGuiLayer != nullptr)
        {
            m_ImGuiLayer->BeginFrame();
            m_ImGuiLayer->RenderDebugStatistics(frameDeltaTime);
            m_ImGuiLayer->EndFrame();
        }

        m_Window->OnUpdate();
    }
}

void Application::OnEvent(Event& event)
{
    std::cout << event.ToString() << std::endl;

    if (event.GetEventType() == EventType::WindowClose)
    {
        m_Running = false;
        event.Handled = true;
    }
}

} // namespace Raven
