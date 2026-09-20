#pragma once

#include "Raven/Renderer/RHI/RHITexture.h"

#include <array>

namespace Raven
{

// API非依存のSurface分類。Maskedのalpha cutoffは各描画経路のShaderで実装します。
enum class MaterialSurfaceType
{
    Opaque = 0,
    Masked,
    Transparent
};

// Legacy Pipeline/Shaderを持たない描画用Material Snapshotです。
// Texture未指定時の既定Texture選択は描画側が担当します。
struct RHIMaterialProperties
{
    std::array<float, 4> Tint = {1.0f, 1.0f, 1.0f, 1.0f};
    Ref<RHITexture> Texture;
    MaterialSurfaceType SurfaceType = MaterialSurfaceType::Opaque;
};

} // namespace Raven
