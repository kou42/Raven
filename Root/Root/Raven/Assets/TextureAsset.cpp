#include "Raven/Assets/TextureAsset.h"

#include "Raven/Assets/TextureAssetImporter.h"

namespace Raven
{

TextureAsset::TextureAsset(std::string sourcePath, const Ref<Texture>& texture)
    : m_SourcePath(sourcePath), m_Texture(texture)
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