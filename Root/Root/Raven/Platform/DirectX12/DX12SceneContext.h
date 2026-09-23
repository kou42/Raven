#pragma once

#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"

#include "DX12Adapter.h"
#include "DX12CommandList.h"
#include "DX12CommandQueue.h"
#include "DX12Device.h"
#include "DX12Factory.h"
#include "DX12Fence.h"
#include "DX12FrameRenderer.h"
#include "DX12SwapChain.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Raven
{
// Clear DemoとGPU Resourceを共有しないDX12 Scene専用Frame Contextです。
// Scene用RHICommandList/Pipelineが揃うまでApplication Factoryへは接続しません。
class DX12SceneContext final : public RHISceneFrameLifecycle
{
public:
    ~DX12SceneContext() override;

    bool Init(Window& window);
    RHIFrameResult BeginFrame() override;
    RHIFrameResult EndFrame() override;
    RHIFrameResult Present() override;
    bool Resize(uint32_t width, uint32_t height) override;
    void Shutdown();

    // Scene DrawのRasterizer領域とScissorを同時に設定します。
    bool SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    void SetClearColor(const float color[4]);
    ID3D12GraphicsCommandList* GetActiveCommandList() const;

    // Scene用Buffer/PipelineはClear DemoとDeviceを共有せず、このContextから生成します。
    // Context終了後に取得したnative pointerを保持しないでください。
    ID3D12Device* GetNativeDevice() const;

    // Draw命令が参照するBufferを対応FrameのFence完了まで保持します。
    bool RetainDrawBuffers(const Ref<RHIBuffer>& vertexBuffer,
        const Ref<RHIBuffer>& indexBuffer);

    // Pipeline/Root Signature設定後にのみ呼ぶnative Indexed Drawの記録入口です。
    // strideはPipelineのVertex Binding 0と一致させてください。
    bool DrawIndexed(const Ref<RHIBuffer>& vertexBuffer,
        const Ref<RHIBuffer>& indexBuffer, uint32_t stride,
        uint32_t indexCount = 0);

    // 現在のFrameで設定済みのPipelineを検証してから描画します。
    // PSOが未設定のDrawIndexedInstancedを発行しないための入口です。
    bool BindGraphicsPipeline(ID3D12PipelineState* pipelineState,
        ID3D12RootSignature* rootSignature);
    bool RetainGraphicsPipeline(const Ref<RHIGraphicsPipeline>& pipeline);
    bool IsGraphicsPipelineBound() const;
    bool SetClipTransform(const std::array<float, 16>& transform);
    bool SetMaterialTint(const std::array<float, 4>& tint);

private:
    struct FrameResource
    {
        std::unique_ptr<DX12CommandList> CommandList;
        uint64_t FenceValue = 0;
        std::vector<Ref<RHIBuffer>> RetainedBuffers;
        std::vector<Ref<RHIGraphicsPipeline>> RetainedPipelines;
    };

    DX12Factory m_Factory;
    DX12Adapter m_Adapters;
    DX12Device m_Device;
    DX12CommandQueue m_Queue;
    DX12SwapChain m_SwapChain;
    DX12Fence m_Fence;
    DX12FrameRenderer m_FrameRenderer;
    std::vector<FrameResource> m_Frames;
    uint32_t m_CurrentFrame = 0;
    float m_ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool m_VSync = true;
    bool m_FrameActive = false;
    bool m_FrameSubmitted = false;
    bool m_GraphicsPipelineBound = false;
};
} // namespace Raven
