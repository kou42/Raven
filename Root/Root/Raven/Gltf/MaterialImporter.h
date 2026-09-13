// Raven/Gltf/MaterialImporter.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Gltf/GltfDocument.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{

class TextureAsset;

namespace Gltf
{

enum class MaterialAlphaMode
{
    Opaque = 0,
    Mask,
    Blend
};

// ============================================================================
// ImportedMaterial
// ============================================================================
// glTF Materialのうち、Renderer Pipelineへ依存せずAsset変換できる情報を保持します。
//
// MaterialそのものをここでRenderer::Materialへ変換しない理由は、使用Shader/Pipelineと
// uniform名は描画Pass側の契約だからです。ImporterはglTFの意味情報とTextureAssetまでを作り、
// Scene/Renderer側が各PipelineのMaterialへ接続する境界を維持します。
struct ImportedMaterial
{
    std::string Name;

    math::Vec4 BaseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
    Ref<TextureAsset> BaseColorTexture;

    // glTF 2.0ではalphaMode省略時はOPAQUE、alphaCutoff省略時は0.5です。
    // Renderer固有enumをImporterへ持ち込まず、glTFの意味情報として保持します。
    MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
    float AlphaCutoff = 0.5f;

    std::size_t BaseColorTextureIndex = InvalidGltfIndex;
    std::size_t BaseColorImageIndex = InvalidGltfIndex;
    std::size_t BaseColorTexCoord = 0u;
};

// ============================================================================
// MaterialImporter
// ============================================================================
// glTF 2.0 PBR Materialの最小Import経路です。
// 現段階ではbaseColorFactor / baseColorTexture / alphaMode / alphaCutoffを対象にし、
// Metallic-RoughnessやNormal等は同じ構造へ後から段階的に追加します。
//
// .glb内のbufferView画像はTextureAssetImporter::ImportMemory()へ渡します。
// これによりGltf層がstb_imageやOpenGL Texture生成を直接扱わず、通常画像と同じAssets経路を通ります。
class MaterialImporter
{
public:
    static bool LoadFromGlb(
        const std::string& filePath,
        std::vector<ImportedMaterial>& outMaterials,
        std::string* errorMessage = nullptr);
};

} // namespace Gltf
} // namespace Raven
