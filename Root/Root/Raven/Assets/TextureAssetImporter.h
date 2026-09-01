#pragma once

#include <cstddef>
#include <string>

#include "Raven/Core/Base.h"

namespace Raven
{

class Texture;
class TextureAsset;

// Texture向けSource Asset Importerです。
// Source Formatの判定・decodeをAssets層へ閉じ込め、Rendererにはdecode済みpixelだけを渡します。
//
// File PathだけでなくMemory上のencoded imageも同じImporterへ通せるようにしています。
// glTF/GLBのbufferView内画像、Archive内画像、Network等から取得した画像でも、
// RendererへPNG/JPEG等のSource Format知識を漏らさず同じRuntime Texture生成経路を再利用できます。
class TextureAssetImporter
{
public:
    static Ref<TextureAsset> Import(const std::string& sourcePath);
    static Ref<Texture> ImportTexture(const std::string& sourcePath);

    // encodedDataはPNG/JPEG等の圧縮済みSource bytesです。
    // sourceIdentifierは実ファイルPathである必要はなく、"model.glb#image[0]"のような
    // Assetの出所を追跡するための論理名として利用できます。
    static Ref<TextureAsset> ImportMemory(
        const void* encodedData,
        std::size_t encodedSize,
        const std::string& sourceIdentifier);

    static Ref<Texture> ImportTextureMemory(
        const void* encodedData,
        std::size_t encodedSize,
        const std::string& sourceIdentifier = std::string{});

    static bool SupportsExtension(const std::string& extension);
};

}
