// Raven/Gltf/StaticSceneImporter.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Gltf/GltfDocument.h"
#include "Raven/Math/MathMatrix.h"

namespace Raven
{

class MeshGeometry;

namespace Gltf
{

// ============================================================================
// ImportedStaticMeshInstance
// ============================================================================
// Mesh PrimitiveのGeometryと、それを配置するglTF NodeのWorld Transformを組にした結果です。
// Geometry自体へTransformをBakeしないため、同一Meshを複数Nodeが参照するglTFでも
// CPU頂点データを複製せずにInstanceとして扱えます。
struct ImportedStaticMeshInstance
{
    Ref<MeshGeometry> Geometry;
    math::Mat4 WorldTransform = math::Mat4::Identity();

    std::string NodeName;
    std::string MeshName;

    std::size_t NodeIndex = InvalidGltfIndex;
    std::size_t MeshIndex = InvalidGltfIndex;
    std::size_t PrimitiveIndex = InvalidGltfIndex;
    std::size_t MaterialIndex = InvalidGltfIndex;
};

// ============================================================================
// StaticSceneImporter
// ============================================================================
// StaticMeshImporterとNodeHierarchyを結び、Scene内で実際に表示すべきMesh Instanceを構築します。
//
// この段階ではScene/ECS Entityを直接生成しません。
// ImporterはAsset変換だけを担当し、Entity生成は呼び出し側へ残すことで依存方向を
// Gltf -> Rendererまでに留めます。
class StaticSceneImporter
{
public:
    static bool LoadFromGlb(
        const std::string& filePath,
        std::vector<ImportedStaticMeshInstance>& outInstances,
        std::string* errorMessage = nullptr);
};

} // namespace Gltf
} // namespace Raven
