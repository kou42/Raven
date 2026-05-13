#include "Application.h"


namespace Raven
{

Application::Application()
{
    m_Window = Window::Create();

    m_Window->SetEventCallback([this](Event& event)
        {
            OnEvent(event);
        });
}

void Application::Run()
{
    while (m_Running)
    {
        if (Input::IsKeyPressed(Key::Escape))
        {
            m_Running = false;
        }

        if (Input::IsKeyPressed(Key::W))
        {
            std::cout << "W Pressed\n";
        }

        auto [x, y] = Input::GetMousePosition();

        std::cout << x << ", " << y << std::endl;

        RenderCommand::SetClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        RenderCommand::Clear();

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

}
