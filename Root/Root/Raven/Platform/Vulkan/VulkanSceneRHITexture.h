#pragma once

#include "Raven/Renderer/RHI/RHITexture.h"
#include "VulkanSceneTexture.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace Raven
{

// RHITextureからScene用VkImageへのBridgeです。
// DescriptorはScene側で管理し、Texture本体の所有権だけをRHIに統一します。
class VulkanSceneRHITexture final : public RHITexture
{
public:
    VulkanSceneRHITexture() = default;
    ~VulkanSceneRHITexture() override = default;

    bool Init(const VulkanDevice& device,
        const RHITextureSpecification& specification,
        const void* initialData, std::size_t initialDataSize)
    {
        if (specification.Format != RHITextureFormat::RGBA8 ||
            specification.Usage != RHITextureUsage::Sampled ||
            specification.GenerateMips == true ||
            specification.Width == 0 || specification.Height == 0 ||
            initialData == nullptr ||
            static_cast<uint64_t>(specification.Width) * specification.Height >
                std::numeric_limits<std::size_t>::max() / 4 ||
            initialDataSize !=
                static_cast<std::size_t>(specification.Width) * specification.Height * 4)
        {
            return false;
        }
        if (m_Texture.Init(device, specification.Width, specification.Height,
            static_cast<const uint8_t*>(initialData)) == false)
        {
            return false;
        }
        m_Specification = specification;
        return true;
    }

    void SetData(const void* data, std::size_t dataSize) override
    {
        // Descriptorが参照するImageViewを途中で差し替えないため、更新は現段階では未対応です。
        // Textureの再登録はSceneのAddTextureから行い、将来の更新APIで同期を統一します。
        (void)data;
        (void)dataSize;
    }

    const RHITextureSpecification& GetSpecification() const override
    {
        return m_Specification;
    }

    const VulkanSceneTexture& GetNativeTexture() const { return m_Texture; }
    void Shutdown() { m_Texture.Shutdown(); }

private:
    RHITextureSpecification m_Specification;
    VulkanSceneTexture m_Texture;
};

} // namespace Raven
