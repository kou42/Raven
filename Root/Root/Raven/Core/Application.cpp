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
    m_ImGuiLayer = CreateScope<ImGuiLayer>(*m_Window);
    m_ImGuiLayer->OnAttach();
}

Application::~Application()
{
    // 通常Layerを先にDetachします。
    // 将来EditorLayer::OnDetach()がImGui関連リソースへ触れる場合でも、ImGui Contextがまだ有効な順序を保証します。
    for (auto& layer : m_Layers)
    {
        if (layer != nullptr)
        {
            layer->OnDetach();
        }
    }

    // ImGui OpenGL backendは有効なContextを必要とするためWindowより先に明示的にDetachします。
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

        // デバッグ停止やウィンドウ移動後の巨大dtを制限します。
        frameDeltaTime = std::min(frameDeltaTime, 0.25f);

        if (m_scene != nullptr)
        {
            m_scene->OnUpdate(frameDeltaTime);
            m_scene->OnRender();
        }

        // 通常LayerのRuntime更新・描画はDear ImGui frameとは独立して実行します。
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
            // Dear ImGuiは1 frameにつきBegin/Endを一度だけ実行し、その間で各LayerにUI構築を依頼します。
            // これにより将来EditorLayerをPushLayer()するだけでOnImGuiRender()を参加させられます。
            m_ImGuiLayer->Begin();

            for (auto& layer : m_Layers)
            {
                if (layer != nullptr)
                {
                    layer->OnImGuiRender(frameDeltaTime);
                }
            }

            // 現段階のbootstrap Statistics表示です。
            // 次段階でEditorLayer/StatisticsPanelへ移行した後は、この呼び出し自体を不要にできます。
            m_ImGuiLayer->OnImGuiRender(frameDeltaTime);
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

    // Eventがまだ処理されていない場合だけLayerへ逆順伝播します。
    // 後から積まれたEditor/Overlay系Layerほど先に入力を受け取れるため、Editor UIとの統合にも向いた順序です。
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
