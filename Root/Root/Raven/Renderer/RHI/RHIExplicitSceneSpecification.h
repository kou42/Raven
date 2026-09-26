#pragma once

#include "Raven/Assets/RHIShaderAsset.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"

namespace Raven
{

// Explicit Scene Runtimeを初期化するBackend非依存設定です。
// Application / Demoのどちらから起動しても同じPipeline・Shader境界を使用し、
// Platform固有Runtimeへ設定値を直接散らさないための値型として扱います。
struct RHIExplicitSceneSpecification
{
    PipelineSpecification Pipeline{};
    RHIShaderAssetSpecification VertexShader{};
    RHIShaderAssetSpecification FragmentShader{};
};

} // namespace Raven
