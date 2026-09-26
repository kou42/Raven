#include "RunDX12SceneRuntimeDemo.h"

#include "Raven/Core/Window.h"
#include "Raven/Core/Application.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"
#include "Raven/Renderer/RHI/RHIExplicitSceneRuntimeFactory.h"
#include "Raven/Renderer/RHI/RHIExplicitSceneSpecification.h"
#include "Raven/Scene/SceneCamera.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/ExplicitCubeSceneDemo.h"

#include <GLFW/glfw3.h>

#include <filesystem>
#include <iostream>

namespace Raven
{

int RunDX12SceneRuntimeDemo()
{
    auto window = Window::Create(WindowProps(
        "Raven DX12 Scene Runtime", 1280, 720, RHIBackend::DirectX12));
    if (window == nullptr || window->GetNativeWindow() == nullptr)
    {
        return 1;
    }

    const std::filesystem::path shaderDirectory =
        std::filesystem::path("Raven") / "Assets" / "Shaders";
    RHIExplicitSceneSpecification sceneSpecification{};
    RHIShaderAssetSpecification& vertexShader = sceneSpecification.VertexShader;
    vertexShader.DirectX12Path =
        (shaderDirectory / "DirectX12" / "SceneMesh.vs.dxil").generic_string();
    RHIShaderAssetSpecification& fragmentShader = sceneSpecification.FragmentShader;
    fragmentShader.DirectX12Path =
        (shaderDirectory / "DirectX12" / "SceneMesh.ps.dxil").generic_string();

    PipelineSpecification& pipelineSpecification = sceneSpecification.Pipeline;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.Blend = false;
    pipelineSpecification.DebugName = "DX12 Normal Mesh Scene";

    std::cout << "[DX12 Scene Demo] Initializing runtime...\n" << std::flush;
    Scope<IExplicitSceneRuntime> runtime = RHIExplicitSceneRuntimeFactory::Create(RHIBackend::DirectX12);
    ExplicitCubeSceneDemo demo;

    // Shader/Pipeline設定はBackendごとに保持し、初期化・失敗時の解放順序は共通化します。
    Application::ExplicitSceneHooks hooks;
    // 通常Applicationと同じScene更新順序でAnimation/Physics/Layerを進めます。
    hooks.OnUpdate = [&demo](float dt) { demo.GetScene().OnUpdate(dt); };
    hooks.OnEvent = [&demo](Event& event) { demo.GetScene().OnEvent(event); };
    hooks.OnScene = [&demo]() { demo.Render(); };
    hooks.OnResizeCamera = [&demo](uint32_t width, uint32_t height)
    {
        demo.ResizeCamera(width, height);
    };
    hooks.OnBeforeShutdown = [&demo]() { demo.Shutdown(); };

    const auto initializeScene = [&demo](IExplicitSceneRuntime& sceneRuntime)
    {
        if (demo.Init("DX12", sceneRuntime.GetWidth(), sceneRuntime.GetHeight()) == false)
        {
            std::cerr << "DX12 Entity Scene creation failed.\n";
            return false;
        }
        if (sceneRuntime.PrepareScene(demo.GetScene()) == false)
        {
            std::cerr << "DX12 Entity Scene mesh preparation failed.\n";
            return false;
        }
        return true;
    };

    return Application::RunInitializedExplicitScene(
        std::move(window), std::move(runtime),
        sceneSpecification, hooks, initializeScene);
}

} // namespace Raven
