#include "Raven/Assets/TextureAsset.h"

#include "Raven/Assets/TextureAssetImporter.h"
#include "Raven/Renderer/RHI/RHIDevice.h"

#include <utility>

namespace Raven
{
namespace
{
RHITextureFormat ToRHITextureFormat(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8: return RHITextureFormat::R8;
    case TextureFormat::RGB8: return RHITextureFormat::RGB8;
    case TextureFormat::RGBA8: return RHITextureFormat::RGBA8;
    case TextureFormat::R32I: return RHITextureFormat::R32I;
    case TextureFormat::Depth24Stencil8: return RHITextureFormat::Depth24Stencil8;
    case TextureFormat::None:
    default: return RHITextureFormat::None;
    }
}
} // namespace

bool TextureAssetPixelData::IsValid() const
{
    return Width > 0u && Height > 0u && Format != TextureFormat::None &&
        Pixels.empty() == false;
}


TextureAsset::TextureAsset(std::string sourcePath, const Ref<Texture>& texture)
    : m_SourcePath(std::move(sourcePath)), m_Texture(texture)
{
}

TextureAsset::TextureAsset(std::string sourcePath, const Ref<Texture>& texture,
    TextureAssetPixelData pixelData)
    : m_SourcePath(std::move(sourcePath)), m_Texture(texture),
      m_PixelData(std::move(pixelData))
{
}

const std::string& TextureAsset::GetSourcePath() const
{
    return m_SourcePath;
}

const Ref<Texture>& TextureAsset::GetTexture() const
{
    return m_Texture;
}

bool TextureAsset::IsValid() const
{
    return m_Texture != nullptr && m_Texture->GetID() != 0;
}

bool TextureAsset::HasPixelData() const
{
    return m_PixelData.IsValid();
}

const TextureAssetPixelData& TextureAsset::GetPixelData() const
{
    return m_PixelData;
}

Ref<RHITexture> TextureAsset::CreateRHITexture(RHIDevice& device) const
{
    if (m_PixelData.IsValid() == false)
    {
        return nullptr;
    }

    const RHITextureFormat format = ToRHITextureFormat(m_PixelData.Format);
    if (format == RHITextureFormat::None)
    {
        return nullptr;
    }

    RHITextureSpecification specification{};
    specification.Width = m_PixelData.Width;
    specification.Height = m_PixelData.Height;
    specification.Format = format;
    specification.Usage = RHITextureUsage::Sampled;
    specification.GenerateMips = m_PixelData.GenerateMips;
    specification.DebugName = m_SourcePath;

    return device.CreateTexture(
        specification, m_PixelData.Pixels.data(), m_PixelData.Pixels.size());
}

Ref<TextureAsset> TextureAssetManager::Load(const std::string& sourcePath)
{
    auto it = m_Assets.find(sourcePath);
    if (it != m_Assets.end())
    {
        return it->second;
    }

    Ref<TextureAsset> asset = TextureAssetImporter::Import(sourcePath);
    if (asset == nullptr)
    {
        return nullptr;
    }

    m_Assets[sourcePath] = asset;
    return asset;
}

Ref<TextureAsset> TextureAssetManager::Get(const std::string& sourcePath) const
{
    auto it = m_Assets.find(sourcePath);
    if (it == m_Assets.end())
    {
        return nullptr;
    }

    return it->second;
}

bool TextureAssetManager::Exists(const std::string& sourcePath) const
{
    return m_Assets.find(sourcePath) != m_Assets.end();
}

void TextureAssetManager::Clear()
{
    m_Assets.clear();
}

}