#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHITypes.h"

#include <string>

namespace Raven
{

class RHIShaderAsset;

// コンパイル済みShaderを共通RHIバイナリへ変換するAsset Importerです。
// ファイル形式の検証はRHIShaderBinaryLoaderへ集約し、Asset層はRuntime Asset生成だけを担当します。
class RHIShaderAssetImporter final
{
public:
    static Ref<RHIShaderAsset> Import(
        const std::string& sourcePath,
        RHIBackend backend,
        const std::string& entryPoint = "main");
};

} // namespace Raven
