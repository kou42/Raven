#include "Raven/Core/Application.h"
#include "Raven/Renderer/Layer/SandboxLayer.h"
#include "Raven/Core/Base.h"
#include "Raven/Scene/SceneGame.h"

int main()
{
    Raven::Application app;
    //app.PushLayer(new Raven::SandboxLayer());
    //app.PushLayer(Raven::CreateScope<Raven::SandboxLayer>());
    app.SetScene(Raven::CreateScope<Raven::SceneGame>());
    app.Run();
    return 0;
}