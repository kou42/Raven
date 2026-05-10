#pragma once
#include "Window.h"
#include "Input.h"
#include "KeyCodes.h"

#include <memory>
#include <iostream>

class Application
{
public:
    Application()
    {
        m_Window = Window::Create();

        m_Window->SetEventCallback([this](Event& event)
            {
                OnEvent(event);
            });
    }

    void Run()
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

            m_Window->OnUpdate();
        }
    }

    void OnEvent(Event& event)
    {
        std::cout << event.ToString() << std::endl;

        if (event.GetEventType() == EventType::WindowClose)
        {
            m_Running = false;
            event.Handled = true;
        }
    }

private:
    bool m_Running = true;
    std::unique_ptr<Window> m_Window;
};