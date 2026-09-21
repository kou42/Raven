#pragma once

#include "Raven/Assets/TextureAsset.h"
#include "Raven/UI/Core/UIDrawList.h"
#include "Raven/UI/Text/UIUtf8.h"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <string_view>

namespace Raven
{

// Glyphの矩形はAtlas内のpixel座標、BearingはBaselineからGlyph左上へのoffsetです。
// Advanceは次のGlyphのPen位置までの距離であり、Bitmapの幅とは一致しない場合があります。
struct UIGlyphMetrics
{
    UIRect AtlasRect{};
    math::Vec2 Bearing{};
    float Advance = 0.0f;
};

// RasterizerとGPU描画を分離するFont Atlasの最小Runtime表現です。
// Fontファイルの読込・Rasterize・Atlasへの詰め込みは後続のFont Loaderが担当します。
// TextureAssetのRefを保持し、DrawListが発行したGlyphの描画終了までTextureを生存させます。
class UIFontAtlas
{
public:
    bool Initialize(const Ref<TextureAsset>& texture, std::uint32_t width, std::uint32_t height)
    {
        if (texture == nullptr || texture->IsValid() == false || width == 0u || height == 0u)
        {
            return false;
        }

        const Ref<Texture>& runtimeTexture = texture->GetTexture();
        if (runtimeTexture == nullptr ||
            runtimeTexture->GetWidth() != static_cast<int>(width) ||
            runtimeTexture->GetHeight() != static_cast<int>(height))
        {
            return false;
        }

        m_Texture = texture;
        m_Width = width;
        m_Height = height;
        m_Glyphs.clear();
        m_Ascent = 0.0f;
        m_Descent = 0.0f;
        m_LineGap = 0.0f;
        return true;
    }

    void Clear()
    {
        m_Glyphs.clear();
        m_Texture = nullptr;
        m_Width = 0u;
        m_Height = 0u;
        m_Ascent = 0.0f;
        m_Descent = 0.0f;
        m_LineGap = 0.0f;
    }

    bool AddGlyph(std::uint32_t codepoint, const UIGlyphMetrics& metrics)
    {
        if (m_Texture == nullptr || codepoint > 0x10FFFFu ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu) ||
            metrics.Advance < 0.0f ||
            metrics.AtlasRect.Min.x < 0.0f || metrics.AtlasRect.Min.y < 0.0f ||
            metrics.AtlasRect.Max.x < metrics.AtlasRect.Min.x ||
            metrics.AtlasRect.Max.y < metrics.AtlasRect.Min.y ||
            metrics.AtlasRect.Max.x > static_cast<float>(m_Width) ||
            metrics.AtlasRect.Max.y > static_cast<float>(m_Height))
        {
            return false;
        }

        m_Glyphs[codepoint] = metrics;
        return true;
    }

    const UIGlyphMetrics* FindGlyph(std::uint32_t codepoint) const
    {
        const auto found = m_Glyphs.find(codepoint);
        if (found == m_Glyphs.end())
        {
            return nullptr;
        }
        return &found->second;
    }

    // PenはBaselineの位置です。Glyphが空白等で面積0の場合もAdvanceは呼び出し元が適用します。
    bool AppendGlyph(
        UIDrawList& drawList,
        std::uint32_t codepoint,
        const math::Vec2& pen,
        const math::Vec4& color = math::Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }) const
    {
        const UIGlyphMetrics* glyph = FindGlyph(codepoint);
        if (glyph == nullptr || m_Texture == nullptr)
        {
            return false;
        }

        const math::Vec2 size(
            glyph->AtlasRect.Max.x - glyph->AtlasRect.Min.x,
            glyph->AtlasRect.Max.y - glyph->AtlasRect.Min.y);
        if (size.x > 0.0f && size.y > 0.0f)
        {
            const math::Vec2 min = pen + glyph->Bearing;
            const math::Vec2 max = min + size;
            const math::Vec2 uvMin(
                glyph->AtlasRect.Min.x / static_cast<float>(m_Width),
                glyph->AtlasRect.Min.y / static_cast<float>(m_Height));
            const math::Vec2 uvMax(
                glyph->AtlasRect.Max.x / static_cast<float>(m_Width),
                glyph->AtlasRect.Max.y / static_cast<float>(m_Height));

            // Atlasのpixel座標をUI共通の左上原点UVへ変換し、既存Image経路で描画します。
            drawList.AddGlyph(min, max, m_Texture, uvMin, uvMax, color);
        }
        return true;
    }

    // 文字列をBaseline起点で左から右へ並べます。
    // Glyphが未収録ならU+FFFD、次に'?'を探し、いずれも無ければ描画せず進みます。
    // 改行はLineHeightでPenを進めます。Kerning・折り返し・複雑な文字形成は後続Phaseです。
    math::Vec2 AppendText(
        UIDrawList& drawList,
        std::string_view text,
        const math::Vec2& baseline,
        float lineHeight,
        const math::Vec4& color = math::Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }) const;

    // Font全体のBaseline基準Metrics（pixel）。Descentは通常負値です。
    void SetVerticalMetrics(float ascent, float descent, float lineGap)
    {
        m_Ascent = ascent;
        m_Descent = descent;
        m_LineGap = lineGap;
    }
    float GetAscent() const { return m_Ascent; }
    float GetDescent() const { return m_Descent; }
    float GetLineGap() const { return m_LineGap; }

    const Ref<TextureAsset>& GetTexture() const { return m_Texture; }
    std::uint32_t GetWidth() const { return m_Width; }
    std::uint32_t GetHeight() const { return m_Height; }

private:
    Ref<TextureAsset> m_Texture;
    std::uint32_t m_Width = 0u;
    std::uint32_t m_Height = 0u;
    std::unordered_map<std::uint32_t, UIGlyphMetrics> m_Glyphs;
    float m_Ascent = 0.0f;
    float m_Descent = 0.0f;
    float m_LineGap = 0.0f;
};

} // namespace Raven
