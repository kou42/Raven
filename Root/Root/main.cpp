#include "Raven/Core/Application.h"
#include "Raven/Renderer/Layer/SandboxLayer.h"
#include "Raven/Core/Base.h"
#include "Raven/Scene/SceneGame.h"
#include "Raven/Editor/EditorLayer.h"

int main()
{
    Raven::Application app;

    // Runtime SceneはApplicationへ設定し、Editor機能は通常Layerとして独立して登録します。
    // これによりApplicationはEditor固有UIを知らず、Editor側だけを段階的に拡張できます。
    app.SetScene(Raven::CreateScope<Raven::SceneGame>());
    app.PushLayer(Raven::CreateScope<Raven::EditorLayer>());

    app.Run();
    return 0;
}
