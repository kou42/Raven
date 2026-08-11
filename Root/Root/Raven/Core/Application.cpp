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

    // ImGuiLayerもRavenのLayerライフサイクルへ統合します。
    // ただしDear ImGuiのBegin/Endは全LayerのOnImGuiRender()を囲む必要があるため、
    // Applicationが専用Layerへの参照を保持してframe境界だけを制御します。
    //
    // Editor固有UIはEditorLayer側へ分離し、ApplicationはEditorのPanel内容を知りません。
    m_ImGuiLayer = CreateScope<ImGuiLayer>(*m_Window);
    m_ImGuiLayer->OnAttach();
}

Application::~Application()
{
    for (auto& layer : m_Layers)
    {
        if (layer != nullptr)
        {
            layer->OnDetach();
        }
    }

    if (m_ImGuiLayer != nullptr)
    {
        m_ImGuiLayer->OnDetach();
        m_ImGuiLayer.reset();
    }
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
    if (layer == nullptr)
    {
        return;
    }

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
        frameDeltaTime = std::min(frameDeltaTime, 0.25f);

        // Renderer統計はApplication frame単位で集計します。
        // Scene描画より前にResetすることで、Scene本体とその後のDebug Overlayを同じframeへ含めます。
        Renderer::BeginFrame();

        if (m_scene != nullptr)
        {
            m_scene->OnUpdate(frameDeltaTime);
            m_scene->OnRender();
        }

        for (auto& layer : m_Layers)
        {
            if (layer != nullptr)
            {
                layer->OnUpdate(frameDeltaTime);
                layer->OnRender();
            }
        }

        if (m_ImGuiLayer != nullptr)
        {
            m_ImGuiLayer->Begin();

            for (auto& layer : m_Layers)
            {
                if (layer != nullptr)
                {
                    layer->OnImGuiRender(frameDeltaTime);
                }
            }

            m_ImGuiLayer->End();
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

    for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
    {
        if (event.Handled)
        {
            break;
        }

        if (*it != nullptr)
        {
            (*it)->OnEvent(event);
        }
    }
}

} // namespace Raven
