#ifdef _WIN32
#include <Windows.h>
#endif

#include <filesystem>

#include "Raven/Core/Application.h"
#include "Raven/Renderer/Layer/SandboxLayer.h"
#include "Raven/Core/Base.h"
#include "Raven/Scene/SceneGame.h"
#include "Raven/Editor/EditorLayer.h"
#include "Raven/Debug/BrowserDebugViewer.h"
#include "Raven/Physics/SoftBody/Debug/SoftBodyClothDemoLayer.h"
#include "Raven/Physics/SoftBody/Debug/SoftBodyJellyDemoLayer.h"

#ifdef _DEBUG
#include "Raven/Physics/Tests/SoftBodyIntegratedStepSelfTests.h"
#endif

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

#ifdef _DEBUG
    // ========================================================================
    // Debug Startup Self Tests
    // ========================================================================
    Raven::ph::tests::RunSoftBodyIntegratedStepSelfTests();

    // ========================================================================
    // Browser Debug Viewer
    // ========================================================================
    // ブラウザではSVGそのものではなくViewer.htmlを開きます。
    // Viewer.htmlはStartup.svgを定期的に再読み込みするため、後続のPhysics Writerが同じSVGを
    // 上書きすればブラウザを再起動せず最新のデバッグ表示へ更新できます。
    const std::filesystem::path browserDebugDirectory =
        std::filesystem::path("Raven") / "Debug" / "Generated";
    const std::filesystem::path browserDebugSvgPath = browserDebugDirectory / "Startup.svg";
    const std::filesystem::path browserDebugHtmlPath = browserDebugDirectory / "Viewer.html";

    const bool svgWritten = Raven::BrowserDebugViewer::WriteStartupSvg(browserDebugSvgPath);
    const bool htmlWritten = Raven::BrowserDebugViewer::WriteAutoReloadHtml(
        browserDebugHtmlPath,
        browserDebugSvgPath,
        250u);

    if (svgWritten == true && htmlWritten == true)
    {
        Raven::BrowserDebugViewer::Open(std::filesystem::absolute(browserDebugHtmlPath));
    }
#endif

    Raven::Application app;

    // Runtime Sceneを先に生成した後、SoftBody検証LayerとEditorLayerを登録します。
    // Cloth / Jelly LayerのOnAttach()はApplicationからActive Sceneを借用して
    // MeshRendererComponent + MeshDeformationComponentを登録するため、SetScene()より後である必要があります。
    app.SetScene(Raven::CreateScope<Raven::SceneGame>());
    app.PushLayer(Raven::CreateScope<Raven::SoftBodyClothDemoLayer>(app));
    app.PushLayer(Raven::CreateScope<Raven::SoftBodyJellyDemoLayer>(app));
    app.PushLayer(Raven::CreateScope<Raven::EditorLayer>(app));

    app.Run();
    return 0;
}