// Raven/Gltf/SkinnedMeshImporter.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Animation/SkinnedMeshData.h"
#include "Raven/Core/Base.h"
#include "Raven/Gltf/GltfDocument.h"
#include "Raven/Gltf/SkinImporter.h"
#include "Raven/Math/MathMatrix.h"

namespace Raven
{

class MeshGeometry;

namespace Gltf
{

// ============================================================================
// ImportedSkinnedPrimitive
// ============================================================================
// 1つのglTF Mesh Primitiveを、RavenのDynamic MeshGeometryとSkinning入力へ変換した結果です。
// Skeleton自体はImportedSkinnedAsset::SkinsへAsset単位で保持し、ここではSkinIndexで参照します。
// 同じSkeletonをBody / Clothesなど複数Primitiveが共有しても定義データを複製しません。
struct ImportedSkinnedPrimitive
{
    std::string MeshName;

    std::size_t NodeIndex = InvalidGltfIndex;
    std::size_t MeshIndex = InvalidGltfIndex;
    std::size_t PrimitiveIndex = InvalidGltfIndex;
    std::size_t MaterialIndex = InvalidGltfIndex;
    std::size_t SkinIndex = InvalidGltfIndex;

    // glTF Node階層をすべて合成したScene SpaceのGlobal Transformです。
    // glTF 2.0の基準座標系は+Y upで固定され、Authoring Tool由来の基底変換もNode階層へ含まれます。
    // Raven World Spaceへの基底変換はScene配置境界でBuildGltfToRavenWorldTransform()を通して適用します。
    // Skinning計算自体はMesh Local / Bind Spaceで行い、このTransformをSkeleton内部へ混ぜません。
    math::Mat4 WorldTransform = math::Mat4::Identity();

    // SkeletalMeshDeformerが頂点を更新するため、StaticMeshImporterのGeometryをそのまま使わず
    // GeometryUsage::Dynamicとして複製したGeometryを保持します。
    Ref<MeshGeometry> Geometry;

    // Geometryの頂点順と1:1対応するBind Position / SkinWeightです。
    SkinnedMeshData SkinningData;
};

struct ImportedSkinnedAsset
{
    // glTF skins[]と同じindex順で保持します。
    // Primitive::SkinIndexからSkeletonData / JointToBoneIndexへアクセスできます。
    std::vector<ImportedSkin> Skins;
    std::vector<ImportedSkinnedPrimitive> Primitives;
};

// ============================================================================
// SkinnedMeshImporter
// ============================================================================
// JOINTS_0 / WEIGHTS_0を既存ImportedSkinのJointToBoneIndexへ接続し、
// Raven::SkinWeight / SkinnedMeshDataへ変換します。
//
// 現段階ではglTF 2.0の基本4 Influenceだけを対象とし、JOINTS_1 / WEIGHTS_1は未対応です。
// 対応範囲を曖昧に広げず、まず既存CPU SkeletalMeshDeformerへ確実に接続できる形を作ります。
class SkinnedMeshImporter
{
public:
    static bool LoadFromGlb(
        const std::string& filePath,
        ImportedSkinnedAsset& outAsset,
        std::string* errorMessage = nullptr);
};

} // namespace Gltf
} // namespace Raven
