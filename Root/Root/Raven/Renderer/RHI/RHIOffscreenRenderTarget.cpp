#include "Raven/Renderer/RHI/RHIOffscreenRenderTarget.h"

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

    // Scene描画用のDepthは既存Backendと同じDepth24Stencil8、またはDepthなしを許可します。
    return specification.DepthFormat == RHITextureFormat::None ||
        specification.DepthFormat == RHITextureFormat::Depth24Stencil8;
}

} // namespace Raven
