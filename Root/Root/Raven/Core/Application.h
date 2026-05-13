#pragma once
#include "Window.h"
#include "Input.h"
#include "KeyCodes.h"

#include "../Renderer/RenderCommand.h"

#include <memory>
#include <iostream>

namespace Raven
{

class Application
{

public:

    Application();

    void Run();

    void OnEvent(Event& event);

private:
    bool m_Running = true;
    std::unique_ptr<Window> m_Window;
};

}