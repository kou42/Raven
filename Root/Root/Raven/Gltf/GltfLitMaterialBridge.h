// Raven/Gltf/GltfLitMaterialBridge.h
#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/Material/LitMaterialFactory.h"

namespace Raven
{
class Material;

namespace Gltf
{
struct ImportedMaterial;

// Importerが保持するglTFの意味情報を、現在のDirectional Lit描画契約へ接続します。
// glTF解析とRenderer Pipeline構築を分離したまま、Terrain/Prop側から再利用できる境界です。
class GltfLitMaterialBridge
{
public:
    static Ref<Material> Create(
        const ImportedMaterial& importedMaterial,
        const DirectionalLightSettings& light = DirectionalLightSettings{});
};

} // namespace Gltf
} // namespace Raven
