#include "Raven/Renderer/Texture/Texture.h"

#include "Raven/Assets/TextureAssetImporter.h"
#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Renderer/RenderCommand.h"
#include "Raven/Platform/OpenGL/OpenGLTexture.h"

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
    switch (GetRHIBackend())
    {
    case RHIBackend::OpenGL:
        return CreateRef<OpenGLTexture>(specification);

    case RHIBackend::DirectX11:
    case RHIBackend::DirectX12:
    case RHIBackend::Vulkan:
    case RHIBackend::None:
    default:
        return nullptr;
    }
}

Ref<Texture> Texture::Create(const TextureSpecification& specification, const void* data, std::size_t dataSize)
{
    // Legacy呼び出しの戻り値・初期化順は変更しません。
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

Ref<Texture> Texture::Create(const TextureSpecification& specification, const void* data,
    std::size_t dataSize, TextureCreationFailure* outFailure)
{
    if (outFailure != nullptr)
    {
        *outFailure = TextureCreationFailure::None;
    }

    // 現行Texture BridgeはOpenGLのみ実装されています。Deviceが無い場合は
    // OpenGLTextureを生成してもnative resourceを取得できないため、ここで区別します。
    if (GetRHIBackend() == RHIBackend::OpenGL && RenderCommand::GetDevice() == nullptr)
    {
        if (outFailure != nullptr)
        {
            *outFailure = TextureCreationFailure::DeviceUnavailable;
        }
        return nullptr;
    }

    Ref<Texture> texture = Create(specification);
    if (texture == nullptr || texture->GetID() == 0u)
    {
        if (outFailure != nullptr)
        {
            *outFailure = TextureCreationFailure::ResourceUnavailable;
        }
        return nullptr;
    }

    if (data != nullptr)
    {
        // RHITexture::SetDataはvoidのため、uploadの成否はここでは断定しません。
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