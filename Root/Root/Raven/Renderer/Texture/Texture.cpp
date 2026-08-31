#include "Raven/Renderer/Texture/Texture.h"

#include "Raven/Renderer/RendererAPI.h"
#include "Raven/Renderer/Texture/OpenGLTexture.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace Raven
{

namespace
{

std::string NormalizeExtension(const std::string& extension)
{
    std::string normalized = extension;
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        }
    );

    if (normalized.empty() == false && normalized.front() != '.')
    {
        normalized.insert(normalized.begin(), '.');
    }

    return normalized;
}

} // namespace

Ref<Texture> Texture::Create(const std::string& path)
{
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
        return CreateRef<OpenGLTexture>(path);

    case RendererAPI::API::DirectX11:
        // 将来DirectX11Textureを追加した際に、ここで生成先を切り替えます。
        return nullptr;

    case RendererAPI::API::DirectX12:
        // 将来DirectX12Textureを追加した際に、ここで生成先を切り替えます。
        return nullptr;
    }

    return nullptr;
}

Ref<Texture> Texture::Create(const TextureSpecification& specification)
{
    switch (RendererAPI::GetAPI())
    {
    case RendererAPI::API::OpenGL:
        return CreateRef<OpenGLTexture>(specification);

    case RendererAPI::API::DirectX11:
        return nullptr;

    case RendererAPI::API::DirectX12:
        return nullptr;
    }

    return nullptr;
}

Ref<Texture> Texture::Create(
    const TextureSpecification& specification,
    const void* data,
    std::size_t dataSize
)
{
    Ref<Texture> texture = Create(specification);

    if (texture == nullptr)
    {
        return nullptr;
    }

    if (data != nullptr)
    {
        texture->SetData(data, dataSize);
    }

    return texture;
}

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

Ref<TextureAsset> TextureAssetImporter::Import(const std::string& sourcePath)
{
    if (sourcePath.empty())
    {
        std::cerr << "TextureAssetImporter::Import failed. Source path is empty." << std::endl;
        return nullptr;
    }

    const std::string extension = std::filesystem::path(sourcePath).extension().string();
    if (SupportsExtension(extension) == false)
    {
        std::cerr << "TextureAssetImporter::Import failed. Unsupported source format: "
                  << extension << " path: " << sourcePath << std::endl;
        return nullptr;
    }

    Ref<Texture> texture = Texture::Create(sourcePath);
    if (texture == nullptr || texture->GetID() == 0)
    {
        std::cerr << "TextureAssetImporter::Import failed. Runtime texture creation failed: "
                  << sourcePath << std::endl;
        return nullptr;
    }

    return CreateRef<TextureAsset>(sourcePath, texture);
}

bool TextureAssetImporter::SupportsExtension(const std::string& extension)
{
    const std::string normalized = NormalizeExtension(extension);

    // stb_imageで現在のTextureロード経路が扱える形式だけを明示します。
    // SVG/Rive/VideoはTextureへ押し込まず、将来それぞれ専用Runtime AssetへImportします。
    return normalized == ".png" ||
           normalized == ".jpg" ||
           normalized == ".jpeg" ||
           normalized == ".bmp" ||
           normalized == ".tga" ||
           normalized == ".gif" ||
           normalized == ".psd" ||
           normalized == ".hdr" ||
           normalized == ".pic" ||
           normalized == ".pnm";
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

void TextureLibrary::Add(const std::string& name, const Ref<Texture>& texture)
{
    if (texture == nullptr)
    {
        std::cerr << "TextureLibrary::Add failed. Texture is null: " << name << std::endl;
        return;
    }

    if (Exists(name))
    {
        std::cerr << "Texture already exists: " << name << std::endl;
        return;
    }

    m_Textures[name] = texture;
}

Ref<Texture> TextureLibrary::Load(const std::string& name, const std::string& path)
{
    Ref<Texture> texture = Texture::Create(path);
    Add(name, texture);
    return texture;
}

Ref<Texture> TextureLibrary::Get(const std::string& name)
{
    auto it = m_Textures.find(name);
    if (it == m_Textures.end())
    {
        std::cerr << "Texture not found: " << name << std::endl;
        return nullptr;
    }

    return it->second;
}

bool TextureLibrary::Exists(const std::string& name) const
{
    return m_Textures.find(name) != m_Textures.end();
}

}