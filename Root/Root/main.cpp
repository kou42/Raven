#ifdef _WIN32
#include <Windows.h>
#endif

#include "Raven/Core/Application.h"
#include "Raven/Renderer/Layer/SandboxLayer.h"
#include "Raven/Core/Base.h"
#include "Raven/Scene/SceneGame.h"
#include "Raven/Editor/EditorLayer.h"
#include "Raven/Physics/SoftBody/Debug/SoftBodyClothDemoLayer.h"

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    Raven::Application app;

    // Runtime Sceneを先に生成した後、SoftBody検証LayerとEditorLayerを登録します。
    // SoftBodyClothDemoLayer::OnAttach()はApplicationからActive Sceneを借用して
    // MeshDeformationComponentを登録するため、SetScene()より後である必要があります。
    app.SetScene(Raven::CreateScope<Raven::SceneGame>());
    app.PushLayer(Raven::CreateScope<Raven::SoftBodyClothDemoLayer>(app));
    app.PushLayer(Raven::CreateScope<Raven::EditorLayer>(app));

    app.Run();
    return 0;
}
