#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHIResource.h"
#include "Raven/Renderer/RHI/RHITexture.h"

#include <array>
#include <cstdint>
#include <string>

namespace Raven
{

// Scene View / Game View等、SwapChainとは独立した描画先の共通設定です。
// Color Attachmentは描画完了後にSampled TextureとしてUIから参照できることを契約に含めます。
// Native RTV / VkFramebufferやResource State / Image LayoutはBackend実装内へ閉じ込めます。
struct RHIOffscreenRenderTargetSpecification
{
    std::uint32_t Width = 1u;
    std::uint32_t Height = 1u;
    RHITextureFormat ColorFormat = RHITextureFormat::RGBA8;
    // 具体的なDepth形式はRHIDevice::GetGraphicsPipelineTarget()と一致させます。
    // EditorはDepth Textureを表示しないため、Backend形式を上位層へ公開しません。
    bool EnableDepth = true;
    // Scene ViewだけがEntity Pickingを必要とします。Game Viewや通常のoff-screen描画では
    // falseのままとし、整数Attachment／Readback Bufferを余分に確保しません。
    bool EnableEntityPicking = false;
    // Resource生成時の最適化Clear値と実描画時のClearを一致させます。
    // 色を変更する場合はTarget交換が必要です。
    std::array<float, 4> ClearColor = { 0.1f, 0.1f, 0.3f, 1.0f };
    std::string DebugName;
};

bool IsValidRHIOffscreenRenderTargetSpecification(
    const RHIOffscreenRenderTargetSpecification& specification);

// Explicit Backendの非同期Picking要求をFrame境界へ渡すための値です。
// SequenceはResizeや連続Clickで古いReadback結果を捨てるために使用します。
struct RHIEntityPickingRequest
{
    std::uint64_t Sequence = 0u;
    std::uint32_t X = 0u;
    std::uint32_t Y = 0u;
};

// GPU APIに依存しないPicking要求／完了結果のMailboxです。
// 最新Clickだけを有効にすることで、Readbackが数Frame遅延しても古い選択へ戻りません。
// BackendはFrame記録前にTakePendingRequest()し、Fence完了後にPublishResult()します。
class RHIEntityPickingReadbackState final
{
public:
    bool Request(std::uint32_t width, std::uint32_t height,
        std::uint32_t x, std::uint32_t y, std::uint64_t& outSequence);
    bool TakePendingRequest(RHIEntityPickingRequest& outRequest);
    bool PublishResult(std::uint64_t sequence, std::int32_t entityID);
    bool ConsumeResult(std::uint64_t& outSequence, std::int32_t& outEntityID);
    void Reset();

private:
    RHIEntityPickingRequest m_PendingRequest{};
    std::uint64_t m_LatestSequence = 0u;
    std::uint64_t m_ResultSequence = 0u;
    std::int32_t m_ResultEntityID = -1;
    bool m_HasPendingRequest = false;
    bool m_HasResult = false;
};

// Explicit Backend共通のoff-screen描画先です。
//
// Ownership:
// - RHIDeviceが生成し、Explicit RuntimeがRefで所有します。
// - Targetの寸法は生成後不変です。Resize時はTarget全体を新規生成してRefを交換し、
//   UIDrawListとFrame Fenceが保持する旧Target／Textureの生存期間を壊しません。
// - Begin/End時の状態遷移はRHISceneCommandListが担当し、Editor層はnative handleを扱いません。
class RHIOffscreenRenderTarget : public RHIResource
{
public:
    ~RHIOffscreenRenderTarget() override = default;

    virtual const RHIOffscreenRenderTargetSpecification& GetSpecification() const = 0;
    virtual const Ref<RHITexture>& GetColorAttachment() const = 0;
    virtual bool IsValid() const = 0;

    // Picking対応はBackendごとに段階導入します。未対応Targetは要求を受理せず、
    // Editorが同期Readbackやnative handleへ暗黙fallbackしないよう明示します。
    virtual bool SupportsEntityPicking() const;
    virtual bool RequestEntityPicking(std::uint32_t x, std::uint32_t y,
        std::uint64_t& outSequence);
    virtual bool ConsumeEntityPickingResult(
        std::uint64_t& outSequence, std::int32_t& outEntityID);

protected:
    RHIOffscreenRenderTarget() = default;
};

} // namespace Raven
