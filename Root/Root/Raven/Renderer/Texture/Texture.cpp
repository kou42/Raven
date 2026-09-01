#include "Raven/Renderer/Texture/Texture.h"

#include "Raven/Assets/TextureAssetImporter.h"
#include "Raven/Renderer/RendererAPI.h"
#include "Raven/Renderer/Texture/OpenGLTexture.h"

#include <iostream>

namespace Raven
{

Ref<Texture> Texture::Create(const std::string& path)
{
    // 既存APIの互換性を維持するため入口だけ残します。
    // Source Format判定とdecodeはAssets層へ委譲し、Renderer層自身はファイル形式を扱いません。
    return TextureAssetImporter::ImportTexture(path);
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

Ref<Texture> Texture::Create(const TextureSpecification& specification, const void* data, std::size_t dataSize)
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