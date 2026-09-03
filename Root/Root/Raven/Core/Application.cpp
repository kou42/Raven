#include "Application.h"
#include "../Renderer/Renderer.h"
#include "Raven/ImGui/ImGuiLayer.h"
#include "Raven/UI/Rendering/UIRenderer.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UIPanel.h"
#include "Raven/UI/Widgets/UISlider.h"
#include "Raven/UI/Widgets/UISplitter.h"

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
    m_UIContext.SetRenderer(UIRenderer::Create());

#if defined(_DEBUG)
    auto validationPanel = CreateScope<UIPanel>();
    validationPanel->SetPosition(math::Vec2(24.0f, 48.0f));
    validationPanel->SetSize(math::Vec2(360.0f, 190.0f));
    validationPanel->SetBackgroundColor(math::Vec4(0.05f, 0.08f, 0.14f, 0.96f));
    validationPanel->SetLayoutMode(UILayoutMode::Vertical);
    validationPanel->SetPadding(12.0f);
    validationPanel->SetSpacing(10.0f);

    auto headerButton = CreateScope<UIButton>();
    headerButton->SetSize(math::Vec2(336.0f, 42.0f));
    headerButton->SetFocusable(true);
    headerButton->SetNormalColor(math::Vec4(0.10f, 0.28f, 0.55f, 1.0f));
    headerButton->SetHoveredColor(math::Vec4(0.16f, 0.40f, 0.72f, 1.0f));
    headerButton->SetPressedColor(math::Vec4(0.07f, 0.20f, 0.42f, 1.0f));
    headerButton->SetOnClick([]()
        {
            std::cout << "Raven UI validation button clicked" << std::endl;
        });
    validationPanel->AddChild(std::move(headerButton));

    auto horizontalRow = CreateScope<UIPanel>();
    horizontalRow->SetSize(math::Vec2(336.0f, 62.0f));
    horizontalRow->SetBackgroundColor(math::Vec4(0.08f, 0.11f, 0.18f, 1.0f));
    horizontalRow->SetLayoutMode(UILayoutMode::Horizontal);
    horizontalRow->SetPadding(UIThickness(8.0f, 9.0f));
    horizontalRow->SetSpacing(8.0f);

    auto leftPanel = CreateScope<UIPanel>();
    leftPanel->SetSize(math::Vec2(120.0f, 44.0f));
    leftPanel->SetMinSize(math::Vec2(72.0f, 44.0f));
    leftPanel->SetMaxSize(math::Vec2(224.0f, 44.0f));
    leftPanel->SetBackgroundColor(math::Vec4(0.18f, 0.48f, 0.32f, 1.0f));
    UIPanel* leftPanelElement = leftPanel.get();
    horizontalRow->AddChild(std::move(leftPanel));

    auto splitter = CreateScope<UISplitter>();
    splitter->SetSize(math::Vec2(8.0f, 44.0f));
    splitter->SetOrientation(UISplitterOrientation::Vertical);

    auto rightPanel = CreateScope<UIPanel>();
    rightPanel->SetSize(math::Vec2(176.0f, 44.0f));
    rightPanel->SetMinSize(math::Vec2(72.0f, 44.0f));
    rightPanel->SetMaxSize(math::Vec2(224.0f, 44.0f));
    rightPanel->SetBackgroundColor(math::Vec4(0.42f, 0.20f, 0.55f, 1.0f));
    UIPanel* rightPanelElement = rightPanel.get();

    splitter->SetOnDragDelta([leftPanelElement, rightPanelElement](float delta)
        {
            if (leftPanelElement == nullptr || rightPanelElement == nullptr)
            {
                return;
            }

            constexpr float panelTotalWidth = 296.0f;
            const float currentLeftWidth = leftPanelElement->GetPreferredSize().x;
            const float newLeftWidth = std::clamp(currentLeftWidth + delta, 72.0f, 224.0f);
            const float newRightWidth = panelTotalWidth - newLeftWidth;
            leftPanelElement->SetPreferredSize(math::Vec2(newLeftWidth, 44.0f));
            rightPanelElement->SetPreferredSize(math::Vec2(newRightWidth, 44.0f));
        });

    horizontalRow->AddChild(std::move(splitter));
    horizontalRow->AddChild(std::move(rightPanel));
    validationPanel->AddChild(std::move(horizontalRow));

    auto footerSlider = CreateScope<UISlider>();
    footerSlider->SetSize(math::Vec2(336.0f, 42.0f));
    footerSlider->SetFocusable(true);
    footerSlider->SetRange(0.0f, 1.0f);
    footerSlider->SetValue(0.35f);
    footerSlider->SetOnValueChanged([](float value)
        {
            std::cout << "Raven UI validation slider: " << value << std::endl;
        });
    validationPanel->AddChild(std::move(footerSlider));

    m_UIContext.GetRootElement().AddChild(std::move(validationPanel));
#endif

    m_ImGuiLayer = CreateScope<ImGuiLayer>(*m_Window);
    m_ImGuiLayer->OnAttach();
}

Application::~Application()
{
    for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
    {
        if (*it != nullptr)
        {
            (*it)->OnDetach();
        }
    }
    m_Layers.clear();

    if (m_scene != nullptr)
    {
        m_scene->OnDestroy();
        m_scene->Scene::OnDestroy();
        m_scene.reset();
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
    static_cast<void>(layer);
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
        m_scene->Scene::OnDestroy();
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

        Renderer::BeginFrame();
        m_UIContext.BeginFrame(math::Vec2(
            static_cast<float>(m_Window->GetWidth()),
            static_cast<float>(m_Window->GetHeight())));

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

        m_UIContext.EndFrame();
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

    if (event.GetEventType() == EventType::WindowFocusLost)
    {
        m_UIContext.CancelMouseCapture();
    }

    // GLFW固有KeyをUIのSemantic Keyへここで変換し、UI CoreへPlatform定数を持ち込みません。
    if (event.Handled == false && event.GetEventType() == EventType::KeyPressed)
    {
        KeyPressedEvent& keyEvent = static_cast<KeyPressedEvent&>(event);
        UIKeyEvent uiEvent;
        uiEvent.Pressed = true;
        uiEvent.Repeat = keyEvent.IsRepeat();
        uiEvent.Shift = (keyEvent.GetModifiers() & GLFW_MOD_SHIFT) != 0;
        uiEvent.Context = &m_UIContext;
        if (keyEvent.GetKeyCode() == GLFW_KEY_TAB)
        {
            uiEvent.Key = UIKey::Tab;
        }
        event.Handled = m_UIContext.RouteKeyEvent(uiEvent);
    }
    else if (event.Handled == false && event.GetEventType() == EventType::KeyReleased)
    {
        KeyReleasedEvent& keyEvent = static_cast<KeyReleasedEvent&>(event);
        UIKeyEvent uiEvent;
        uiEvent.Pressed = false;
        uiEvent.Shift = (keyEvent.GetModifiers() & GLFW_MOD_SHIFT) != 0;
        uiEvent.Context = &m_UIContext;
        if (keyEvent.GetKeyCode() == GLFW_KEY_TAB)
        {
            uiEvent.Key = UIKey::Tab;
        }
        event.Handled = m_UIContext.RouteKeyEvent(uiEvent);
    }
    else if (event.Handled == false && event.GetEventType() == EventType::MouseMoved)
    {
        MouseMovedEvent& mouseEvent = static_cast<MouseMovedEvent&>(event);
        event.Handled = m_UIContext.RouteMouseMove(math::Vec2(mouseEvent.GetX(), mouseEvent.GetY()));
    }
    else if (event.Handled == false && event.GetEventType() == EventType::MouseScrolled)
    {
        MouseScrolledEvent& mouseEvent = static_cast<MouseScrolledEvent&>(event);
        event.Handled = m_UIContext.RouteMouseScroll(
            math::Vec2(mouseEvent.GetX(), mouseEvent.GetY()),
            math::Vec2(mouseEvent.GetOffsetX(), mouseEvent.GetOffsetY()));
    }
    else if (event.Handled == false && event.GetEventType() == EventType::MouseButtonPressed)
    {
        MouseButtonPressedEvent& mouseEvent = static_cast<MouseButtonPressedEvent&>(event);
        UIMouseButton uiButton = UIMouseButton::None;
        if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_LEFT)
        {
            uiButton = UIMouseButton::Left;
        }
        else if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_RIGHT)
        {
            uiButton = UIMouseButton::Right;
        }
        else if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            uiButton = UIMouseButton::Middle;
        }

        if (uiButton != UIMouseButton::None)
        {
            event.Handled = m_UIContext.RouteMouseDown(math::Vec2(mouseEvent.GetX(), mouseEvent.GetY()), uiButton);
        }
    }
    else if (event.Handled == false && event.GetEventType() == EventType::MouseButtonReleased)
    {
        MouseButtonReleasedEvent& mouseEvent = static_cast<MouseButtonReleasedEvent&>(event);
        UIMouseButton uiButton = UIMouseButton::None;
        if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_LEFT)
        {
            uiButton = UIMouseButton::Left;
        }
        else if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_RIGHT)
        {
            uiButton = UIMouseButton::Right;
        }
        else if (mouseEvent.GetMouseButton() == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            uiButton = UIMouseButton::Middle;
        }

        if (uiButton != UIMouseButton::None)
        {
            event.Handled = m_UIContext.RouteMouseUp(math::Vec2(mouseEvent.GetX(), mouseEvent.GetY()), uiButton);
        }
    }

    for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
    {
        if (event.Handled == true)
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