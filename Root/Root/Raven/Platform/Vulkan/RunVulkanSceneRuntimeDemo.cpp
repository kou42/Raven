#include "RunVulkanSceneRuntimeDemo.h"

#include "VulkanSceneRuntime.h"

#include "Raven/Core/Window.h"
#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"
#include "Raven/Scene/SceneCamera.h"

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

    VulkanSceneRuntime runtime;
    if (runtime.Init(
        *window,
        pipelineSpecification,
        vertexShader,
        fragmentShader) == false)
    {
        std::cerr << "Vulkan Scene Runtime initialization failed.\n";
        return 1;
    }

    // Explicit-only起動ではMesh生成時にOpenGL VAO/VBOへ触れません。
    // Runtimeへ登録した時点で、このVulkan Deviceに属するRHI Bufferだけを構築します。
    Ref<Mesh> mesh = PrimitiveMeshFactory::CreateCube(
        LegacyMeshResourceCreation::Deferred);
    if (mesh == nullptr || mesh->GetVertexArray() != nullptr ||
        runtime.PrepareMesh(mesh) == false)
    {
        std::cerr << "Vulkan Scene Runtime mesh preparation failed.\n";
        runtime.Shutdown();
        return 1;
    }

    // Explicit Scene QueueはMaterialのRHI値だけを参照するため、Legacy Pipelineは不要です。
    Ref<Material> material = CreateRef<Material>();
    if (material == nullptr || material->HasLegacyPipeline() == true)
    {
        std::cerr << "Vulkan Scene Runtime material creation failed.\n";
        runtime.Shutdown();
        return 1;
    }
    material->SetRHITint({0.35f, 0.75f, 1.0f, 1.0f});
    material->SetSurfaceType(MaterialSurfaceType::Opaque);

    SceneCamera camera;
    camera.SetViewMatrix(math::Mat4::LookAt(
        {0.0f, 0.0f, 2.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}));
    camera.SetViewportSize(
        static_cast<float>(runtime.GetWidth()),
        static_cast<float>(runtime.GetHeight()));

    GLFWwindow* nativeWindow = static_cast<GLFWwindow*>(window->GetNativeWindow());
    int exitCode = 0;
    while (glfwWindowShouldClose(nativeWindow) == GLFW_FALSE)
    {
        window->PollEvents();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(nativeWindow, &width, &height);
        if (width <= 0 || height <= 0)
        {
            // 最小化中は0サイズのSwapChainを生成せず、入力待ちでCPU消費も抑えます。
            glfwWaitEvents();
            continue;
        }

        if (runtime.GetWidth() != static_cast<uint32_t>(width) ||
            runtime.GetHeight() != static_cast<uint32_t>(height))
        {
            if (runtime.Resize(
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)) == false)
            {
                exitCode = 1;
                break;
            }
            camera.SetViewportSize(
                static_cast<float>(width),
                static_cast<float>(height));
        }

        Renderer::BeginFrame();
        Renderer::BeginScene(camera);
        const float time = static_cast<float>(glfwGetTime());
        const math::Mat4 model =
            math::Mat4::RotationY(time * 0.6f) *
            math::Mat4::RotationX(-0.35f);
        Renderer::Draw(mesh, material, model);

        const RHIFrameResult result = runtime.DrawFrame();
        if (result == RHIFrameResult::ResizeRequired)
        {
            if (runtime.Resize(
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)) == false)
            {
                exitCode = 1;
                break;
            }
            camera.SetViewportSize(
                static_cast<float>(width),
                static_cast<float>(height));
        }
        else if (result != RHIFrameResult::Success)
        {
            exitCode = 1;
            break;
        }
    }

    Renderer::Shutdown();
    runtime.Shutdown();
    return exitCode;
}

} // namespace Raven
