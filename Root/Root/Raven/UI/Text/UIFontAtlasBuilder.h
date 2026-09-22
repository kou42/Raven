#pragma once

#include "Raven/UI/Text/UIFontAtlas.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>
#include <string>
#include <vector>

namespace Raven
{

struct UIFontAtlasBuildOptions
{
    float PixelHeight = 20.0f;
    std::uint32_t AtlasWidth = 1024u;
    std::uint32_t AtlasHeight = 1024u;
    std::uint32_t Padding = 1u;
};

// Font Atlas構築の失敗段階を表します。TextureCreationFailedはnative Resource生成失敗、
// TextureDeviceUnavailableはRHI Device未初期化、TextureUploadFailedはpixel転送失敗です。
enum class UIFontAtlasBuildFailure
{
    None,
    InvalidDPIOptions,
    FontFileUnavailable,
    FontDataInvalid,
    AtlasCapacityExceeded,
    TextureCreationFailed,
    TextureDeviceUnavailable,
    TextureUploadFailed,
    AtlasInitializationFailed
};

// Fontファイルを読み込み、指定されたコードポイントだけを事前Rasterizeします。
// 呼び出しには有効なRenderer/RHI DeviceとGPU Contextが必要です。
// 日本語全文字を無条件に展開せず、用途ごとの文字集合を指定してAtlas容量を制御します。
class UIFontAtlasBuilder
{
public:
    // DIP基準のPixelHeightから高解像度AtlasのRasterize高さを決定します。
    // scaleは1/8刻みに量子化し、Monitor移動時の微小変化による再生成を抑えます。
    static bool ResolveDPIOptions(
        const UIFontAtlasBuildOptions& baseOptions,
        float effectiveScale,
        UIFontAtlasBuildOptions& outOptions,
        float& outRasterScale);

    static bool BuildFromFile(
        const std::string& fontPath,
        const std::vector<std::uint32_t>& codepoints,
        const UIFontAtlasBuildOptions& options,
        UIFontAtlas& outAtlas,
        UIFontAtlasBuildFailure* outFailure = nullptr);
};

// GPU Contextを持つ呼び出し側が所有する明示Cacheです。
// Fontパス・文字集合・Atlas設定・量子化したDPI倍率が一致するAtlasだけを再利用します。

class UIFontAtlasDPICache
{
public:
    // GPU生成を伴わず、生成済みAtlasだけを検索します。DPI通知から安全に呼べます。
    Ref<UIFontAtlas> Find(
        const std::string& fontPath,
        const std::vector<std::uint32_t>& codepoints,
        const UIFontAtlasBuildOptions& baseOptions,
        float effectiveScale,
        float& outRasterScale) const;

    Ref<UIFontAtlas> GetOrBuild(
        const std::string& fontPath,
        const std::vector<std::uint32_t>& codepoints,
        const UIFontAtlasBuildOptions& baseOptions,
        float effectiveScale,
        float& outRasterScale,
        UIFontAtlasBuildFailure* outFailure = nullptr);
    void Clear() { m_Entries.clear(); }
    std::size_t GetEntryCount() const { return m_Entries.size(); }

private:
    using Key = std::tuple<std::string, std::vector<std::uint32_t>, float,
        std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;
    std::map<Key, Ref<UIFontAtlas>> m_Entries;
};

} // namespace Raven
