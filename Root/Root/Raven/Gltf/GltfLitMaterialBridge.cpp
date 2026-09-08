// Raven/Gltf/GltfLitMaterialBridge.cpp
#include "Raven/Gltf/GltfLitMaterialBridge.h"

#include "Raven/Assets/TextureAsset.h"
#include "Raven/Gltf/MaterialImporter.h"
#include "Raven/Renderer/Texture/Texture.h"

namespace Raven
{
namespace Gltf
{
namespace
{
// Terrain.glbの「Geometryは存在するが画面に出ない」問題を切り分ける一時診断スイッチです。
// trueではImported Static SceneだけをTexture / Normal / Lighting非依存の固定Magentaへ置換します。
// 原因特定後はfalseへ戻し、通常のglTF Lit Material経路を再確認します。
constexpr bool ForceStaticSceneVisibilityDiagnosticMaterial = true;
}

Ref<Material> GltfLitMaterialBridge::Create(
    const ImportedMaterial& importedMaterial,
    const DirectionalLightSettings& light)
{
    if (ForceStaticSceneVisibilityDiagnosticMaterial == true)
    {
        return LitMaterialFactory::CreateStaticSceneVisibilityDiagnostic();
    }

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
