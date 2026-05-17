#pragma once
#include "Window.h"
#include "Input.h"
#include "KeyCodes.h"

#include "../Renderer/RenderCommand.h"
#include "../Renderer/Layer/Layer.h"
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

    void PushLayer(Layer* layer);
    void PushLayer(Scope<Layer> layer);

private:
    bool m_Running = true;
    std::unique_ptr<Window> m_Window;
    //std::vector<Layer*> m_Layers;
    std::vector<Scope<Layer>> m_Layers;
};

}