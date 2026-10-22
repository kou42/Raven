#include "Raven/Renderer/RHI/RHIOffscreenRenderTarget.h"

#include <limits>

namespace Raven
{

bool IsValidRHIOffscreenRenderTargetSpecification(
    const RHIOffscreenRenderTargetSpecification& specification)
{
    if (specification.Width == 0u || specification.Height == 0u)
    {
        return false;
    }

    // この段階ではRaven UIがそのままSamplingできるRGBA8 Colorだけを正式対応とします。
    // HDRや複数Color Attachmentは、Pipeline互換性とTone Mappingの規約を決めてから拡張します。
    if (specification.ColorFormat != RHITextureFormat::RGBA8)
    {
        return false;
    }

    // Picking Passは通常Color描画済みDepthを再利用して可視面だけを識別します。
    // Depthなしで別の選択結果になるTargetは共通契約として受理しません。
    if (specification.EnableEntityPicking == true &&
        specification.EnableDepth == false)
    {
        return false;
    }

    return true;
}

bool RHIEntityPickingReadbackState::Request(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t x,
    std::uint32_t y,
    std::uint64_t& outSequence)
{
    if (width == 0u || height == 0u || x >= width || y >= height)
    {
        return false;
    }

    // 0は未発行Tokenとして予約します。実運用でwrapした場合も1へ戻し、
    // 同一Target内の最新要求比較を継続できるようにします。
    if (m_LatestSequence == std::numeric_limits<std::uint64_t>::max())
    {
        m_LatestSequence = 1u;
    }
    else
    {
        ++m_LatestSequence;
    }

    m_PendingRequest = { m_LatestSequence, x, y };
    m_HasPendingRequest = true;
    // 新しいClickが来た時点で、まだ消費されていない古い結果は無効です。
    m_HasResult = false;
    outSequence = m_LatestSequence;
    return true;
}

bool RHIEntityPickingReadbackState::TakePendingRequest(
    RHIEntityPickingRequest& outRequest)
{
    if (m_HasPendingRequest == false)
    {
        return false;
    }
    outRequest = m_PendingRequest;
    m_HasPendingRequest = false;
    return true;
}

bool RHIEntityPickingReadbackState::PublishResult(
    std::uint64_t sequence,
    std::int32_t entityID)
{
    if (sequence == 0u || sequence != m_LatestSequence)
    {
        // Resize前や新しいClickより古いGPU完了結果は選択へ反映しません。
        return false;
    }
    m_ResultSequence = sequence;
    m_ResultEntityID = entityID;
    m_HasResult = true;
    return true;
}

bool RHIEntityPickingReadbackState::ConsumeResult(
    std::uint64_t& outSequence,
    std::int32_t& outEntityID)
{
    if (m_HasResult == false)
    {
        return false;
    }
    outSequence = m_ResultSequence;
    outEntityID = m_ResultEntityID;
    m_HasResult = false;
    return true;
}

void RHIEntityPickingReadbackState::Reset()
{
    m_PendingRequest = {};
    m_LatestSequence = 0u;
    m_ResultSequence = 0u;
    m_ResultEntityID = -1;
    m_HasPendingRequest = false;
    m_HasResult = false;
}

bool RHIOffscreenRenderTarget::SupportsEntityPicking() const
{
    return false;
}

bool RHIOffscreenRenderTarget::RequestEntityPicking(
    std::uint32_t x,
    std::uint32_t y,
    std::uint64_t& outSequence)
{
    (void)x;
    (void)y;
    (void)outSequence;
    return false;
}

bool RHIOffscreenRenderTarget::ConsumeEntityPickingResult(
    std::uint64_t& outSequence,
    std::int32_t& outEntityID)
{
    (void)outSequence;
    (void)outEntityID;
    return false;
}

} // namespace Raven
