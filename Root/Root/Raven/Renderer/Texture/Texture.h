#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "Raven/Core/Base.h"

// Spriteは「画像」ではない
// →Texture + Transform + Render情報などの組み合わせ

// 要検討
// Sprite　
//↓
//RenderComponent
//↓
//Renderer2D
//↓
//BatchRenderer
//↓
//GPU
//という設計になることが多いらしいです。

namespace Raven
{

enum class TextureFormat
{
    None = 0,
    R8,
    RGB8,
    RGBA8,
    R32I,
    Depth24Stencil8
};

enum class TextureUsage
{
    Sampled = 0,
    RenderTarget,
    DepthStencil
};

struct TextureSpecification
{
    std::uint32_t Width = 1;
    std::uint32_t Height = 1;
    TextureFormat Format = TextureFormat::RGBA8;
    TextureUsage Usage = TextureUsage::Sampled;
    bool GenerateMips = true;
};

class Texture
{
public:
    virtual ~Texture() = default;

    // 画像ファイルからTextureを生成します。
    // 現在選択されているRendererAPIに対応した具象型の選択はFactory内部だけで行います。
    // 既存コード互換用として残し、新しいUI/RuntimeコードはTextureAssetImporterを経由します。
    static Ref<Texture> Create(const std::string& path);

    static Ref<Texture> Create(const TextureSpecification& specification);
    static Ref<Texture> Create(
        const TextureSpecification& specification,
        const void* data,
        std::size_t dataSize
    );

    virtual void Bind(unsigned int slot = 0) const = 0;
    virtual void Unbind() const = 0;

    // Texture全体のピクセルデータを更新します。
    // 現段階ではSampled用途のColor Texture更新を対象とします。
    // DepthStencil用途はGPUの描画先として利用するため、通常のSetData経路では更新しません。
    virtual void SetData(const void* data, std::size_t dataSize) = 0;

    // 既存コードとの互換性を維持するためRenderer側のIDを公開しています。
    // 現在のFramebuffer / ImGui連携整理後に、上位層からのRendererID直接参照を削除予定です。
    virtual unsigned int GetID() const = 0;

    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual const TextureSpecification& GetSpecification() const = 0;
};

// Source Assetの拡張子とRuntime Textureを分離する最初の境界です。
// UIは.png/.jpg等を判定せず、このRuntime Assetだけを参照します。
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

// Texture向けSource Asset Importerです。
// 現段階では既存のTexture::Create(path)を内部利用して互換性を保ちますが、
// 呼び出し側からファイル形式とRenderer生成経路を隠すことで、後続のdecoder分離を安全に行える境界を先に作ります。
class TextureAssetImporter
{
public:
    static Ref<TextureAsset> Import(const std::string& sourcePath);
    static bool SupportsExtension(const std::string& extension);
};

// Runtime側が同じSource Assetを重複Importしないための最小Cacheです。
// 将来AssetHandle/AssetRegistryを追加する際も、UI側の参照先をTextureAssetのまま維持できます。
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

class TextureLibrary
{
public:
    void Add(const std::string& name, const Ref<Texture>& texture);

    Ref<Texture> Load(const std::string& name, const std::string& path);

    Ref<Texture> Get(const std::string& name);
    bool Exists(const std::string& name) const;

private:
    std::unordered_map<std::string, Ref<Texture>> m_Textures;
};

}