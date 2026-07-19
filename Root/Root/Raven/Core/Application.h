#pragma once
#include "Raven/Core/Window.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"

#include "Raven/Scene/Scene.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Core/Event.h"

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

    void SetScene(Scope<Scene> scene);

private:
    bool m_Running = true;
    std::unique_ptr<Window> m_Window;
    //std::vector<Layer*> m_Layers;
    std::vector<Scope<Layer>> m_Layers;
    
    Scope<Scene> m_scene;
};

}