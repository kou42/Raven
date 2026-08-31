#pragma once

#include <string>
#include <unordered_map>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Texture/Texture.h"

namespace Raven
{

// Source AssetのパスとRuntime Textureを結び付けるAsset層の型です。
// UIやゲーム側は.png/.jpg等のSource Formatを判定せず、このRuntime Assetを参照します。
class TextureAsset
{
public:
    TextureAsset(std::string sourcePath, const Ref<Texture>& texture);

    const std::string& GetSourcePath() const;
    const Ref<Texture>& GetTexture() const;
    bool IsValid() const;

private:
    std::string m_SourcePath;
    Ref<Texture> m_Texture;
};

// 同じSource Assetを重複Importしないための最小Runtime Cacheです。
// Import処理そのものはTextureAssetImporterへ委譲し、ManagerはAssetの寿命と再利用だけを担当します。
class TextureAssetManager
{
public:
    Ref<TextureAsset> Load(const std::string& sourcePath);
    Ref<TextureAsset> Get(const std::string& sourcePath) const;
    bool Exists(const std::string& sourcePath) const;
    void Clear();

private:
    std::unordered_map<std::string, Ref<TextureAsset>> m_Assets;
};

}