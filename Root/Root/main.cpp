#ifdef _WIN32
#include <Windows.h>
#endif

#include <filesystem>
#include <utility>

#include "Raven/Character/Debug/CharacterControllerDemoLayer.h"
#include "Raven/Character/Debug/CharacterLocomotionDebugOverlayLayer.h"
#include "Raven/Core/Application.h"
#include "Raven/Renderer/Layer/SandboxLayer.h"
#include "Raven/Core/Base.h"
#include "Raven/Scene/SceneGame.h"
#include "Raven/Editor/EditorLayer.h"
#include "Raven/Debug/BrowserDebugConfig.h"
#include "Raven/Debug/BrowserDebugServer.h"
#include "Raven/Debug/BrowserDebugViewer.h"
#include "Raven/Physics/Fluid/Debug/FluidSPHDemoLayer.h"
#include "Raven/Physics/SoftBody/Debug/SoftBodyClothDemoLayer.h"
#include "Raven/Physics/SoftBody/Debug/SoftBodyJellyDemoLayer.h"

#ifdef _DEBUG
#include "Raven/Animation/Tests/BlendTreeRuntimeSelfTests.h"
#include "Raven/Animation/Tests/PoseInertializerSelfTests.h"
#include "Raven/Character/Tests/CharacterCeilingCollisionSelfTests.h"
#include "Raven/Character/Tests/CharacterSprintLocomotionSelfTests.h"
#include "Raven/Physics/Tests/PhysicsSimulationWorldSelfTests.h"
#include "Raven/Physics/Tests/SoftBodyIntegratedStepSelfTests.h"
#include "Raven/Physics/Tests/StaticMeshTriangleBVHSelfTests.h"
#include "Raven/UI/Svg/Debug/UISvgDemoLayer.h"
#endif

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

#ifdef _DEBUG
    Raven::tests::RunCharacterCeilingCollisionSelfTests();
    Raven::tests::RunCharacterSprintLocomotionSelfTests();
    Raven::tests::RunBlendTreeRuntimeSelfTests();
    Raven::tests::RunPoseInertializerSelfTests();
    Raven::ph::tests::RunPhysicsSimulationWorldSelfTests();
    Raven::ph::tests::RunSoftBodyIntegratedStepSelfTests();
    Raven::ph::tests::RunStaticMeshTriangleBVHSelfTests();

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
    app.SetScene(Raven::CreateScope<Raven::SceneGame>());

    Raven::Scene* runtimeScene = app.GetScene();
    if (runtimeScene != nullptr)
    {
        // Character本体はScene-ownedのまま維持し、ImGui表示だけをApplication-owned Overlayへ分離します。
        // Fluidデモ位置へ移動するとき現在座標を常時確認できるよう、診断HUDを再有効化します。
        auto characterLayer = Raven::CreateScope<Raven::CharacterControllerDemoLayer>(*runtimeScene);
        Raven::CharacterControllerDemoLayer* characterLayerPointer = characterLayer.get();

        runtimeScene->PushLayer(std::move(characterLayer));

        if (characterLayerPointer != nullptr)
        {
            app.PushLayer(
                Raven::CreateScope<Raven::CharacterLocomotionDebugOverlayLayer>(
                    *characterLayerPointer));
        }
    }

    app.PushLayer(Raven::CreateScope<Raven::SoftBodyClothDemoLayer>(app));
    app.PushLayer(Raven::CreateScope<Raven::SoftBodyJellyDemoLayer>(app));
    // SPH Particleも通常EntityとしてActive Sceneへ登録し、既存ECS描画経路で可視化します。
    app.PushLayer(Raven::CreateScope<Raven::FluidSPHDemoLayer>(app));

#ifdef _DEBUG
    app.PushLayer(Raven::CreateScope<Raven::UISvgDemoLayer>(app));
#endif

    app.PushLayer(Raven::CreateScope<Raven::EditorLayer>(app));

    app.Run();
    return 0;
}
