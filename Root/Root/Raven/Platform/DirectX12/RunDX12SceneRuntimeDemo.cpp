#include "RunDX12SceneRuntimeDemo.h"

#include "DX12SceneRuntime.h"

#include "Raven/Core/Window.h"
#include "Raven/Core/Application.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"
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
    RHIShaderAssetSpecification vertexShader{};
    vertexShader.DirectX12Path =
        (shaderDirectory / "DirectX12" / "SceneMesh.vs.dxil").generic_string();
    RHIShaderAssetSpecification fragmentShader{};
    fragmentShader.DirectX12Path =
        (shaderDirectory / "DirectX12" / "SceneMesh.ps.dxil").generic_string();

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.Blend = false;
    pipelineSpecification.DebugName = "DX12 Normal Mesh Scene";

    std::cout << "[DX12 Scene Demo] Initializing runtime...\n" << std::flush;
    auto runtime = CreateScope<DX12SceneRuntime>();
    if (runtime->Init(
        *window,
        pipelineSpecification,
        vertexShader,
        fragmentShader) == false)
    {
        std::cerr << "DX12 Scene Runtime initialization failed.\n";
        // Initが部分初期化状態を解放する設計ですが、終了入口でも明示します。
        runtime->Shutdown();
        return 1;
    }

    std::cout << "[DX12 Scene Demo] Runtime initialized.\n" << std::flush;

    // Backend非依存の検証Sceneを共有し、GPU Buffer準備だけRuntimeに任せます。
    ExplicitCubeSceneDemo demo;
    if (demo.Init("DX12", runtime->GetWidth(), runtime->GetHeight()) == false)
    {
        std::cerr << "DX12 Entity Scene creation failed.\\n";
        Renderer::Shutdown();
        runtime->Shutdown();
        return 1;
    }
    if (runtime->PrepareScene(demo.GetScene()) == false)
    {
        std::cerr << "DX12 Entity Scene mesh preparation failed.\\n";
        demo.Shutdown();
        Renderer::Shutdown();
        runtime->Shutdown();
        return 1;
    }

    uint32_t swapChainWidth = runtime->GetWidth();
    uint32_t swapChainHeight = runtime->GetHeight();
    RHISceneFrameLifecycle* frame = runtime->GetFrameLifecycle();
    if (frame == nullptr)
    {
        scene.reset();
        mesh.reset();
        material.reset();
        Renderer::Shutdown();
        runtime->Shutdown();
        return 1;
    }
    // ScopeをApplicationへ移譲してもRuntime実体のアドレスは変わりません。
    // Callbackは移譲元Scopeではなく、実体を借用するPointerを捕捉します。
    DX12SceneRuntime* runtimeHandle = runtime.get();
    Application::ExplicitSceneCallbacks callbacks;
    callbacks.OnScene = [&demo]() { demo.Render(); };
    callbacks.Resize = [&](uint32_t width, uint32_t height, bool force)
    {
        // Windowの通知サイズと実SwapChainサイズを分け、再生成後にCameraを同期します。
        if (force == true || swapChainWidth != width || swapChainHeight != height)
        {
            if (runtimeHandle->Resize(width, height) == false)
            {
                return false;
            }
            swapChainWidth = width;
            swapChainHeight = height;
            demo.ResizeCamera(width, height);
        }
        return true;
    };
    callbacks.OnBeforeShutdown = [&demo]() { demo.Shutdown(); };
    callbacks.DiscardPrepared = [runtimeHandle]() { runtimeHandle->DiscardPreparedFrame(); };
    callbacks.Prepare = [runtimeHandle]() { return runtimeHandle->PrepareFrame(); };
    callbacks.DrawPrepared = [runtimeHandle]() { return runtimeHandle->DrawPreparedFrame(); };
    const int exitCode = Application::RunOwnedExplicitScene(
        std::move(window), std::move(runtime), callbacks);
    return exitCode;
}

} // namespace Raven
