#pragma once

#include "DX12SceneCommandList.h"
#include "DX12SceneRHIDevice.h"

#include "Raven/Assets/RHIShaderAsset.h"
#include "Raven/Core/Window.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Renderer.h"
#include "Raven/Renderer/RHI/RHISceneDrawItemBuilder.h"
#include "Raven/Scene/Scene.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace Raven
{

// VulkanSceneRuntimeと同じ共通Renderer QueueをDX12 Sceneへ流すRuntimeです。
// native Resourceの生成・破棄はContext/Deviceに閉じ込めます。
class DX12SceneRuntime final
{
public:
    DX12SceneRuntime() = default;
    ~DX12SceneRuntime() { Shutdown(); }

    DX12SceneRuntime(const DX12SceneRuntime&) = delete;
    DX12SceneRuntime& operator=(const DX12SceneRuntime&) = delete;

    bool Init(Window& window,
        const PipelineSpecification& pipelineSpecification,
        const RHIShaderAssetSpecification& vertexShader,
        const RHIShaderAssetSpecification& fragmentShader)
    {
        Shutdown();
        if (window.GetBackend() != RHIBackend::DirectX12 ||
            pipelineSpecification.Topology != PrimitiveTopology::Triangles)
        {
            return false;
        }
        if (m_Context.Init(window) == false)
        {
            // 部分初期化されたDevice/SwapChainを次回Initへ持ち越しません。
            Shutdown();
            return false;
        }
        m_Window = &window;

        m_Device = CreateScope<DX12SceneRHIDevice>(m_Context);
        m_PipelineDebugName = pipelineSpecification.DebugName != nullptr ?
            pipelineSpecification.DebugName : "Unnamed Pipeline";
        m_PipelineSpecification = pipelineSpecification;
        m_PipelineSpecification.DebugName = m_PipelineDebugName.c_str();
        m_VertexShader = m_ShaderAssets.Load(vertexShader, RHIBackend::DirectX12);
        m_FragmentShader = m_ShaderAssets.Load(fragmentShader, RHIBackend::DirectX12);
        if (m_VertexShader == nullptr || m_FragmentShader == nullptr)
        {
            // Shader Assetは相対Pathで開くため、Visual Studioの作業Directoryも表示します。
            std::cerr << "[DX12 Scene] Shader load failed. Working directory: "
                << std::filesystem::current_path().string() << "\n"
                << "  VS: " << vertexShader.DirectX12Path << "\n"
                << "  PS: " << fragmentShader.DirectX12Path << "\n";
            Shutdown();
            return false;
        }
        if (CreatePipelines() == false)
        {
            std::cerr << "[DX12 Scene] Graphics Pipeline creation failed.\n";
            Shutdown();
            return false;
        }
        if (CreateDefaultTexture() == false)
        {
            std::cerr << "[DX12 Scene] Default white texture creation failed.\n";
            Shutdown();
            return false;
        }
        m_Initialized = true;
        return true;
    }

    bool PrepareMesh(const Ref<Mesh>& mesh)
    {
        if (m_Initialized == false || m_Device == nullptr ||
            mesh == nullptr || m_Context.GetActiveCommandList() != nullptr)
        {
            return false;
        }
        // 別Deviceで生成したBufferを誤用しないよう、このRuntimeで再構築します。
        return mesh->BuildRHIResources(*m_Device);
    }

    bool PrepareScene(Scene& scene)
    {
        if (m_Initialized == false || m_Device == nullptr)
        {
            return false;
        }
        return m_Device->PrepareScene(scene);
    }

    // Descriptor準備をBeginFrameより前に行い、Contextと同じRuntimeがSnapshotを保持します。
    void DiscardPreparedFrame()
    {
        // Acquire失敗時の準備済みResourceを再生成・終了前に解放します。
        m_PreparedFrame.reset();
    }

    bool PrepareFrame()
    {
        m_PreparedFrame.reset();
        if (m_Initialized == false || m_Device == nullptr ||
            m_OpaquePipeline == nullptr || m_TransparentPipeline == nullptr ||
            m_DefaultTexture == nullptr ||
            m_Context.GetActiveCommandList() != nullptr)
        {
            return false;
        }
        auto prepared = CreateScope<Renderer::PreparedRHISceneFrame>();
        if (Renderer::PrepareRHISceneFrame(*m_Device, m_OpaquePipeline,
            m_TransparentPipeline, m_DefaultTexture,
            DX12ClipCorrection(), *prepared) == false)
        {
            return false;
        }
        m_PreparedFrame = std::move(prepared);
        return true;
    }

    // Application等がBeginFrameを呼び出した後に使用します。二重Acquireは行いません。
    RHIFrameResult DrawPreparedFrame()
    {
        if (m_Initialized == false || m_PreparedFrame == nullptr ||
            m_Context.GetActiveCommandList() == nullptr)
        {
            return RHIFrameResult::FatalError;
        }
        DX12SceneCommandList commands(m_Context);
        const RHIFrameResult result = Renderer::DrawPreparedRHISceneFrame(
            m_Context, commands, *m_PreparedFrame);
        // GPU使用中のResourceはContext側のFrameResourceがFence完了まで保持します。
        m_PreparedFrame.reset();
        // FatalErrorでもDevice破棄は所有元Applicationの終了処理に委譲します。
        return result;
    }

    RHIFrameResult DrawFrame()
    {
        if (PrepareFrame() == false)
        {
            return RHIFrameResult::FatalError;
        }
        const RHIFrameResult begin = m_Context.BeginFrame();
        if (begin != RHIFrameResult::Success)
        {
            m_PreparedFrame.reset();
            return begin;
        }
        return DrawPreparedFrame();
    }

    bool Resize(uint32_t width, uint32_t height)
    {
        if (m_Initialized == false || width == 0 || height == 0)
        {
            return false;
        }
        // 旧Frame参照を外してからContext/SwapChainを再生成します。
        m_PreparedFrame.reset();
        if (m_Context.Resize(width, height) == false)
        {
            return false;
        }
        // Depth/RTV再生成後、Attachment形式に合うPSOを作り直します。
        m_OpaquePipeline.reset();
        m_TransparentPipeline.reset();
        if (CreatePipelines() == false)
        {
            // 所有元ApplicationがScene→Renderer→Runtimeの順に終了します。
            return false;
        }
        return true;
    }

    void Shutdown()
    {
        m_PreparedFrame.reset();
        // ContextのShutdownはQueueのFenceを待つため、参照を先に解放しても
        // FrameResourceが保持するGPU使用中Resourceは完了まで生存します。
        m_OpaquePipeline.reset();
        m_TransparentPipeline.reset();
        m_DefaultTexture.reset();
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_ShaderAssets.Clear();
        m_Device.reset();
        m_Context.Shutdown();
        m_PipelineSpecification = {};
        m_PipelineDebugName.clear();
        m_Window = nullptr;
        m_Initialized = false;
    }

    bool IsInitialized() const { return m_Initialized; }
    // Runtimeが所有するContextのFrame境界を公開します。Shutdown後は参照しないでください。
    RHISceneFrameLifecycle* GetFrameLifecycle()
    {
        return m_Initialized == true ? &m_Context : nullptr;
    }
    RHIDevice* GetDevice()
    {
        return m_Initialized == true ? m_Device.get() : nullptr;
    }
    const Ref<RHITexture>& GetDefaultTexture() const { return m_DefaultTexture; }
    uint32_t GetWidth() const
    {
        return m_Window != nullptr ? m_Window->GetFramebufferWidth() : 0;
    }
    uint32_t GetHeight() const
    {
        return m_Window != nullptr ? m_Window->GetFramebufferHeight() : 0;
    }

private:
    static math::Mat4 DX12ClipCorrection()
    {
        // RavenのNDC Z[-1,1]をDX12の[0,1]へ変換します。
        // Vulkanと異なりViewportのY反転は行いません。
        return math::Mat4(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.5f, 0.5f,
            0.0f, 0.0f, 0.0f, 1.0f);
    }

    bool CreatePipelines()
    {
        if (m_Device == nullptr || m_VertexShader == nullptr ||
            m_FragmentShader == nullptr ||
            m_VertexShader->IsValid() == false ||
            m_FragmentShader->IsValid() == false)
        {
            return false;
        }
        Ref<RHIGraphicsPipeline> opaque;
        Ref<RHIGraphicsPipeline> transparent;
        if (Renderer::CreateRHIScenePipelines(*m_Device,
            m_PipelineSpecification, m_VertexShader->GetBinary(),
            m_FragmentShader->GetBinary(), opaque, transparent) == false)
        {
            return false;
        }
        m_OpaquePipeline = std::move(opaque);
        m_TransparentPipeline = std::move(transparent);
        return true;
    }

    bool CreateDefaultTexture()
    {
        if (m_Device == nullptr)
        {
            return false;
        }
        RHITextureSpecification specification{};
        specification.Width = 1;
        specification.Height = 1;
        specification.Format = RHITextureFormat::RGBA8;
        specification.Usage = RHITextureUsage::Sampled;
        specification.GenerateMips = false;
        specification.DebugName = "DX12 Scene Default White";
        constexpr std::array<uint8_t, 4> white = {255u, 255u, 255u, 255u};
        m_DefaultTexture = m_Device->CreateTexture(
            specification, white.data(), white.size());
        return m_DefaultTexture != nullptr;
    }

    Window* m_Window = nullptr;
    DX12SceneContext m_Context;
    Scope<DX12SceneRHIDevice> m_Device;
    RHIShaderAssetManager m_ShaderAssets;
    Ref<RHIShaderAsset> m_VertexShader;
    Ref<RHIShaderAsset> m_FragmentShader;
    PipelineSpecification m_PipelineSpecification{};
    std::string m_PipelineDebugName;
    Ref<RHIGraphicsPipeline> m_OpaquePipeline;
    Ref<RHIGraphicsPipeline> m_TransparentPipeline;
    Ref<RHITexture> m_DefaultTexture;
    Scope<Renderer::PreparedRHISceneFrame> m_PreparedFrame;
    bool m_Initialized = false;
};

} // namespace Raven
