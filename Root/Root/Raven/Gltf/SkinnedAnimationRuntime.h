// Raven/Gltf/SkinnedAnimationRuntime.h
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Raven/Animation/Animator.h"
#include "Raven/Gltf/GltfDocument.h"

namespace Raven
{
namespace Gltf
{

class SkinnedMeshRuntimeAsset;

// ============================================================================
// RuntimeAnimationClip
// ============================================================================
// glTF Animationの識別情報とRaven Runtime用AnimationClipを対応付けます。
//
// AnimationClip本体には再生状態やglTF固有indexを持たせず、Import元MetadataはこのRuntime境界で
// 保持します。後続のWalk / Run / State Machine実装でもClip自体を汎用Assetとして再利用できます。
struct RuntimeAnimationClip
{
    std::size_t SourceAnimationIndex = InvalidGltfIndex;
    std::string Name;
    std::shared_ptr<AnimationClip> Clip;
};

// ============================================================================
// SkinnedAnimationRuntime
// ============================================================================
// SkinnedMeshRuntimeAssetへAnimationClip / Animatorを接続するRuntime Controllerです。
//
// Stage 2 (Idle) で担当する責務:
// - glTF AnimationをSkinごとにAnimationClipへImportする
// - Animation名またはglTF animation indexでClipを選択する
// - AnimatorでCurrentTime / Loop / Speedを管理する
// - Animatorが評価したSkeletonPoseを同じSkinを使う全Primitiveへ同期する
// - 最後にSkinnedMeshRuntimeAsset::Update()を通してCPU Skinning / GPU同期まで反映する
//
// 重要:
// Body / Clothesなど複数Primitiveが同じSkinを共有する場合、各SkeletalMeshDeformerは独立した
// SkeletonPoseを持っています。そのためAnimatorはSkinごとに1つだけ持ち、評価済みPoseを
// 同じSkinIndexの全Primitiveへ配布します。これによりPrimitiveごとの再生時刻ずれを防ぎます。
class SkinnedAnimationRuntime
{
public:
    // 既にBuild / LoadFromGlb済みのSkinnedMeshRuntimeAssetへGLB内Animationを接続します。
    // Skin定義も同じGLBから再取得し、AnimationImporterのNode -> Bone remapに使用します。
    bool AttachFromGlb(
        const std::string& filePath,
        SkinnedMeshRuntimeAsset& targetAsset,
        std::string* errorMessage = nullptr);

    // Animation名で再生します。Idle段階では通常 "Idle" などのClip名を指定します。
    bool Play(
        std::size_t skinIndex,
        const std::string& animationName,
        bool restart = true,
        std::string* errorMessage = nullptr);

    // glTF animations[]上の元indexでも再生できます。
    // AssetによってAnimation名が空/不安定な場合の低レベル入口として利用します。
    bool Play(
        std::size_t skinIndex,
        std::size_t sourceAnimationIndex,
        bool restart = true,
        std::string* errorMessage = nullptr);

    bool Pause(std::size_t skinIndex, std::string* errorMessage = nullptr);
    bool Resume(std::size_t skinIndex, std::string* errorMessage = nullptr);
    bool Stop(std::size_t skinIndex, std::string* errorMessage = nullptr);

    bool SetLoop(
        std::size_t skinIndex,
        bool loop,
        std::string* errorMessage = nullptr);

    bool SetSpeed(
        std::size_t skinIndex,
        float speed,
        std::string* errorMessage = nullptr);

    // Attach済みSkinで利用可能なAnimation名をglTF animations[]順で返します。
    bool GetAnimationNames(
        std::size_t skinIndex,
        std::vector<std::string>& outNames,
        std::string* errorMessage = nullptr) const;

    // Animatorを進め、評価済みPoseを全Primitiveへ同期し、Mesh変形まで一括更新します。
    // Idle再生側はGame LoopからこのUpdate()だけを毎Frame呼べばよい構成です。
    bool Update(float deltaTime, std::string* errorMessage = nullptr);

private:
    struct SkinAnimationState
    {
        std::size_t SkinIndex = InvalidGltfIndex;
        Animator AnimatorInstance;
        std::vector<RuntimeAnimationClip> Clips;
    };

    SkinAnimationState* FindSkinState(std::size_t skinIndex);
    const SkinAnimationState* FindSkinState(std::size_t skinIndex) const;

    bool ApplyPoseToSkin(
        const SkinAnimationState& state,
        std::string* errorMessage);

private:
    // SkinnedMeshRuntimeAsset側がMesh / Deformerを所有します。
    // このControllerは再生対象を参照するだけなので、targetAssetは本クラスより長く生存する必要があります。
    SkinnedMeshRuntimeAsset* m_TargetAsset = nullptr;
    std::vector<SkinAnimationState> m_SkinStates;
};

} // namespace Gltf
} // namespace Raven
