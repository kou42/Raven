#include "Raven/UI/Text/UIFontAtlasBuilder.h"

#include "Raven/Renderer/Texture/Texture.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

// Ravenが独立管理するinclude/stb_truetype.hを、この翻訳単位だけで実装します。
// STBTT_STATICでシンボルを内部リンケージにし、外部ライブラリとの衝突を防ぎます。
// UI公開ヘッダもビルド時のFont処理もImGuiのヘッダを参照しません。
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace Raven
{
namespace
{
constexpr std::uint32_t kMaximumAtlasSide = 4096u;
constexpr std::size_t kMaximumFontBytes = static_cast<std::size_t>(std::numeric_limits<int>::max());

bool IsValidCodepoint(std::uint32_t codepoint)
{
    return codepoint <= 0x10FFFFu &&
        (codepoint < 0xD800u || codepoint > 0xDFFFu);
}
} // namespace

bool UIFontAtlasBuilder::ResolveDPIOptions(
    const UIFontAtlasBuildOptions& baseOptions,
    float effectiveScale,
    UIFontAtlasBuildOptions& outOptions,
    float& outRasterScale)
{
    if (std::isfinite(effectiveScale) == false || effectiveScale <= 0.0f ||
        std::isfinite(baseOptions.PixelHeight) == false || baseOptions.PixelHeight <= 0.0f ||
        effectiveScale > 16.0f)
    {
        return false;
    }
    // 倍率を1/8刻みに固定し、同一Monitor付近の微小な倍率差でAtlasを増やしません。
    const float quantized = std::max(1.0f, std::round(effectiveScale * 8.0f)) / 8.0f;
    const float pixelHeight = baseOptions.PixelHeight * quantized;
    if (std::isfinite(pixelHeight) == false || pixelHeight > 4096.0f)
    {
        return false;
    }
    outOptions = baseOptions;
    outOptions.PixelHeight = pixelHeight;
    outRasterScale = quantized;
    return true;
}

Ref<UIFontAtlas> UIFontAtlasDPICache::Find(
    const std::string& fontPath,
    const std::vector<std::uint32_t>& codepoints,
    const UIFontAtlasBuildOptions& baseOptions,
    float effectiveScale,
    float& outRasterScale) const
{
    UIFontAtlasBuildOptions resolved{};
    float rasterScale = 1.0f;
    if (UIFontAtlasBuilder::ResolveDPIOptions(baseOptions, effectiveScale,
        resolved, rasterScale) == false)
    {
        return nullptr;
    }
    std::vector<std::uint32_t> normalized = codepoints;
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    const std::uint32_t scaleStep = static_cast<std::uint32_t>(rasterScale * 8.0f);
    const Key key(fontPath, normalized, baseOptions.PixelHeight, baseOptions.AtlasWidth,
        baseOptions.AtlasHeight, baseOptions.Padding, scaleStep);
    const auto found = m_Entries.find(key);
    if (found == m_Entries.end())
    {
        return nullptr;
    }
    outRasterScale = rasterScale;
    return found->second;
}

Ref<UIFontAtlas> UIFontAtlasDPICache::GetOrBuild(
    const std::string& fontPath,
    const std::vector<std::uint32_t>& codepoints,
    const UIFontAtlasBuildOptions& baseOptions,
    float effectiveScale,
    float& outRasterScale,
    UIFontAtlasBuildFailure* outFailure)
{
    if (outFailure != nullptr)
    {
        *outFailure = UIFontAtlasBuildFailure::None;
    }
    UIFontAtlasBuildOptions resolved{};
    float rasterScale = 1.0f;
    if (UIFontAtlasBuilder::ResolveDPIOptions(baseOptions, effectiveScale,
        resolved, rasterScale) == false)
    {
        if (outFailure != nullptr)
        {
            *outFailure = UIFontAtlasBuildFailure::InvalidDPIOptions;
        }
        return nullptr;
    }
    // 文字集合は順序に依存しないため正規化し、同一内容の重複Atlasを避けます。
    std::vector<std::uint32_t> normalized = codepoints;
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    const std::uint32_t scaleStep = static_cast<std::uint32_t>(rasterScale * 8.0f);
    const Key key(fontPath, normalized, baseOptions.PixelHeight, baseOptions.AtlasWidth,
        baseOptions.AtlasHeight, baseOptions.Padding, scaleStep);
    const auto found = m_Entries.find(key);
    if (found != m_Entries.end())
    {
        outRasterScale = rasterScale;
        return found->second;
    }
    // ファイル未発見とRasterize/GPU生成失敗を分離して診断します。
    // filesystemの例外を避け、無効Pathでも従来どおり失敗として返します。
    std::error_code fileError;
    if (std::filesystem::is_regular_file(fontPath, fileError) == false)
    {
        if (outFailure != nullptr)
        {
            *outFailure = UIFontAtlasBuildFailure::FontFileUnavailable;
        }
        return nullptr;
    }
    Ref<UIFontAtlas> built = CreateRef<UIFontAtlas>();
    UIFontAtlasBuildFailure buildFailure = UIFontAtlasBuildFailure::None;
    if (UIFontAtlasBuilder::BuildFromFile(fontPath, normalized, resolved, *built, &buildFailure) == false)
    {
        // GPU生成失敗時に空のAtlasをCacheへ登録しません。
        if (outFailure != nullptr)
        {
            *outFailure = buildFailure;
        }
        return nullptr;
    }
    m_Entries.emplace(key, built);
    outRasterScale = rasterScale;
    return built;
}

bool UIFontAtlasBuilder::BuildFromFile(
    const std::string& fontPath,
    const std::vector<std::uint32_t>& codepoints,
    const UIFontAtlasBuildOptions& options,
    UIFontAtlas& outAtlas,
    UIFontAtlasBuildFailure* outFailure)
{
    if (outFailure != nullptr)
    {
        *outFailure = UIFontAtlasBuildFailure::None;
    }
    // エラー代入を一箇所に集約し、outAtlasは成功時だけ変更します。
    const auto fail = [outFailure](UIFontAtlasBuildFailure reason) -> bool
    {
        if (outFailure != nullptr)
        {
            *outFailure = reason;
        }
        return false;
    };
    if (fontPath.empty() || std::isfinite(options.PixelHeight) == false ||
        options.PixelHeight <= 0.0f || options.AtlasWidth == 0u ||
        options.AtlasHeight == 0u || options.AtlasWidth > kMaximumAtlasSide ||
        options.AtlasHeight > kMaximumAtlasSide || options.Padding > kMaximumAtlasSide)
    {
        return fail(UIFontAtlasBuildFailure::InvalidDPIOptions);
    }

    std::ifstream stream(fontPath, std::ios::binary | std::ios::ate);
    if (stream.is_open() == false)
    {
        return fail(UIFontAtlasBuildFailure::FontFileUnavailable);
    }

    const std::streampos end = stream.tellg();
    if (end <= std::streampos(0) || end > static_cast<std::streamoff>(kMaximumFontBytes))
    {
        return fail(UIFontAtlasBuildFailure::FontDataInvalid);
    }
    const std::size_t byteCount = static_cast<std::size_t>(end);
    std::vector<unsigned char> fontBytes(byteCount);
    stream.seekg(0, std::ios::beg);
    if (stream.read(reinterpret_cast<char*>(fontBytes.data()), static_cast<std::streamsize>(byteCount)).fail())
    {
        return fail(UIFontAtlasBuildFailure::FontDataInvalid);
    }

    const int fontOffset = stbtt_GetFontOffsetForIndex(fontBytes.data(), 0);
    stbtt_fontinfo font{};
    if (fontOffset < 0 || stbtt_InitFont(&font, fontBytes.data(), fontOffset) == 0)
    {
        return fail(UIFontAtlasBuildFailure::FontDataInvalid);
    }

    const float scale = stbtt_ScaleForPixelHeight(&font, options.PixelHeight);
    if (std::isfinite(scale) == false || scale <= 0.0f)
    {
        return fail(UIFontAtlasBuildFailure::FontDataInvalid);
    }

    // UI.glslはRGBAのRGBを文字色、AをCoverageとして使用します。
    // R8をそのままImageとして描くとAlphaにCoverageが入らないため、RGBAへ展開します。
    const std::size_t width = options.AtlasWidth;
    const std::size_t height = options.AtlasHeight;
    std::vector<unsigned char> pixels(width * height * 4u, 0u);
    struct PendingGlyph
    {
        std::uint32_t Codepoint = 0u;
        UIGlyphMetrics Metrics{};
    };
    std::vector<PendingGlyph> pending;
    std::unordered_set<std::uint32_t> visited;
    std::uint32_t cursorX = options.Padding;
    std::uint32_t cursorY = options.Padding;
    std::uint32_t rowHeight = 0u;

    for (std::uint32_t codepoint : codepoints)
    {
        if (IsValidCodepoint(codepoint) == false || visited.insert(codepoint).second == false)
        {
            continue;
        }
        // 未収録文字をfontの.notdefへ黙って割り当てず、AppendTextのfallbackへ委譲します。
        if (stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint)) == 0)
        {
            continue;
        }

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBox(&font, static_cast<int>(codepoint), scale, scale, &x0, &y0, &x1, &y1);
        const int glyphWidth = x1 - x0;
        const int glyphHeight = y1 - y0;
        if (glyphWidth < 0 || glyphHeight < 0 ||
            static_cast<std::uint32_t>(glyphWidth) > options.AtlasWidth ||
            static_cast<std::uint32_t>(glyphHeight) > options.AtlasHeight)
        {
            return fail(UIFontAtlasBuildFailure::AtlasCapacityExceeded);
        }

        int advance = 0;
        int leftBearing = 0;
        stbtt_GetCodepointHMetrics(&font, static_cast<int>(codepoint), &advance, &leftBearing);
        (void)leftBearing;

        UIGlyphMetrics metrics{};
        metrics.Bearing = math::Vec2(static_cast<float>(x0), static_cast<float>(y0));
        metrics.Advance = static_cast<float>(advance) * scale;

        if (glyphWidth > 0 && glyphHeight > 0)
        {
            const std::uint32_t glyphW = static_cast<std::uint32_t>(glyphWidth);
            const std::uint32_t glyphH = static_cast<std::uint32_t>(glyphHeight);
            // 減算で比較し、Padding + GlyphWidthの整数overflowを防ぎます。
            if (options.Padding > options.AtlasWidth ||
                glyphW > options.AtlasWidth - options.Padding ||
                options.Padding > options.AtlasHeight ||
                glyphH > options.AtlasHeight - options.Padding)
            {
                return fail(UIFontAtlasBuildFailure::AtlasCapacityExceeded);
            }
            if (cursorX > options.AtlasWidth ||
                glyphW > options.AtlasWidth - cursorX)
            {
                cursorX = options.Padding;
                if (cursorY > options.AtlasHeight ||
                    rowHeight > options.AtlasHeight - cursorY)
                {
                    return fail(UIFontAtlasBuildFailure::AtlasCapacityExceeded);
                }
                cursorY += rowHeight;
                rowHeight = 0u;
            }
            if (cursorY > options.AtlasHeight || glyphH > options.AtlasHeight - cursorY)
            {
                return fail(UIFontAtlasBuildFailure::AtlasCapacityExceeded);
            }

            std::vector<unsigned char> bitmap(static_cast<std::size_t>(glyphW) * glyphH);
            stbtt_MakeCodepointBitmap(
                &font, bitmap.data(), glyphWidth, glyphHeight, glyphWidth,
                scale, scale, static_cast<int>(codepoint));

            for (std::uint32_t y = 0u; y < glyphH; ++y)
            {
                for (std::uint32_t x = 0u; x < glyphW; ++x)
                {
                    const std::size_t dst = ((static_cast<std::size_t>(cursorY + y) * width)
                        + cursorX + x) * 4u;
                    pixels[dst + 0u] = 255u;
                    pixels[dst + 1u] = 255u;
                    pixels[dst + 2u] = 255u;
                    pixels[dst + 3u] = bitmap[static_cast<std::size_t>(y) * glyphW + x];
                }
            }

            metrics.AtlasRect.Min = math::Vec2(
                static_cast<float>(cursorX), static_cast<float>(cursorY));
            metrics.AtlasRect.Max = math::Vec2(
                static_cast<float>(cursorX + glyphW), static_cast<float>(cursorY + glyphH));
            cursorX += glyphW + options.Padding;
            const std::uint32_t occupiedHeight = glyphH + options.Padding;
            if (occupiedHeight > rowHeight)
            {
                rowHeight = occupiedHeight;
            }
        }
        pending.push_back({ codepoint, metrics });
    }

    TextureSpecification specification{};
    specification.Width = options.AtlasWidth;
    specification.Height = options.AtlasHeight;
    specification.Format = TextureFormat::RGBA8;
    specification.Usage = TextureUsage::Sampled;
    specification.GenerateMips = false;
    Ref<Texture> texture = Texture::Create(specification, pixels.data(), pixels.size());
    if (texture == nullptr || texture->GetID() == 0u)
    {
        return fail(UIFontAtlasBuildFailure::TextureCreationFailed);
    }

    // Fontバイト列はRasterize終了後に解放できます。AtlasはGPU Textureだけを所有します。
    // 全処理が成功するまでoutAtlasへ触れず、失敗時に既存のAtlasを失わないようにします。
    Ref<TextureAsset> asset = CreateRef<TextureAsset>(fontPath, texture);
    UIFontAtlas built;
    if (built.Initialize(asset, options.AtlasWidth, options.AtlasHeight) == false)
    {
        return fail(UIFontAtlasBuildFailure::AtlasInitializationFailed);
    }
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    built.SetVerticalMetrics(ascent * scale, descent * scale, lineGap * scale);
    for (const PendingGlyph& entry : pending)
    {
        if (built.AddGlyph(entry.Codepoint, entry.Metrics) == false)
        {
            return fail(UIFontAtlasBuildFailure::AtlasInitializationFailed);
        }
    }
    outAtlas = std::move(built);
    return true;
}

} // namespace Raven
