// Raven/Renderer/Material/LitMaterialFactory.h
#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{

class Material;
class Texture;

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
// glTF Material Bridgeからも同じ経路を利用し、baseColorFactor / baseColorTextureだけを
// Renderer Materialへ接続します。Metallic-Roughness等は後続のPBR段階で追加します。
class LitMaterialFactory
{
public:
    static Ref<Material> CreateDirectionalLit(
        const DirectionalLightSettings& light = DirectionalLightSettings{});

    static Ref<Material> CreateDirectionalLit(
        const math::Vec4& baseColorFactor,
        const Ref<Texture>& baseColorTexture,
        const DirectionalLightSettings& light = DirectionalLightSettings{});

    // Static Sceneの可視性問題をRenderer/Material層で切り分ける診断専用Materialです。
    // Texture / Normal / Lightingを使わず固定色で描画し、Cullも無効化します。
    // 本番表現用ではなく、Geometry -> Camera -> Rasterize経路の確認だけに使用します。
    static Ref<Material> CreateStaticSceneVisibilityDiagnostic();
};

} // namespace Raven
