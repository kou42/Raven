// Raven/Renderer/Material/LitMaterialFactory.h
#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{

class Material;

// ============================================================================
// DirectionalLightSettings
// ============================================================================
// 最初のTerrain/Static Scene描画で使用する最小Directional Light設定です。
// Directionは「光がWorld Space内を進む方向」と定義し、Shader側でSurface->Light方向へ反転します。
struct DirectionalLightSettings
{
    math::Vec3 Direction{ -0.35f, -1.0f, -0.25f };
    math::Vec3 Color{ 1.0f, 0.96f, 0.90f };
    float Intensity = 0.85f;
    float AmbientIntensity = 0.20f;
};

// ============================================================================
// LitMaterialFactory
// ============================================================================
// Scene側へShader/Pipeline構築詳細を漏らさず、Normalを使う最小Lambert Materialを生成します。
// PBR Material Bridgeを追加するまでは、StaticSceneSpawnerへこのMaterialを渡すことで
// Terrain GLBをDirectional Light付きで確認できます。
class LitMaterialFactory
{
public:
    static Ref<Material> CreateDirectionalLit(
        const DirectionalLightSettings& light = DirectionalLightSettings{});
};

} // namespace Raven
