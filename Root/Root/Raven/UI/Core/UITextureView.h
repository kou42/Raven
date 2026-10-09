#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"

#include <cstdint>

namespace Raven
{

class RHITexture;
class Texture;
class TextureAsset;

// Texture内容の上端がnormalized V=0/1のどちらに対応するかを表します。
// Raven UIが公開するUVは常に左上原点であり、Graphics API差ではなくResource生成規約だけを
// このViewへ記録します。OpenGL Framebuffer Attachmentは通常BottomLeftです。
enum class UITextureOrigin
{
    TopLeft = 0,
    BottomLeft
};

enum class UITextureSourceType
{
    None = 0,
    Asset,
    LegacyTexture,
    RHITexture
};

// UI Coreへnative handleを公開せず、Asset画像とRenderTarget Textureを同じImage Commandへ渡すViewです。
// Refを保持するため、DrawListがGPU描画を完了するまで参照ResourceのLifetimeを維持します。
class UITextureView
{
public:
    static UITextureView FromAsset(
        const Ref<TextureAsset>& asset,
        UITextureOrigin origin = UITextureOrigin::TopLeft);
    static UITextureView FromTexture(
        const Ref<Texture>& texture,
        UITextureOrigin origin = UITextureOrigin::TopLeft);
    static UITextureView FromRHITexture(
        const Ref<RHITexture>& texture,
        UITextureOrigin origin = UITextureOrigin::TopLeft);

    bool IsValid() const;
    UITextureSourceType GetSourceType() const;
    UITextureOrigin GetOrigin() const;
    std::uint32_t GetWidth() const;
    std::uint32_t GetHeight() const;

    const Ref<TextureAsset>& GetAsset() const;
    const Ref<Texture>& GetLegacyTexture() const;
    const Ref<RHITexture>& GetRHITexture() const;

    // Raven UI左上原点UVをResource生成規約のUVへ変換します。
    math::Vec2 ResolveResourceUV(const math::Vec2& logicalUV) const;

    // logicalUVをResource Pixelへ変換します。Pixel座標もResource原点に従うため、
    // Framebuffer::ReadPixel等のBackend境界へそのまま渡せます。
    bool TryMapLogicalUVToPixel(
        const math::Vec2& logicalUV,
        std::uint32_t& outX,
        std::uint32_t& outY) const;

private:
    UITextureSourceType m_SourceType = UITextureSourceType::None;
    UITextureOrigin m_Origin = UITextureOrigin::TopLeft;
    Ref<TextureAsset> m_Asset;
    Ref<Texture> m_LegacyTexture;
    Ref<RHITexture> m_RHITexture;
};

} // namespace Raven
