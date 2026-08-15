#ifdef _WIN32
#include <Windows.h>
#endif

#include "Raven/Core/Application.h"
#include "Raven/Renderer/Layer/SandboxLayer.h"
#include "Raven/Core/Base.h"
#include "Raven/Scene/SceneGame.h"
#include "Raven/Editor/EditorLayer.h"

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    Raven::Application app;

    // Runtime SceneはApplicationへ設定し、Editor機能は通常Layerとして独立して登録します。
    // EditorLayerへApplication参照を渡すことで、所有権を移さずActive Scene / Window等の
    // Runtime状態を各Editor Panelへ安全に橋渡しできます。
    app.SetScene(Raven::CreateScope<Raven::SceneGame>());
    app.PushLayer(Raven::CreateScope<Raven::EditorLayer>(app));

    app.Run();
    return 0;
}
