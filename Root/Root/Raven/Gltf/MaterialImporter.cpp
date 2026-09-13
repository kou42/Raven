// Raven/Gltf/MaterialImporter.cpp
#include "Raven/Gltf/MaterialImporter.h"

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Assets/TextureAssetImporter.h"
#include "Raven/Gltf/GlbReader.h"
#include "Raven/Gltf/JsonParser.h"

#include <cmath>
#include <filesystem>
#include <limits>
#include <utility>

namespace Raven
{
namespace Gltf
{
namespace
{
struct ImageSource
{
    std::string Uri;
    std::string MimeType;
    std::size_t BufferViewIndex = InvalidGltfIndex;
};

struct TextureSource
{
    std::size_t ImageIndex = InvalidGltfIndex;
};

bool SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    return false;
}

bool ReadSize(const JsonValue& value, std::size_t& outValue)
{
    if (value.IsNumber() == false)
    {
        return false;
    }
    const double number = value.GetNumber();
    if (std::isfinite(number) == false || number < 0.0 || std::floor(number) != number
        || number > static_cast<double>((std::numeric_limits<std::size_t>::max)()))
    {
        return false;
    }
    outValue = static_cast<std::size_t>(number);
    return true;
}

bool ReadFloat(const JsonValue& value, float& outValue)
{
    if (value.IsNumber() == false || std::isfinite(value.GetNumber()) == false)
    {
        return false;
    }
    const double number = value.GetNumber();
    if (number < -static_cast<double>((std::numeric_limits<float>::max)())
        || number > static_cast<double>((std::numeric_limits<float>::max)()))
    {
        return false;
    }
    outValue = static_cast<float>(number);
    return true;
}

bool ParseImages(const JsonValue& root, std::vector<ImageSource>& outImages, std::string* errorMessage)
{
    const JsonValue* value = root.Find("images");
    if (value == nullptr)
    {
        outImages.clear();
        return true;
    }
    if (value->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.imagesはArrayである必要があります");
    }
    std::vector<ImageSource> images;
    const JsonValue::Array& array = value->GetArray();
    images.reserve(array.size());
    for (std::size_t i = 0u; i < array.size(); ++i)
    {
        const JsonValue& item = array[i];
        const std::string context = "images[" + std::to_string(i) + "]";
        if (item.IsObject() == false)
        {
            return SetError(errorMessage, context + " はObjectである必要があります");
        }
        ImageSource image;
        const JsonValue* uri = item.Find("uri");
        if (uri != nullptr)
        {
            if (uri->IsString() == false)
            {
                return SetError(errorMessage, context + ".uri はStringである必要があります");
            }
            image.Uri = uri->GetString();
        }
        const JsonValue* mime = item.Find("mimeType");
        if (mime != nullptr)
        {
            if (mime->IsString() == false)
            {
                return SetError(errorMessage, context + ".mimeType はStringである必要があります");
            }
            image.MimeType = mime->GetString();
        }
        const JsonValue* bufferView = item.Find("bufferView");
        if (bufferView != nullptr && ReadSize(*bufferView, image.BufferViewIndex) == false)
        {
            return SetError(errorMessage, context + ".bufferView が0以上の整数ではありません");
        }
        const bool hasUri = image.Uri.empty() == false;
        const bool hasBufferView = image.BufferViewIndex != InvalidGltfIndex;
        if (hasUri == hasBufferView)
        {
            return SetError(errorMessage, context + " はuriまたはbufferViewのどちらか一方を持つ必要があります");
        }
        if (hasBufferView && image.MimeType.empty())
        {
            return SetError(errorMessage, context + " のbufferView画像にはmimeTypeが必要です");
        }
        images.emplace_back(std::move(image));
    }
    outImages = std::move(images);
    return true;
}

bool ParseTextures(const JsonValue& root, const std::vector<ImageSource>& images, std::vector<TextureSource>& outTextures, std::string* errorMessage)
{
    const JsonValue* value = root.Find("textures");
    if (value == nullptr)
    {
        outTextures.clear();
        return true;
    }
    if (value->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.texturesはArrayである必要があります");
    }
    std::vector<TextureSource> textures;
    const JsonValue::Array& array = value->GetArray();
    textures.reserve(array.size());
    for (std::size_t i = 0u; i < array.size(); ++i)
    {
        const JsonValue& item = array[i];
        const std::string context = "textures[" + std::to_string(i) + "]";
        if (item.IsObject() == false)
        {
            return SetError(errorMessage, context + " はObjectである必要があります");
        }
        const JsonValue* source = item.Find("source");
        TextureSource texture;
        if (source == nullptr || ReadSize(*source, texture.ImageIndex) == false || texture.ImageIndex >= images.size())
        {
            return SetError(errorMessage, context + ".source がimages範囲内の整数ではありません");
        }
        textures.emplace_back(texture);
    }
    outTextures = std::move(textures);
    return true;
}

bool ReadBaseColorFactor(const JsonValue& pbr, math::Vec4& outFactor, const std::string& context, std::string* errorMessage)
{
    const JsonValue* value = pbr.Find("baseColorFactor");
    if (value == nullptr)
    {
        return true;
    }
    if (value->IsArray() == false || value->GetArray().size() != 4u)
    {
        return SetError(errorMessage, context + ".baseColorFactor は4要素Arrayである必要があります");
    }
    float factor[4]{};
    for (std::size_t i = 0u; i < 4u; ++i)
    {
        if (ReadFloat(value->GetArray()[i], factor[i]) == false)
        {
            return SetError(errorMessage, context + ".baseColorFactor に有限な数値以外が含まれています");
        }
    }
    outFactor = { factor[0], factor[1], factor[2], factor[3] };
    return true;
}

bool ReadAlphaProperties(const JsonValue& value, ImportedMaterial& material, const std::string& context, std::string* errorMessage)
{
    const JsonValue* modeValue = value.Find("alphaMode");
    if (modeValue != nullptr)
    {
        if (modeValue->IsString() == false)
        {
            return SetError(errorMessage, context + ".alphaMode はStringである必要があります");
        }
        const std::string& mode = modeValue->GetString();
        if (mode == "OPAQUE")
        {
            material.AlphaMode = MaterialAlphaMode::Opaque;
        }
        else if (mode == "MASK")
        {
            material.AlphaMode = MaterialAlphaMode::Mask;
        }
        else if (mode == "BLEND")
        {
            material.AlphaMode = MaterialAlphaMode::Blend;
        }
        else
        {
            return SetError(errorMessage, context + ".alphaMode がglTF 2.0のOPAQUE/MASK/BLENDではありません");
        }
    }
    const JsonValue* cutoffValue = value.Find("alphaCutoff");
    if (cutoffValue != nullptr && ReadFloat(*cutoffValue, material.AlphaCutoff) == false)
    {
        return SetError(errorMessage, context + ".alphaCutoff は有限な数値である必要があります");
    }
    return true;
}

bool ResolveImageAsset(std::size_t imageIndex, const std::string& glbPath, const GltfDocument& document, const std::vector<ImageSource>& images, std::vector<Ref<TextureAsset>>& cache, Ref<TextureAsset>& outAsset, std::string* errorMessage)
{
    if (imageIndex >= images.size())
    {
        return SetError(errorMessage, "Image indexが範囲外です");
    }
    if (cache[imageIndex] != nullptr)
    {
        outAsset = cache[imageIndex];
        return true;
    }
    const ImageSource& image = images[imageIndex];
    Ref<TextureAsset> asset;
    if (image.BufferViewIndex != InvalidGltfIndex)
    {
        const std::vector<BufferView>& views = document.GetBufferViews();
        if (image.BufferViewIndex >= views.size())
        {
            return SetError(errorMessage, "imageのbufferViewが範囲外です");
        }
        const BufferView& view = views[image.BufferViewIndex];
        const std::vector<std::uint8_t>& binary = document.GetBinaryChunk();
        if (view.BufferIndex != 0u || view.ByteOffset > binary.size() || view.ByteLength > binary.size() - view.ByteOffset || view.ByteLength == 0u)
        {
            return SetError(errorMessage, "imageのbufferViewがGLB BIN範囲外です");
        }
        asset = TextureAssetImporter::ImportMemory(binary.data() + view.ByteOffset, view.ByteLength, glbPath + "#image[" + std::to_string(imageIndex) + "]");
    }
    else
    {
        if (image.Uri.rfind("data:", 0u) == 0u)
        {
            return SetError(errorMessage, "data URIは未対応です");
        }
        std::filesystem::path path(image.Uri);
        if (path.is_relative())
        {
            path = std::filesystem::path(glbPath).parent_path() / path;
        }
        asset = TextureAssetImporter::Import(path.lexically_normal().string());
    }
    if (asset == nullptr || asset->IsValid() == false)
    {
        return SetError(errorMessage, "TextureAsset Importに失敗しました");
    }
    cache[imageIndex] = asset;
    outAsset = asset;
    return true;
}

bool ParseMaterials(const JsonValue& root, const std::string& glbPath, const GltfDocument& document, const std::vector<ImageSource>& images, const std::vector<TextureSource>& textures, std::vector<ImportedMaterial>& outMaterials, std::string* errorMessage)
{
    const JsonValue* value = root.Find("materials");
    if (value == nullptr)
    {
        outMaterials.clear();
        return true;
    }
    if (value->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.materialsはArrayである必要があります");
    }
    std::vector<ImportedMaterial> materials;
    std::vector<Ref<TextureAsset>> imageCache(images.size());
    const JsonValue::Array& array = value->GetArray();
    materials.reserve(array.size());
    for (std::size_t i = 0u; i < array.size(); ++i)
    {
        const JsonValue& item = array[i];
        const std::string context = "materials[" + std::to_string(i) + "]";
        if (item.IsObject() == false)
        {
            return SetError(errorMessage, context + " はObjectである必要があります");
        }
        ImportedMaterial material;
        const JsonValue* name = item.Find("name");
        if (name != nullptr)
        {
            if (name->IsString() == false)
            {
                return SetError(errorMessage, context + ".name はStringである必要があります");
            }
            material.Name = name->GetString();
        }
        if (ReadAlphaProperties(item, material, context, errorMessage) == false)
        {
            return false;
        }
        const JsonValue* pbr = item.Find("pbrMetallicRoughness");
        if (pbr != nullptr)
        {
            if (pbr->IsObject() == false)
            {
                return SetError(errorMessage, context + ".pbrMetallicRoughness はObjectである必要があります");
            }
            const std::string pbrContext = context + ".pbrMetallicRoughness";
            if (ReadBaseColorFactor(*pbr, material.BaseColorFactor, pbrContext, errorMessage) == false)
            {
                return false;
            }
            const JsonValue* texture = pbr->Find("baseColorTexture");
            if (texture != nullptr)
            {
                if (texture->IsObject() == false)
                {
                    return SetError(errorMessage, pbrContext + ".baseColorTexture はObjectである必要があります");
                }
                const JsonValue* index = texture->Find("index");
                if (index == nullptr || ReadSize(*index, material.BaseColorTextureIndex) == false || material.BaseColorTextureIndex >= textures.size())
                {
                    return SetError(errorMessage, pbrContext + ".baseColorTexture.index がtextures範囲外です");
                }
                const JsonValue* texCoord = texture->Find("texCoord");
                if (texCoord != nullptr && ReadSize(*texCoord, material.BaseColorTexCoord) == false)
                {
                    return SetError(errorMessage, pbrContext + ".baseColorTexture.texCoord が0以上の整数ではありません");
                }
                material.BaseColorImageIndex = textures[material.BaseColorTextureIndex].ImageIndex;
                if (ResolveImageAsset(material.BaseColorImageIndex, glbPath, document, images, imageCache, material.BaseColorTexture, errorMessage) == false)
                {
                    return false;
                }
            }
        }
        materials.emplace_back(std::move(material));
    }
    outMaterials = std::move(materials);
    return true;
}
} // namespace

bool MaterialImporter::LoadFromGlb(const std::string& filePath, std::vector<ImportedMaterial>& outMaterials, std::string* errorMessage)
{
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    GlbData glbData;
    if (GlbReader::ReadFromFile(filePath, glbData, errorMessage) == false)
    {
        return false;
    }
    JsonValue root;
    if (JsonParser::Parse(glbData.JsonText, root, errorMessage) == false)
    {
        return false;
    }
    GltfDocument document;
    if (GltfDocument::BuildFromJson(root, std::move(glbData.BinaryChunk), document, errorMessage) == false)
    {
        return false;
    }
    std::vector<ImageSource> images;
    if (ParseImages(root, images, errorMessage) == false)
    {
        return false;
    }
    std::vector<TextureSource> textures;
    if (ParseTextures(root, images, textures, errorMessage) == false)
    {
        return false;
    }
    std::vector<ImportedMaterial> materials;
    if (ParseMaterials(root, filePath, document, images, textures, materials, errorMessage) == false)
    {
        return false;
    }
    outMaterials = std::move(materials);
    return true;
}

} // namespace Gltf
} // namespace Raven
