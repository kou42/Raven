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
    if (runtime->Init(
        *window,
        pipelineSpecification,
        vertexShader,
        fragmentShader) == false)
    {
        std::cerr << "Vulkan Scene Runtime initialization failed.\n";
        return 1;
    }

    // 通常Sceneと同じECS経路で複数Entityを登録します。
    // Explicit-only起動なのでOpenGL VAO/VBOは生成せず、CPU Geometryだけを持ちます。
    auto scene = CreateScope<Scene>();
    Ref<Mesh> mesh = PrimitiveMeshFactory::CreateCube(
        LegacyMeshResourceCreation::Deferred);
    Ref<Material> material = CreateRef<Material>();
    if (mesh == nullptr || mesh->GetVertexArray() != nullptr || material == nullptr ||
        material->HasLegacyPipeline() == true)
    {
        std::cerr << "Vulkan Entity Scene creation failed.\n";
        scene.reset();
        mesh.reset();
        material.reset();
        Renderer::Shutdown();
        runtime->Shutdown();
        return 1;
    }
    material->SetRHITint({0.35f, 0.75f, 1.0f, 1.0f});
    material->SetSurfaceType(MaterialSurfaceType::Opaque);

    Entity left = scene->CreateEntity("VulkanLeftCube");
    left.GetComponent<TransformComponent>().Position = {-1.2f, 0.0f, 0.0f};
    left.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh, material});

    Entity center = scene->CreateEntity("VulkanCenterCube");
    center.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh, material});

    Entity right = scene->CreateEntity("VulkanRightCube");
    right.GetComponent<TransformComponent>().Position = {1.2f, 0.0f, 0.0f};
    right.AddComponent<MeshRendererComponent>(MeshRendererComponent{mesh, material});

    // 3 Entityは同じMeshを共有します。Bufferの生成は1回だけです。
    if (runtime->PrepareScene(*scene) == false)
    {
        std::cerr << "Vulkan Entity Scene mesh preparation failed.\n";
        scene.reset();
        mesh.reset();
        material.reset();
        Renderer::Shutdown();
        runtime->Shutdown();
        return 1;
    }

    SceneCamera camera;
    camera.SetViewMatrix(math::Mat4::LookAt(
        {0.0f, 0.0f, 5.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}));
    camera.SetViewportSize(
        static_cast<float>(runtime->GetWidth()),
        static_cast<float>(runtime->GetHeight()));

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
    VulkanSceneRuntime* runtimeHandle = runtime.get();
    Application::ExplicitSceneCallbacks callbacks;
    callbacks.OnScene = [&]()
    {
        Renderer::BeginScene(camera);
        const float time = static_cast<float>(glfwGetTime());
        // SceneのEntityを更新し、Renderer Queueへの登録は既存ECS経路を使用します。
        center.GetComponent<TransformComponent>().Rotation.y = time * 0.6f;
        left.GetComponent<TransformComponent>().Rotation.x = -time * 0.35f;
        right.GetComponent<TransformComponent>().Rotation.y = -time * 0.4f;
        scene->RenderEntities();
    };
    callbacks.Resize = [&](uint32_t width, uint32_t height, bool force)
    {
        // Windowの通知サイズと実SwapChainサイズを分け、再生成後にCameraを同期します。
        if (force == true || runtimeHandle->GetWidth() != width || runtimeHandle->GetHeight() != height)
        {
            if (runtimeHandle->Resize(width, height) == false)
            {
                return false;
            }
            camera.SetViewportSize(static_cast<float>(width),
                static_cast<float>(height));
        }
        return true;
    };
    callbacks.OnBeforeShutdown = [&]()
    {
        // Entityが所有するMesh/MaterialをDeviceのShutdownより先に解放します。
        // Runtimeへ移譲した後もSceneの寿命を明示的に短く保ちます。
        scene.reset();
        mesh.reset();
        material.reset();
    };
    callbacks.Prepare = [runtimeHandle]() { return runtimeHandle->PrepareFrame(); };
    callbacks.DrawPrepared = [runtimeHandle]() { return runtimeHandle->DrawPreparedFrame(); };
    const int exitCode = Application::RunOwnedExplicitScene(
        std::move(window), std::move(runtime), callbacks);
    return exitCode;
}

} // namespace Raven
