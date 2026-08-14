// Raven/Gltf/SkinImporter.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Animation/Skeleton.h"
#include "Raven/Gltf/GltfDocument.h"

namespace Raven
{
namespace Gltf
{

// ============================================================================
// ImportedSkin
// ============================================================================
// glTF skin.jointsの順序とRaven SkeletonのBone順序は一致する保証がありません。
// JOINTS_0は「skin.joints配列のindex」を保持するため、このRemapをSkinWeight Import時に
// 必ず経由してRaven::BoneIndexへ変換します。
struct ImportedSkin
{
    std::string Name;
    std::size_t SkinIndex = InvalidGltfIndex;
    std::size_t SkeletonRootNodeIndex = InvalidGltfIndex;

    Skeleton SkeletonData;

    // JointToBoneIndex[jointSlot] -> Raven BoneIndex
    // jointSlotはJOINTS_0が参照するskin.joints配列上の位置です。
    std::vector<BoneIndex> JointToBoneIndex;

    // NodeToBoneIndex[gltfNodeIndex] -> Raven BoneIndex。
    // JointではないNodeはInvalidBoneIndexです。
    std::vector<BoneIndex> NodeToBoneIndex;

    // glTF skin.jointsを元の順序のまま保持します。
    std::vector<std::size_t> JointNodeIndices;
};

// ============================================================================
// SkinImporter
// ============================================================================
// glTF skins[]をRaven::Skeletonへ変換します。
//
// 保証すること:
// - skin.jointsの重複/範囲外を拒否
// - Jointを親->子順へ並べ替えてSkeleton::AddBone()契約を満たす
// - JOINTS_0用 joint slot -> BoneIndex Remapを保持
// - inverseBindMatricesがある場合はFLOAT MAT4として読み取り、Boneへそのまま保存
// - Jointのmatrix-only Transformは現段階では拒否し、TRSをBindLocalTransformへ保存
class SkinImporter
{
public:
    static bool LoadFromGlb(
        const std::string& filePath,
        std::vector<ImportedSkin>& outSkins,
        std::string* errorMessage = nullptr);
};

} // namespace Gltf
} // namespace Raven
