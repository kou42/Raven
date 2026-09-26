#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstddef>
#include <cstdint>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Texture/Texture.h"
#include "Raven/Renderer/RHI/RHITexture.h"

namespace Raven
{

// Source AssetのパスとRuntime Textureを結び付けるAsset層の型です。
// UIやゲーム側は.png/.jpg等のSource Formatを判定せず、このRuntime Assetを参照します。
class RHIDevice;

struct TextureAssetPixelData
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    TextureFormat Format = TextureFormat::None;
    bool GenerateMips = true;
    std::vector<std::byte> Pixels;

    bool IsValid() const;
};

class TextureAsset
{
public:
    TextureAsset(std::string sourcePath, const Ref<Texture>& texture);
    TextureAsset(std::string sourcePath, const Ref<Texture>& texture,
        TextureAssetPixelData pixelData);

    const std::string& GetSourcePath() const;
    const Ref<Texture>& GetTexture() const;
    bool IsValid() const;
    bool HasPixelData() const;
    const TextureAssetPixelData& GetPixelData() const;

    // Explicit Backend用TextureはAssetのdecode済みPixelから生成します。
    // Legacy Textureのnative ID/readbackへ依存しないため、Backend間で同じSource内容を再利用できます。
    Ref<RHITexture> CreateRHITexture(RHIDevice& device) const;

private:
    std::string m_SourcePath;
    Ref<Texture> m_Texture;
    TextureAssetPixelData m_PixelData;
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