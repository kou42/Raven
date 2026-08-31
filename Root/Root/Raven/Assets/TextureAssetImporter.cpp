#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Raven/Assets/TextureAssetImporter.h"

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Renderer/Texture/Texture.h"

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
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char value)
    {
        return static_cast<char>(std::tolower(value));
    });

    if (normalized.empty() == false && normalized.front() != '.')
    {
        normalized.insert(normalized.begin(), '.');
    }

    return normalized;
}

TextureFormat TextureFormatFromChannels(int channels)
{
    switch (channels)
    {
    case 1:
        return TextureFormat::R8;
    case 3:
        return TextureFormat::RGB8;
    case 4:
        return TextureFormat::RGBA8;
    default:
        return TextureFormat::None;
    }
}

std::size_t GetTextureDataSize(const TextureSpecification& specification)
{
    std::size_t bytesPerPixel = 0;
    switch (specification.Format)
    {
    case TextureFormat::R8:
        bytesPerPixel = 1;
        break;
    case TextureFormat::RGB8:
        bytesPerPixel = 3;
        break;
    case TextureFormat::RGBA8:
    case TextureFormat::R32I:
    case TextureFormat::Depth24Stencil8:
        bytesPerPixel = 4;
        break;
    case TextureFormat::None:
    default:
        return 0;
    }

    return static_cast<std::size_t>(specification.Width) *
           static_cast<std::size_t>(specification.Height) * bytesPerPixel;
}

} // namespace

Ref<Texture> TextureAssetImporter::ImportTexture(const std::string& sourcePath)
{
    if (sourcePath.empty())
    {
        std::cerr << "TextureAssetImporter::ImportTexture failed. Source path is empty." << std::endl;
        return nullptr;
    }

    const std::string extension = std::filesystem::path(sourcePath).extension().string();
    if (SupportsExtension(extension) == false)
    {
        std::cerr << "TextureAssetImporter::ImportTexture failed. Unsupported source format: "
                  << extension << " path: " << sourcePath << std::endl;
        return nullptr;
    }

    // Source画像の座標系変換はRenderer API固有の責務ではありません。
    // Importerで統一して上下反転し、どのRendererでも同じRuntime pixel layoutを受け取れるようにします。
    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(sourcePath.c_str(), &width, &height, &channels, 0);
    if (data == nullptr)
    {
        std::cerr << "Texture source decode failed: " << sourcePath << std::endl;
        return nullptr;
    }

    // stb_imageの戻り値をそのままunsignedへ変換すると異常値を巨大Textureとして扱う可能性があるため、
    // Runtime Specificationへ移す前に正の画像サイズであることを保証します。
    if (width <= 0 || height <= 0)
    {
        std::cerr << "Texture source decode failed. Invalid image size: "
                  << width << "x" << height << " path: " << sourcePath << std::endl;
        stbi_image_free(data);
        return nullptr;
    }

    const TextureFormat format = TextureFormatFromChannels(channels);
    if (format == TextureFormat::None)
    {
        std::cerr << "Texture source decode failed. Unsupported channel count: "
                  << channels << " path: " << sourcePath << std::endl;
        stbi_image_free(data);
        return nullptr;
    }

    TextureSpecification specification;
    specification.Width = static_cast<std::uint32_t>(width);
    specification.Height = static_cast<std::uint32_t>(height);
    specification.Format = format;
    specification.Usage = TextureUsage::Sampled;
    specification.GenerateMips = true;

    const std::size_t dataSize = GetTextureDataSize(specification);
    if (dataSize == 0)
    {
        stbi_image_free(data);
        return nullptr;
    }

    // RendererにはSource pathやPNG/JPEG等の形式を渡さず、decode済みpixelと共通Specificationだけを渡します。
    Ref<Texture> texture = Texture::Create(specification, data, dataSize);
    stbi_image_free(data);
    return texture;
}

Ref<TextureAsset> TextureAssetImporter::Import(const std::string& sourcePath)
{
    Ref<Texture> texture = ImportTexture(sourcePath);
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

    // stb_imageで現在扱うSource Formatだけを明示します。
    // SVG/Rive/VideoはTextureへ押し込まず、将来それぞれ専用ImporterとRuntime Assetへ分離します。
    return normalized == ".png" || normalized == ".jpg" || normalized == ".jpeg" ||
           normalized == ".bmp" || normalized == ".tga" || normalized == ".gif" ||
           normalized == ".psd" || normalized == ".hdr" || normalized == ".pic" ||
           normalized == ".pnm";
}

}