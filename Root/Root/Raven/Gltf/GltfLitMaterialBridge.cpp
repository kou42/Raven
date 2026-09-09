// Raven/Gltf/GltfLitMaterialBridge.cpp
#include "Raven/Gltf/GltfLitMaterialBridge.h"

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Gltf/MaterialImporter.h"
#include "Raven/Renderer/Texture/Texture.h"

namespace Raven
{
namespace Gltf
{

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

    // ImportedMaterialはglTF意味情報、LitMaterialFactoryはRenderer契約を担当します。
    // Bridgeでは値の対応付けだけを行い、ImporterへShader依存を逆流させません。
    return LitMaterialFactory::CreateDirectionalLit(
        importedMaterial.BaseColorFactor,
        baseColorTexture,
        light);
}

} // namespace Gltf
} // namespace Raven
