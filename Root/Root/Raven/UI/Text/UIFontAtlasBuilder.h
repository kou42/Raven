#pragma once

#include "Raven/UI/Text/UIFontAtlas.h"

#include <cstdint>
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

// Fontファイルを読み込み、指定されたコードポイントだけを事前Rasterizeします。
// 呼び出しには有効なRenderer/RHI DeviceとGPU Contextが必要です。
// 日本語全文字を無条件に展開せず、用途ごとの文字集合を指定してAtlas容量を制御します。
class UIFontAtlasBuilder
{
public:
    static bool BuildFromFile(
        const std::string& fontPath,
        const std::vector<std::uint32_t>& codepoints,
        const UIFontAtlasBuildOptions& options,
        UIFontAtlas& outAtlas);
};

} // namespace Raven
