#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "Raven/Renderer/RHI/RHIResource.h"

namespace Raven
{

// GPU Textureのピクセル形式をRHI共通値として表します。
// Graphics API固有のGLenum / DXGI_FORMAT / VkFormatはBackend実装側で変換します。
enum class RHITextureFormat
{
    None = 0,
    R8,
    RGB8,
    RGBA8,
    R32I,
    Depth24Stencil8
};

// Textureの利用目的を明示し、Backendが適切な生成方法やstateを選択できるようにします。
enum class RHITextureUsage
{
    None = 0,
    Sampled,
    RenderTarget,
    DepthStencil
};

struct RHITextureSpecification
{
    std::uint32_t Width = 1;
    std::uint32_t Height = 1;
    RHITextureFormat Format = RHITextureFormat::RGBA8;
    RHITextureUsage Usage = RHITextureUsage::Sampled;
    bool GenerateMips = true;
    std::string DebugName;
};

// TextureのGPU Resource本体をGraphics APIから分離するためのRHI境界です。
// 現段階では2D Texture全体の更新を対象とし、Samplerや部分更新は後続実装で追加します。
class RHITexture : public RHIResource
{
public:
    ~RHITexture() override = default;

    virtual void SetData(const void* data, std::size_t dataSize) = 0;
    // 明示診断経路です。既存のvoid SetData呼び出しは維持します。
    virtual bool TrySetData(const void* data, std::size_t dataSize) = 0;
    virtual const RHITextureSpecification& GetSpecification() const = 0;
};

} // namespace Raven
