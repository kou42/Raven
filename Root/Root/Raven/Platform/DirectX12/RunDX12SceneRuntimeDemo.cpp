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
    DX12SceneRuntime runtime;
    if (runtime.Init(
        *window,
        pipelineSpecification,
        vertexShader,
        fragmentShader) == false)
    {
        std::cerr << "DX12 Scene Runtime initialization failed.\n";
        return 1;
    }

    std::cout << "[DX12 Scene Demo] Runtime initialized.\n" << std::flush;

    // 通常Sceneと同じECS経路で複数Entityを登録します。
    // Explicit-only起動なのでOpenGL VAO/VBOは生成せず、CPU Geometryだけを持ちます。
    Scene scene;
    Ref<Mesh> mesh = PrimitiveMeshFactory::CreateCube(
        LegacyMeshResourceCreation::Deferred);
    Ref<Material> material = CreateRef<Material>();
    if (mesh == nullptr || mesh->GetVertexArray() != nullptr || material == nullptr ||
        material->HasLegacyPipeline() == true)
    {
        std::cerr << "DX12 Entity Scene creation failed.\n";
        runtime.Shutdown();
        return 1;
    }
    material->SetRHITint({0.35f, 0.75f, 1.0f, 1.0f});
    material->SetSurfaceType(MaterialSurfaceType::Opaque);

    Entity left = scene.CreateEntity("DX12LeftCube");
    left.GetComponent<TransformComponent>().Position = {-1.2f, 0.0f, 0.0f};
    left.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh, material});

    Entity center = scene.CreateEntity("DX12CenterCube");
    center.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh, material});

    Entity right = scene.CreateEntity("DX12RightCube");
    right.GetComponent<TransformComponent>().Position = {1.2f, 0.0f, 0.0f};
    right.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh, material});

    // 3 Entityは同じMeshを共有します。Bufferの生成は1回だけです。
    std::cout << "[DX12 Scene Demo] Preparing 3 Cube entities...\n" << std::flush;
    if (runtime.PrepareScene(scene) == false)
    {
        std::cerr << "DX12 Entity Scene mesh preparation failed.\n";
        runtime.Shutdown();
        return 1;
    }

    std::cout << "[DX12 Scene Demo] Mesh preparation succeeded.\n" << std::flush;

    SceneCamera camera;
    camera.SetViewMatrix(math::Mat4::LookAt(
        {0.0f, 0.0f, 5.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}));
    camera.SetViewportSize(
        static_cast<float>(runtime.GetWidth()),
        static_cast<float>(runtime.GetHeight()));

    // Window寸法はResize後に更新されるため、実際のSwapChain寸法を別に追跡します。
    uint32_t swapChainWidth = runtime.GetWidth();
    uint32_t swapChainHeight = runtime.GetHeight();
    GLFWwindow* nativeWindow = static_cast<GLFWwindow*>(window->GetNativeWindow());
    int exitCode = 0;
    bool firstFrame = true;
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

        if (swapChainWidth != static_cast<uint32_t>(width) ||
            swapChainHeight != static_cast<uint32_t>(height))
        {
            if (runtime.Resize(
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)) == false)
            {
                exitCode = 1;
                break;
            }
            swapChainWidth = static_cast<uint32_t>(width);
            swapChainHeight = static_cast<uint32_t>(height);
            camera.SetViewportSize(
                static_cast<float>(width),
                static_cast<float>(height));
        }

        Renderer::BeginFrame();
        Renderer::BeginScene(camera);
        const float time = static_cast<float>(glfwGetTime());
        // EntityのTransformを毎Frame更新し、ECSの描画入口から通常Queueへ送ります。
        center.GetComponent<TransformComponent>().Rotation.y = time * 0.6f;
        left.GetComponent<TransformComponent>().Rotation.x = -time * 0.35f;
        right.GetComponent<TransformComponent>().Rotation.y = -time * 0.4f;
        scene.RenderEntities();

        RHISceneFrameLifecycle* frame = runtime.GetFrameLifecycle();
        if (frame == nullptr)
        {
            exitCode = 1;
            break;
        }
        // Applicationの共通進行を使用し、Descriptor準備→Acquire→描画の順序を保証します。
        const RHIFrameResult result = Application::ExecuteExplicitSceneFrame(
            *frame,
            [&runtime]() { return runtime.PrepareFrame(); },
            [&runtime]() { return runtime.DrawPreparedFrame(); });
        if (firstFrame == true)
        {
            std::cout << "[DX12 Scene Demo] First frame result: "
                << static_cast<int>(result) << "\\n" << std::flush;
            firstFrame = false;
        }
        if (result == RHIFrameResult::ResizeRequired)
        {
            if (runtime.Resize(
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)) == false)
            {
                exitCode = 1;
                break;
            }
            swapChainWidth = static_cast<uint32_t>(width);
            swapChainHeight = static_cast<uint32_t>(height);
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
