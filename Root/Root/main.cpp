#ifdef _WIN32
#include <Windows.h>
#endif

#include "Raven/Core/Application.h"
#include "Raven/Renderer/Layer/SandboxLayer.h"
#include "Raven/Core/Base.h"
#include "Raven/Scene/SceneGame.h"
#include "Raven/Editor/EditorLayer.h"
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
    // SoftBody統合StepのSelfTestはassertベースであり、通常のSimulation処理ではありません。
    // Application / Sceneを生成する前に1回だけ実行することで、毎フレームの実行コストを避けつつ、
    // Particle-Triangle CounterやSpatial Hash比較処理の回帰をDebug起動時に早期検出します。
    //
    // Release構成では _DEBUG が定義されないため、この呼び出し自体がコンパイル対象外になります。
    Raven::ph::tests::RunSoftBodyIntegratedStepSelfTests();
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
