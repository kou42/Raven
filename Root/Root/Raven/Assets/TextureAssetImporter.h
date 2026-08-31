#pragma once

#include <string>

#include "Raven/Core/Base.h"

namespace Raven
{

class Texture;
class TextureAsset;

// Texture向けSource Asset Importerです。
// Source Formatの判定・decodeをAssets層へ閉じ込め、Rendererにはdecode済みpixelだけを渡します。
class TextureAssetImporter
{
public:
    static Ref<TextureAsset> Import(const std::string& sourcePath);
    static Ref<Texture> ImportTexture(const std::string& sourcePath);
    static bool SupportsExtension(const std::string& extension);
};

}