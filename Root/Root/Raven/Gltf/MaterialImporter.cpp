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
    if (std::isfinite(number) == false || number < 0.0 || std::floor(number) != number)
    {
        return false;
    }

    if (number > static_cast<double>((std::numeric_limits<std::size_t>::max)()))
    {
        return false;
    }

    outValue = static_cast<std::size_t>(number);
    return true;
}

bool ReadFloat(const JsonValue& value, float& outValue)
{
    if (value.IsNumber() == false)
    {
        return false;
    }

    const double number = value.GetNumber();
    if (std::isfinite(number) == false)
    {
        return false;
    }

    if (number < -static_cast<double>((std::numeric_limits<float>::max)())
        || number > static_cast<double>((std::numeric_limits<float>::max)()))
    {
        return false;
    }

    outValue = static_cast<float>(number);
    return true;
}

bool ParseImages(
    const JsonValue& root,
    std::vector<ImageSource>& outImages,
    std::string* errorMessage)
{
    const JsonValue* imagesValue = root.Find("images");
    if (imagesValue == nullptr)
    {
        outImages.clear();
        return true;
    }
    if (imagesValue->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.imagesはArrayである必要があります");
    }

    const JsonValue::Array& imageArray = imagesValue->GetArray();
    std::vector<ImageSource> images;
    images.reserve(imageArray.size());

    for (std::size_t imageIndex = 0u; imageIndex < imageArray.size(); ++imageIndex)
    {
        const JsonValue& imageValue = imageArray[imageIndex];
        const std::string context = "images[" + std::to_string(imageIndex) + "]";
        if (imageValue.IsObject() == false)
        {
            return SetError(errorMessage, context + " はObjectである必要があります");
        }

        ImageSource image;

        const JsonValue* uriValue = imageValue.Find("uri");
        if (uriValue != nullptr)
        {
            if (uriValue->IsString() == false)
            {
                return SetError(errorMessage, context + ".uri はStringである必要があります");
            }
            image.Uri = uriValue->GetString();
        }

        const JsonValue* mimeTypeValue = imageValue.Find("mimeType");
        if (mimeTypeValue != nullptr)
        {
            if (mimeTypeValue->IsString() == false)
            {
                return SetError(errorMessage, context + ".mimeType はStringである必要があります");
            }
            image.MimeType = mimeTypeValue->GetString();
        }

        const JsonValue* bufferViewValue = imageValue.Find("bufferView");
        if (bufferViewValue != nullptr)
        {
            if (ReadSize(*bufferViewValue, image.BufferViewIndex) == false)
            {
                return SetError(errorMessage, context + ".bufferView が0以上の整数ではありません");
            }
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

bool ParseTextures(
    const JsonValue& root,
    const std::vector<ImageSource>& images,
    std::vector<TextureSource>& outTextures,
    std::string* errorMessage)
{
    const JsonValue* texturesValue = root.Find("textures");
    if (texturesValue == nullptr)
    {
        outTextures.clear();
        return true;
    }
    if (texturesValue->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.texturesはArrayである必要があります");
    }

    const JsonValue::Array& textureArray = texturesValue->GetArray();
    std::vector<TextureSource> textures;
    textures.reserve(textureArray.size());

    for (std::size_t textureIndex = 0u; textureIndex < textureArray.size(); ++textureIndex)
    {
        const JsonValue& textureValue = textureArray[textureIndex];
        const std::string context = "textures[" + std::to_string(textureIndex) + "]";
        if (textureValue.IsObject() == false)
        {
            return SetError(errorMessage, context + " はObjectである必要があります");
        }

        const JsonValue* sourceValue = textureValue.Find("source");
        if (sourceValue == nullptr)
        {
            return SetError(errorMessage, context + ".source がありません");
        }

        TextureSource texture;
        if (ReadSize(*sourceValue, texture.ImageIndex) == false)
        {
            return SetError(errorMessage, context + ".source が0以上の整数ではありません");
        }
        if (texture.ImageIndex >= images.size())
        {
            return SetError(errorMessage, context + ".source がimages範囲外です");
        }

        textures.emplace_back(texture);
    }

    outTextures = std::move(textures);
    return true;
}

bool ReadBaseColorFactor(
    const JsonValue& pbr,
    math::Vec4& outFactor,
    const std::string& context,
    std::string* errorMessage)
{
    const JsonValue* factorValue = pbr.Find("baseColorFactor");
    if (factorValue == nullptr)
    {
        return true;
    }
    if (factorValue->IsArray() == false)
    {
        return SetError(errorMessage, context + ".baseColorFactor はArrayである必要があります");
    }

    const JsonValue::Array& values = factorValue->GetArray();
    if (values.size() != 4u)
    {
        return SetError(errorMessage, context + ".baseColorFactor は4要素である必要があります");
    }

    float factor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    for (std::size_t i = 0u; i < 4u; ++i)
    {
        if (ReadFloat(values[i], factor[i]) == false)
        {
            return SetError(errorMessage, context + ".baseColorFactor に有限な数値以外が含まれています");
        }
    }

    outFactor = math::Vec4{ factor[0], factor[1], factor[2], factor[3] };
    return true;
}

bool ResolveImageAsset(
    std::size_t imageIndex,
    const std::string& glbPath,
    const GltfDocument& document,
    const std::vector<ImageSource>& images,
    std::vector<Ref<TextureAsset>>& imageCache,
    Ref<TextureAsset>& outAsset,
    std::string* errorMessage)
{
    if (imageIndex >= images.size())
    {
        return SetError(errorMessage, "Image indexが範囲外です");
    }

    if (imageCache[imageIndex] != nullptr)
    {
        outAsset = imageCache[imageIndex];
        return true;
    }

    const ImageSource& image = images[imageIndex];
    Ref<TextureAsset> asset;

    if (image.BufferViewIndex != InvalidGltfIndex)
    {
        const std::vector<BufferView>& bufferViews = document.GetBufferViews();
        if (image.BufferViewIndex >= bufferViews.size())
        {
            return SetError(errorMessage, "images[" + std::to_string(imageIndex) + "].bufferView が範囲外です");
        }

        const BufferView& view = bufferViews[image.BufferViewIndex];

        // 現在GltfDocumentが保持するRaw bytesはGLB BIN chunkのみです。
        // 外部bufferを指すbufferViewまでここで誤ってBIN chunkへ解決しないようbuffer indexを制限します。
        if (view.BufferIndex != 0u)
        {
            return SetError(errorMessage, "images[" + std::to_string(imageIndex) + "] はGLB BIN buffer以外のbufferViewを参照しています");
        }

        const std::vector<std::uint8_t>& binary = document.GetBinaryChunk();
        if (view.ByteOffset > binary.size()
            || view.ByteLength > binary.size() - view.ByteOffset)
        {
            return SetError(errorMessage, "images[" + std::to_string(imageIndex) + "] のbufferView byte範囲がBIN chunk外です");
        }
        if (view.ByteLength == 0u)
        {
            return SetError(errorMessage, "images[" + std::to_string(imageIndex) + "] のbufferViewが空です");
        }

        const std::string sourceIdentifier = glbPath + "#image[" + std::to_string(imageIndex) + "]";
        asset = TextureAssetImporter::ImportMemory(
            binary.data() + view.ByteOffset,
            view.ByteLength,
            sourceIdentifier);
    }
    else
    {
        // data URIはBase64 decode責務が未実装なので、通常の相対/絶対File URIと混同せず明示的に拒否します。
        if (image.Uri.rfind("data:", 0u) == 0u)
        {
            return SetError(errorMessage, "images[" + std::to_string(imageIndex) + "] のdata URIは未対応です");
        }

        const std::filesystem::path glbFilePath(glbPath);
        std::filesystem::path imagePath(image.Uri);
        if (imagePath.is_relative())
        {
            imagePath = glbFilePath.parent_path() / imagePath;
        }

        asset = TextureAssetImporter::Import(imagePath.lexically_normal().string());
    }

    if (asset == nullptr || asset->IsValid() == false)
    {
        return SetError(errorMessage, "images[" + std::to_string(imageIndex) + "] のTextureAsset Importに失敗しました");
    }

    imageCache[imageIndex] = asset;
    outAsset = asset;
    return true;
}

bool ParseMaterials(
    const JsonValue& root,
    const std::string& glbPath,
    const GltfDocument& document,
    const std::vector<ImageSource>& images,
    const std::vector<TextureSource>& textures,
    std::vector<ImportedMaterial>& outMaterials,
    std::string* errorMessage)
{
    const JsonValue* materialsValue = root.Find("materials");
    if (materialsValue == nullptr)
    {
        outMaterials.clear();
        return true;
    }
    if (materialsValue->IsArray() == false)
    {
        return SetError(errorMessage, "glTF.materialsはArrayである必要があります");
    }

    const JsonValue::Array& materialArray = materialsValue->GetArray();
    std::vector<ImportedMaterial> materials;
    materials.reserve(materialArray.size());

    // 複数Materialが同じglTF texture/imageを参照するケースで重複decode/GPU UploadしないためのImport単位Cacheです。
    std::vector<Ref<TextureAsset>> imageCache(images.size());

    for (std::size_t materialIndex = 0u; materialIndex < materialArray.size(); ++materialIndex)
    {
        const JsonValue& materialValue = materialArray[materialIndex];
        const std::string materialContext = "materials[" + std::to_string(materialIndex) + "]";
        if (materialValue.IsObject() == false)
        {
            return SetError(errorMessage, materialContext + " はObjectである必要があります");
        }

        ImportedMaterial material;

        const JsonValue* nameValue = materialValue.Find("name");
        if (nameValue != nullptr)
        {
            if (nameValue->IsString() == false)
            {
                return SetError(errorMessage, materialContext + ".name はStringである必要があります");
            }
            material.Name = nameValue->GetString();
        }

        const JsonValue* pbrValue = materialValue.Find("pbrMetallicRoughness");
        if (pbrValue != nullptr)
        {
            if (pbrValue->IsObject() == false)
            {
                return SetError(errorMessage, materialContext + ".pbrMetallicRoughness はObjectである必要があります");
            }

            const std::string pbrContext = materialContext + ".pbrMetallicRoughness";
            if (ReadBaseColorFactor(*pbrValue, material.BaseColorFactor, pbrContext, errorMessage) == false)
            {
                return false;
            }

            const JsonValue* baseColorTextureValue = pbrValue->Find("baseColorTexture");
            if (baseColorTextureValue != nullptr)
            {
                if (baseColorTextureValue->IsObject() == false)
                {
                    return SetError(errorMessage, pbrContext + ".baseColorTexture はObjectである必要があります");
                }

                const JsonValue* textureIndexValue = baseColorTextureValue->Find("index");
                if (textureIndexValue == nullptr
                    || ReadSize(*textureIndexValue, material.BaseColorTextureIndex) == false)
                {
                    return SetError(errorMessage, pbrContext + ".baseColorTexture.index が0以上の整数ではありません");
                }
                if (material.BaseColorTextureIndex >= textures.size())
                {
                    return SetError(errorMessage, pbrContext + ".baseColorTexture.index がtextures範囲外です");
                }

                const JsonValue* texCoordValue = baseColorTextureValue->Find("texCoord");
                if (texCoordValue != nullptr)
                {
                    if (ReadSize(*texCoordValue, material.BaseColorTexCoord) == false)
                    {
                        return SetError(errorMessage, pbrContext + ".baseColorTexture.texCoord が0以上の整数ではありません");
                    }
                }

                material.BaseColorImageIndex = textures[material.BaseColorTextureIndex].ImageIndex;
                if (ResolveImageAsset(
                        material.BaseColorImageIndex,
                        glbPath,
                        document,
                        images,
                        imageCache,
                        material.BaseColorTexture,
                        errorMessage) == false)
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

bool MaterialImporter::LoadFromGlb(
    const std::string& filePath,
    std::vector<ImportedMaterial>& outMaterials,
    std::string* errorMessage)
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
    if (ParseMaterials(
            root,
            filePath,
            document,
            images,
            textures,
            materials,
            errorMessage) == false)
    {
        return false;
    }

    outMaterials = std::move(materials);
    return true;
}

} // namespace Gltf
} // namespace Raven
