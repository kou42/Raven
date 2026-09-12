#ifdef _WIN32
#include <Windows.h>
#endif

#include <filesystem>
#include <utility>

#include "Raven/Character/Debug/CharacterControllerDemoLayer.h"
#include "Raven/Character/Debug/CharacterLocomotionDebugOverlayLayer.h"
#include "Raven/Character/Debug/CharacterPositionDebugOverlayLayer.h"
#include "Raven/Core/Application.h"
#include "Raven/Renderer/Layer/SandboxLayer.h"
#include "Raven/Core/Base.h"
#include "Raven/Scene/SceneGame.h"
#include "Raven/Editor/EditorLayer.h"
#include "Raven/Debug/BrowserDebugConfig.h"
#include "Raven/Debug/BrowserDebugServer.h"
#include "Raven/Debug/BrowserDebugViewer.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Fluid/Debug/FluidBuoyancyDebugOverlayLayer.h"
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
    // ========================================================================
    // Debug Startup Self Tests
    // ========================================================================
    // Character locomotionの速度選択とBlendTree/Animation Profileの回帰テストに加えて、
    // Motion Matching切替時のPose/速度連続性も実際のDebug起動時に必ず検証します。
    Raven::tests::RunCharacterCeilingCollisionSelfTests();
    Raven::tests::RunCharacterSprintLocomotionSelfTests();
    Raven::tests::RunBlendTreeRuntimeSelfTests();
    Raven::tests::RunPoseInertializerSelfTests();
    Raven::ph::tests::RunPhysicsSimulationWorldSelfTests();
    Raven::ph::tests::RunSoftBodyIntegratedStepSelfTests();
    Raven::ph::tests::RunStaticMeshTriangleBVHSelfTests();

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
    // Cloth / Jelly / Fluid LayerはApplicationからActive Sceneを借用するため、すべてSetScene()後に登録します。
    app.SetScene(Raven::CreateScope<Raven::SceneGame>());

    Raven::Scene* runtimeScene = app.GetScene();
    if (runtimeScene != nullptr)
    {
        // Character本体はScene-ownedのまま維持し、ImGui表示だけをApplication-owned Overlayへ分離します。
        // OverlayはCharacter Layerを非所有pointerで参照しますが、ApplicationはApplication LayerをSceneより先に
        // 破棄するため、終了順序上もdangling pointerになりません。
        //
        // FluidデモはTerrainの影響を避けるため原点から離れた位置へ配置しています。
        // そのためデモエリアへ移動するときに現在座標を確認できるよう、Character診断HUDを有効にします。
        // HUDは表示専用のApplication Layerであり、Character Controller本体のPhysics更新順には影響しません。
        auto characterLayer = Raven::CreateScope<Raven::CharacterControllerDemoLayer>(*runtimeScene);
        Raven::CharacterControllerDemoLayer* characterLayerPointer = characterLayer.get();

        runtimeScene->PushLayer(std::move(characterLayer));

        if (characterLayerPointer != nullptr)
        {
            app.PushLayer(
                Raven::CreateScope<Raven::CharacterLocomotionDebugOverlayLayer>(
                    *characterLayerPointer));

            // 水槽本体は(50, 4, 50)を中心にXZ各4mの範囲です。
            // Characterを+Z側の外へ配置すると、既定Yaw=0のOrbit Cameraが+Z側から-Zを向くため、
            // Character越しに水槽全体を正面へ捉えやすくなります。Y=0は既存の無限Plane床上です。
            const Raven::math::Vec3 fluidDemoCharacterDebugPosition{ 50.0f, 0.0f, 58.0f };
            app.PushLayer(
                Raven::CreateScope<Raven::CharacterPositionDebugOverlayLayer>(
                    *characterLayerPointer,
                    fluidDemoCharacterDebugPosition));
        }
    }

    app.PushLayer(Raven::CreateScope<Raven::SoftBodyClothDemoLayer>(app));
    app.PushLayer(Raven::CreateScope<Raven::SoftBodyJellyDemoLayer>(app));
    // SPH Particleも通常EntityとしてActive Sceneへ登録し、既存ECS描画経路で可視化します。
    app.PushLayer(Raven::CreateScope<Raven::FluidSPHDemoLayer>(app));
    // Fluid LayerがBox / Sphereを生成した後にHUDを登録し、起動時Resetで水面付近へ揃えます。
    // Solver/Coupling本体へDebug入力依存を持ち込まず、検証操作だけをApplication Overlayへ分離します。
    app.PushLayer(Raven::CreateScope<Raven::FluidBuoyancyDebugOverlayLayer>(app));

#ifdef _DEBUG
    // 実ファイルの読み込みからUI Tree展開、AnimationClip再生、OpenGL UI描画までを
    // 起動中に一続きで確認するSVG検証Layerです。Asset固有PathはDebug Layer内へ閉じ込めます。
    app.PushLayer(Raven::CreateScope<Raven::UISvgDemoLayer>(app));
#endif

    app.PushLayer(Raven::CreateScope<Raven::EditorLayer>(app));

    app.Run();
    return 0;
}
