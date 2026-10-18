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

    return true;
}

} // namespace Raven
