// Raven/Gltf/GltfLitMaterialBridge.h
#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Gltf/MaterialImporter.h"
#include "Raven/Renderer/Material/LitMaterialFactory.h"

namespace Raven
{
class Material;

namespace Gltf
{

// Importerが保持するglTFの意味情報を、現在のDirectional Lit描画契約へ接続します。
// glTF解析とRenderer Pipeline構築を分離したまま、Terrain/Prop側から再利用できる境界です。
class GltfLitMaterialBridge
{
public:
    // Alpha ModeからRenderer Surfaceへの変換規約をBridgeへ集約します。
    // 将来PBR Material Bridgeを追加する場合も同じ規約を共有でき、OPAQUE/MASK/BLENDの
    // Render Queue分類がMaterial実装ごとにずれることを防ぎます。
    static MaterialSurfaceType ResolveSurfaceType(MaterialAlphaMode alphaMode);

    static Ref<Material> Create(
        const ImportedMaterial& importedMaterial,
        const DirectionalLightSettings& light = DirectionalLightSettings{});
};

} // namespace Gltf
} // namespace Raven
