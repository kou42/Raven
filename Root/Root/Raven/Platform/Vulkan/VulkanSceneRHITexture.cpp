#include "VulkanSceneRHITexture.h"

namespace Raven
{

bool VulkanSceneRHITexture::InitRenderTarget(const VulkanDevice& device,
    const RHITextureSpecification& specification, VkFormat nativeFormat)
{
    if (specification.Format != RHITextureFormat::RGBA8 ||
        specification.Usage != RHITextureUsage::RenderTarget ||
        specification.GenerateMips == true ||
        specification.Width == 0u || specification.Height == 0u)
    {
        return false;
    }
    if (m_Texture.InitRenderTarget(device, specification.Width,
        specification.Height, nativeFormat) == false)
    {
        return false;
    }
    m_Specification = specification;
    return true;
}

} // namespace Raven
