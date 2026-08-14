// Raven/Gltf/SkinnedMeshSceneSpawner.h
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Raven/Core/Base.h"
#include "Raven/Gltf/GltfDocument.h"
#include "Raven/Math/MathQuatanion.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class Material;
class Scene;

namespace Gltf
{

class SkinnedMeshRuntimeAsset;

// ============================================================================
// SpawnedSkinnedPrimitive
// ============================================================================
// glTFの1 PrimitiveとScene Entityの対応を保持します。
// Bone操作自体はSkinnedMeshRuntimeAssetがSkin単位で同期するため、Scene側では
// Entity HandleとglTF由来indexだけを保持して描画・選択・破棄へ利用します。
struct SpawnedSkinnedPrimitive
{
    Entity EntityHandle{};

    std::size_t NodeIndex = InvalidGltfIndex;
    std::size_t MeshIndex = InvalidGltfIndex;
    std::size_t PrimitiveIndex = InvalidGltfIndex;
    std::size_t SkinIndex = InvalidGltfIndex;
};

// ============================================================================
// SkinnedMeshSceneInstance
// ============================================================================
// 1つのHuman.glbをSceneへ展開したRuntime Handleです。
// RuntimeAssetをshared ownershipすることで、EntityのMeshDeformationComponentが参照する
// MeshDeformationInstanceとBone操作入口を同じLifetimeで保持します。
class SkinnedMeshSceneInstance
{
public:
    bool IsValid() const
    {
        return m_RuntimeAsset != nullptr && m_Primitives.empty() == false;
    }

    // 現在Local Rotationを直接差し替える低レベル入口です。
    bool SetBoneLocalRotation(
        std::size_t skinIndex,
        const std::string& boneName,
        const math::Quat& rotation,
        std::string* errorMessage = nullptr);

    // Human手動確認ではこちらを使用します。
    // Import時のBind Rotationを維持し、Bone Local Spaceの追加回転だけを適用します。
    bool SetBoneLocalRotationOffsetFromBind(
        std::size_t skinIndex,
        const std::string& boneName,
        const math::Quat& rotationOffset,
        std::string* errorMessage = nullptr);

    // AssetごとにBone命名規則が異なるため、Scene/Debug UI側から実名を確認できるよう公開します。
    bool GetBoneNames(
        std::size_t skinIndex,
        std::vector<std::string>& outBoneNames,
        std::string* errorMessage = nullptr) const;

    bool ResetSkinToBindPose(
        std::size_t skinIndex,
        std::string* errorMessage = nullptr);

    const std::vector<SpawnedSkinnedPrimitive>& GetPrimitives() const
    {
        return m_Primitives;
    }

    const Ref<SkinnedMeshRuntimeAsset>& GetRuntimeAsset() const
    {
        return m_RuntimeAsset;
    }

private:
    friend class SkinnedMeshSceneSpawner;

    Ref<SkinnedMeshRuntimeAsset> m_RuntimeAsset;
    std::vector<SpawnedSkinnedPrimitive> m_Primitives;
};

// ============================================================================
// SkinnedMeshSceneSpawner
// ============================================================================
// SkinnedMeshRuntimeAssetをScene/ECSへ接続する最終Bridgeです。
//
// 各Primitive Entityへ以下を設定します。
// - TransformComponent          : glTF Node World TransformをRaven TRSへ変換
// - MeshRendererComponent      : Runtime Mesh + 指定Material
// - MeshDeformationComponent   : SkeletalMeshDeformerを持つInstance
//
// Material Importはまだ未実装なので、現段階では呼び出し側から共通Materialを受け取ります。
class SkinnedMeshSceneSpawner
{
public:
    static bool SpawnFromGlb(
        Scene& scene,
        const std::string& filePath,
        const Ref<Material>& material,
        SkinnedMeshSceneInstance& outInstance,
        std::string* errorMessage = nullptr);

    // Sceneへ生成済みの全Primitive Entityを破棄します。
    // 途中で既に破棄済みのEntityがあっても安全にskipします。
    static void Destroy(
        Scene& scene,
        SkinnedMeshSceneInstance& instance);
};

} // namespace Gltf
} // namespace Raven
