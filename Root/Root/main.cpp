#ifdef _WIN32
#include <Windows.h>
#endif

#include <filesystem>

#include "Raven/Character/Debug/CharacterControllerDemoLayer.h"
#include "Raven/Core/Application.h"
#include "Raven/Renderer/Layer/SandboxLayer.h"
#include "Raven/Core/Base.h"
#include "Raven/Scene/SceneGame.h"
#include "Raven/Editor/EditorLayer.h"
#include "Raven/Debug/BrowserDebugConfig.h"
#include "Raven/Debug/BrowserDebugServer.h"
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
    // Viewer.htmlはStartup.svg / CandidateRejects.svgを定期的に再読み込みするため、後続のPhysics Writerが
    // 同じSVGを上書きすればブラウザを再起動せず最新のデバッグ表示へ更新できます。
    //
    // Viewer.html / Startup.svg / CandidateRejects.svgはDebug生成物として従来どおりファイルへ書き出しますが、
    // ブラウザからはfile://で直接開かず、127.0.0.1限定のBrowserDebugServer経由で表示します。
    // これにより自動reloadだけでなく、BrowserのParticle / Triangle選択を/filter endpointから
    // Raven Processへ返し、次のCandidateRejects.svg生成条件へ反映できます。
    //
    // Browser Debugは診断時だけ必要で、SoftBody Snapshot再評価やSVG I/OはProfilerへ無視できない負荷を
    // 与える可能性があります。そのため起動処理とRuntime Snapshot処理はBrowserDebugConfig.hの
    // kEnableBrowserDebugViewerで一括してON/OFFします。
    if (Raven::kEnableBrowserDebugViewer == true)
    {
        const std::filesystem::path browserDebugDirectory =
            std::filesystem::path("Raven") / "Debug" / "Generated";
        const std::filesystem::path browserDebugSvgPath = browserDebugDirectory / "Startup.svg";
        const std::filesystem::path browserDebugHtmlPath = browserDebugDirectory / "Viewer.html";

        const bool svgWritten = Raven::BrowserDebugViewer::WriteStartupSvg(browserDebugSvgPath);
        const bool htmlWritten = Raven::BrowserDebugViewer::WriteAutoReloadHtml(
            browserDebugHtmlPath,
            browserDebugSvgPath,
            250u);

        Raven::BrowserDebugServer& browserDebugServer = Raven::BrowserDebugServer::Get();
        const bool serverStarted = browserDebugServer.Start(browserDebugDirectory, 18765u);

        if (svgWritten == true
            && htmlWritten == true
            && serverStarted == true)
        {
            Raven::BrowserDebugViewer::OpenUrl(browserDebugServer.GetViewerUrl());
        }
    }
#endif

    Raven::Application app;

    // Runtime Sceneを先に生成した後、Character / SoftBody検証LayerとEditorLayerを登録します。
    // Character ControllerはPhysics Query後のTransformを同じFrameのScene Renderへ反映したいため、
    // Application LayerではなくScene-owned Layerとして登録します。
    // Cloth / Jelly Layerは従来どおりApplicationからActive Sceneを借用するため、すべてSetScene()後に登録します。
    app.SetScene(Raven::CreateScope<Raven::SceneGame>());

    Raven::Scene* runtimeScene = app.GetScene();
    if (runtimeScene != nullptr)
    {
        runtimeScene->PushLayer(Raven::CreateScope<Raven::CharacterControllerDemoLayer>(*runtimeScene));
    }

    app.PushLayer(Raven::CreateScope<Raven::SoftBodyClothDemoLayer>(app));
    app.PushLayer(Raven::CreateScope<Raven::SoftBodyJellyDemoLayer>(app));
    app.PushLayer(Raven::CreateScope<Raven::EditorLayer>(app));

    app.Run();
    return 0;
}