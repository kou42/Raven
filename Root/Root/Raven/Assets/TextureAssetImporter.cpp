#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Raven/Assets/TextureAssetImporter.h"

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Renderer/Texture/Texture.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <limits>

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

std::size_t GetBytesPerPixel(TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::R8:
        return 1u;
    case TextureFormat::RGB8:
        return 3u;
    case TextureFormat::RGBA8:
    case TextureFormat::R32I:
    case TextureFormat::Depth24Stencil8:
        return 4u;
    case TextureFormat::None:
    default:
        return 0u;
    }
}

std::size_t GetTextureDataSize(const TextureSpecification& specification)
{
    const std::size_t bytesPerPixel = GetBytesPerPixel(specification.Format);
    if (bytesPerPixel == 0u)
    {
        return 0u;
    }

    const std::size_t width = static_cast<std::size_t>(specification.Width);
    const std::size_t height = static_cast<std::size_t>(specification.Height);

    // 外部Asset由来の寸法を掛け合わせるため、size_t overflowを事前に拒否します。
    if (height != 0u && width > (std::numeric_limits<std::size_t>::max)() / height)
    {
        return 0u;
    }

    const std::size_t pixelCount = width * height;
    if (pixelCount > (std::numeric_limits<std::size_t>::max)() / bytesPerPixel)
    {
        return 0u;
    }

    return pixelCount * bytesPerPixel;
}

Ref<Texture> CreateRuntimeTextureFromDecodedPixels(
    unsigned char* data,
    int width,
    int height,
    int channels,
    const std::string& sourceIdentifier)
{
    if (data == nullptr)
    {
        return nullptr;
    }

    // stb_imageの戻り値をそのままunsignedへ変換すると異常値を巨大Textureとして扱う可能性があるため、
    // Runtime Specificationへ移す前に正の画像サイズであることを保証します。
    if (width <= 0 || height <= 0)
    {
        std::cerr << "Texture source decode failed. Invalid image size: "
                  << width << "x" << height << " source: " << sourceIdentifier << std::endl;
        return nullptr;
    }

    const TextureFormat format = TextureFormatFromChannels(channels);
    if (format == TextureFormat::None)
    {
        std::cerr << "Texture source decode failed. Unsupported channel count: "
                  << channels << " source: " << sourceIdentifier << std::endl;
        return nullptr;
    }

    TextureSpecification specification;
    specification.Width = static_cast<std::uint32_t>(width);
    specification.Height = static_cast<std::uint32_t>(height);
    specification.Format = format;
    specification.Usage = TextureUsage::Sampled;
    specification.GenerateMips = true;

    const std::size_t dataSize = GetTextureDataSize(specification);
    if (dataSize == 0u)
    {
        std::cerr << "Texture runtime data size calculation failed. source: "
                  << sourceIdentifier << std::endl;
        return nullptr;
    }

    // RendererにはSource pathやPNG/JPEG等の形式を渡さず、decode済みpixelと共通Specificationだけを渡します。
    return Texture::Create(specification, data, dataSize);
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

    // ImporterはOpenGL等のRenderer API都合でSource画像を上下反転しません。
    // decode結果のrow順をそのままRuntime Textureへ渡し、UV原点・座標系の差は
    // UI / Meshなど各RuntimeデータのUV規約とRenderer backendの境界で扱います。
    // これにより同じSource Assetから生成される内容がRenderer APIによって変化することを防ぎます。
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(sourcePath.c_str(), &width, &height, &channels, 0);
    if (data == nullptr)
    {
        std::cerr << "Texture source decode failed: " << sourcePath << std::endl;
        return nullptr;
    }

    Ref<Texture> texture = CreateRuntimeTextureFromDecodedPixels(
        data,
        width,
        height,
        channels,
        sourcePath);

    stbi_image_free(data);
    return texture;
}

Ref<Texture> TextureAssetImporter::ImportTextureMemory(
    const void* encodedData,
    std::size_t encodedSize,
    const std::string& sourceIdentifier)
{
    if (encodedData == nullptr || encodedSize == 0u)
    {
        std::cerr << "TextureAssetImporter::ImportTextureMemory failed. Encoded image is empty. source: "
                  << sourceIdentifier << std::endl;
        return nullptr;
    }

    // stb_imageのMemory APIはbyte長をintで受け取ります。
    // size_tを無条件castすると2GB超Sourceで切り詰めが起きるため、境界で明示的に拒否します。
    if (encodedSize > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        std::cerr << "TextureAssetImporter::ImportTextureMemory failed. Encoded image is too large. source: "
                  << sourceIdentifier << std::endl;
        return nullptr;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    const stbi_uc* bytes = static_cast<const stbi_uc*>(encodedData);
    unsigned char* data = stbi_load_from_memory(
        bytes,
        static_cast<int>(encodedSize),
        &width,
        &height,
        &channels,
        0);

    if (data == nullptr)
    {
        std::cerr << "Texture memory decode failed. source: " << sourceIdentifier << std::endl;
        return nullptr;
    }

    Ref<Texture> texture = CreateRuntimeTextureFromDecodedPixels(
        data,
        width,
        height,
        channels,
        sourceIdentifier);

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

Ref<TextureAsset> TextureAssetImporter::ImportMemory(
    const void* encodedData,
    std::size_t encodedSize,
    const std::string& sourceIdentifier)
{
    Ref<Texture> texture = ImportTextureMemory(encodedData, encodedSize, sourceIdentifier);
    if (texture == nullptr || texture->GetID() == 0)
    {
        std::cerr << "TextureAssetImporter::ImportMemory failed. Runtime texture creation failed. source: "
                  << sourceIdentifier << std::endl;
        return nullptr;
    }

    // TextureAssetの既存SourcePath fieldは現段階では「Sourceを識別する文字列」としても利用します。
    // AssetHandle/Registry導入時にPathとEmbedded Asset IDを型として分離できるよう、
    // glTF側では実在しないPathへ変換せず論理Identifierをそのまま保持します。
    return CreateRef<TextureAsset>(sourceIdentifier, texture);
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
