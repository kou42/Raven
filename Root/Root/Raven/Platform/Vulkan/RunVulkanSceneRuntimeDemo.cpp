#include "RunVulkanSceneRuntimeDemo.h"

#include "VulkanSceneRuntime.h"

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

int RunVulkanSceneRuntimeDemo()
{
    auto window = Window::Create(WindowProps(
        "Raven Vulkan Scene Runtime", 1280, 720, RHIBackend::Vulkan));
    if (window == nullptr || window->GetNativeWindow() == nullptr)
    {
        return 1;
    }

    const std::filesystem::path shaderDirectory =
        std::filesystem::path("Raven") / "Assets" / "Shaders";
    RHIShaderAssetSpecification vertexShader{};
    vertexShader.VulkanPath =
        (shaderDirectory / "Vulkan" / "SceneTriangle.vert.spv").generic_string();
    RHIShaderAssetSpecification fragmentShader{};
    fragmentShader.VulkanPath =
        (shaderDirectory / "Vulkan" / "SceneTriangle.frag.spv").generic_string();

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.Blend = false;
    pipelineSpecification.DebugName = "Vulkan Normal Mesh Scene";

    auto runtime = CreateScope<VulkanSceneRuntime>();
    ExplicitCubeSceneDemo demo;

    // Shader/Pipeline設定はBackendごとに保持し、初期化・失敗時の解放順序は共通化します。
    Application::ExplicitSceneHooks hooks;
    hooks.OnScene = [&demo]() { demo.Render(); };
    hooks.OnResizeCamera = [&demo](uint32_t width, uint32_t height)
    {
        demo.ResizeCamera(width, height);
    };
    hooks.OnBeforeShutdown = [&demo]() { demo.Shutdown(); };

    const auto initializeScene = [&demo](IExplicitSceneRuntime& sceneRuntime)
    {
        if (demo.Init("Vulkan", sceneRuntime.GetWidth(), sceneRuntime.GetHeight()) == false)
        {
            std::cerr << "Vulkan Entity Scene creation failed.\n";
            return false;
        }
        if (sceneRuntime.PrepareScene(demo.GetScene()) == false)
        {
            std::cerr << "Vulkan Entity Scene mesh preparation failed.\n";
            return false;
        }
        return true;
    };

    return Application::RunInitializedExplicitScene(
        std::move(window), std::move(runtime),
        pipelineSpecification, vertexShader, fragmentShader,
        hooks, initializeScene);
}

} // namespace Raven
