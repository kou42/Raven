#pragma once

#include "Raven/Assets/RHIShaderAsset.h"
#include "Raven/Platform/Vulkan/VulkanSceneContext.h"

namespace Raven
{

class Mesh;
class Scene;
class RHIDevice;
class VulkanSceneRHIDevice;

// 通常Renderer QueueをVulkan Explicit Sceneへ流すためのRuntime所有境界です。
// Context / Device / Shader Asset / Pipeline / 既定Textureの終了順序を一か所で管理します。
class VulkanSceneRuntime final
{
public:
    VulkanSceneRuntime();
    ~VulkanSceneRuntime();

    VulkanSceneRuntime(const VulkanSceneRuntime&) = delete;
    VulkanSceneRuntime& operator=(const VulkanSceneRuntime&) = delete;

    bool Init(
        Window& window,
        const PipelineSpecification& pipelineSpecification,
        const RHIShaderAssetSpecification& vertexShader,
        const RHIShaderAssetSpecification& fragmentShader);

    // Frame開始前に通常MeshのExplicit RHI BufferをこのRuntimeのDeviceで構築し直します。
    // Context再生成後も古いDeviceのBufferを再利用しないため、呼び出し側は登録時に一度実行します。
    bool PrepareMesh(const Ref<Mesh>& mesh);

    // SceneのEntityが共有するMeshをFrame開始前に一括準備します。
    bool PrepareScene(Scene& scene);

    // Renderer::BeginScene()以降に蓄積された通常Queueを消費し、
    // Texture準備からPresentまでを一つのExplicit Frameとして実行します。
    RHIFrameResult DrawFrame();

    // SwapChain再生成後にRender Target依存Pipelineも作り直します。
    bool Resize(uint32_t width, uint32_t height);

    void Shutdown();

    bool IsInitialized() const;
    // Runtimeが所有するScene Frame境界を借用します。Window/Contextの所有権は移しません。
    // ApplicationへFrame管理を統合する際の共通入口です。Init成功後だけ使用してください。
    RHISceneFrameLifecycle* GetFrameLifecycle();
    RHIDevice* GetDevice();
    const Ref<RHITexture>& GetDefaultTexture() const;
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;

private:
    bool CreatePipelines();
    bool CreateDefaultTexture();

private:
    Window* m_Window = nullptr;
    VulkanSceneContext m_Context;
    Scope<VulkanSceneRHIDevice> m_Device;
    RHIShaderAssetManager m_ShaderAssets;
    Ref<RHIShaderAsset> m_VertexShader;
    Ref<RHIShaderAsset> m_FragmentShader;
    PipelineSpecification m_PipelineSpecification;
    std::string m_PipelineDebugName;
    Ref<RHIGraphicsPipeline> m_OpaquePipeline;
    Ref<RHIGraphicsPipeline> m_TransparentPipeline;
    Ref<RHITexture> m_DefaultTexture;
    bool m_Initialized = false;
};

} // namespace Raven
