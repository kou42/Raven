#pragma once

#include "Raven/Renderer/RHI/RHIGraphicsPipelineSpecification.h"

#include <filesystem>
#include <string>

namespace Raven
{

// Backend別のコンパイル済みShaderを、共通RHI表現へ読み込む入口です。
// 読み込み・形式検証が完了してから出力を差し替えるため、失敗時は既存値を維持します。
class RHIShaderBinaryLoader final
{
public:
    static bool LoadFromFile(
        const std::filesystem::path& path,
        RHIShaderBinaryFormat format,
        RHIShaderBinary& outShader,
        const std::string& entryPoint = "main");

    // Backendから必要なバイナリ形式を決定します。
    // 現在コンパイル済みShaderを利用するVulkan / DirectX12だけを受け付けます。
    static bool LoadForBackend(
        const std::filesystem::path& path,
        RHIBackend backend,
        RHIShaderBinary& outShader,
        const std::string& entryPoint = "main");
};

} // namespace Raven
