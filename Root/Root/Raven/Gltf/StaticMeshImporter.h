// Raven/Gltf/StaticMeshImporter.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Gltf/GltfDocument.h"

namespace Raven
{

class MeshGeometry;

namespace Gltf
{

// ============================================================================
// ImportedStaticPrimitive
// ============================================================================
// glTFでは1 Meshが複数Primitive(Material境界など)を持てます。
// Raven::MeshGeometryは1つの頂点/Index集合なので、Phase 2ではPrimitive単位で
// Geometryへ変換して保持します。
struct ImportedStaticPrimitive
{
    std::string MeshName;
    std::size_t MeshIndex = InvalidGltfIndex;
    std::size_t PrimitiveIndex = InvalidGltfIndex;
    std::size_t MaterialIndex = InvalidGltfIndex;
    Ref<MeshGeometry> Geometry;
};

// ============================================================================
// StaticMeshImporter
// ============================================================================
// glTF/GLBのMesh PrimitiveをRaven::MeshGeometryへ変換します。
//
// Phase 2の目的はSkin / Node / Animationを混ぜず、純粋なMesh Attributeだけで
// HumanのT-Pose形状を再現できることを確認することです。
// そのため現段階ではNode Transformを適用せず、Mesh Local SpaceのGeometryを返します。
class StaticMeshImporter
{
public:
    static bool LoadFromGlb(
        const std::string& filePath,
        std::vector<ImportedStaticPrimitive>& outPrimitives,
        std::string* errorMessage = nullptr);
};

} // namespace Gltf
} // namespace Raven
