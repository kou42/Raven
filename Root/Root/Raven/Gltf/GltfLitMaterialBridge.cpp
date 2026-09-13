// Raven/Gltf/GltfLitMaterialBridge.cpp
#include "Raven/Gltf/GltfLitMaterialBridge.h"

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Renderer/Texture/Texture.h"

namespace Raven
{
namespace Gltf
{

MaterialSurfaceType GltfLitMaterialBridge::ResolveSurfaceType(MaterialAlphaMode alphaMode)
{
    switch (alphaMode)
    {
    case MaterialAlphaMode::Mask:
        return MaterialSurfaceType::Masked;
    case MaterialAlphaMode::Blend:
        return MaterialSurfaceType::Transparent;
    case MaterialAlphaMode::Opaque:
    default:
        return MaterialSurfaceType::Opaque;
    }
}

Ref<Material> GltfLitMaterialBridge::Create(
    const ImportedMaterial& importedMaterial,
    const DirectionalLightSettings& light)
{
    Ref<Texture> baseColorTexture;
    if (importedMaterial.BaseColorTexture != nullptr
        && importedMaterial.BaseColorTexture->IsValid())
    {
        baseColorTexture = importedMaterial.BaseColorTexture->GetTexture();
    }

    // ImporterはglTFのOPAQUE/MASK/BLENDというAsset意味だけを保持し、
    // Renderer固有のSurface分類への変換はBridge境界で行います。
    // これによりImporterが描画BackendやPipeline設計へ依存しません。
    return LitMaterialFactory::CreateDirectionalLit(
        importedMaterial.BaseColorFactor,
        baseColorTexture,
        ResolveSurfaceType(importedMaterial.AlphaMode),
        importedMaterial.AlphaCutoff,
        light);
}

} // namespace Gltf
} // namespace Raven
