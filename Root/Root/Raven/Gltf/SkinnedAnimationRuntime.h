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
// LocomotionMode
// ============================================================================
// Stage 3ではIdle / Walk / Runを離散状態として扱います。
// Stage 4のBlendTreeではこの境界を連続Blendへ置き換えますが、Character Controllerから渡される
// 「移動速度」をAnimation側へ渡す入口はそのまま再利用できるようにします。
enum class LocomotionMode
{
    Idle,
    Walk,
    Run
};

// ============================================================================
// LocomotionClipSet
// ============================================================================
// 1 Skin分のIdle / Walk / Run Clip名と速度閾値を定義します。
//
// movementSpeed < WalkSpeedThreshold : Idle
// movementSpeed < RunSpeedThreshold  : Walk
// otherwise                           : Run
//
// CrossFadeDurationは離散Clip切り替え時のPose跳びを抑える時間です。
// Walk -> Runではrestart=falseでNormalizedTimeを引き継ぎ、足運びの位相をできるだけ維持します。
struct LocomotionClipSet
{
    std::string IdleAnimationName;
    std::string WalkAnimationName;
    std::string RunAnimationName;

    float WalkSpeedThreshold = 0.1f;
    float RunSpeedThreshold = 3.0f;
    float CrossFadeDuration = 0.15f;
};

// ============================================================================
// SkinnedAnimationRuntime
// ============================================================================
// SkinnedMeshRuntimeAssetへAnimationClip / Animatorを接続するRuntime Controllerです。
//
// Stage 2 (Idle):
// - glTF AnimationをSkinごとにAnimationClipへImportする
// - Animation名またはglTF animation indexでClipを選択する
// - AnimatorでCurrentTime / Loop / Speedを管理する
// - Animatorが評価したSkeletonPoseを同じSkinを使う全Primitiveへ同期する
//
// Stage 3 (Walk / Run):
// - Character移動速度を受け取りIdle / Walk / Runを選択する
// - Clip切り替えはAnimator::CrossFade()を利用する
// - Walk / Run間ではNormalizedTimeを維持して歩行周期の位相を繋ぐ
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

    // Animation名で直接再生します。
    bool Play(
        std::size_t skinIndex,
        const std::string& animationName,
        bool restart = true,
        std::string* errorMessage = nullptr);

    // glTF animations[]上の元indexでも再生できます。
    bool Play(
        std::size_t skinIndex,
        std::size_t sourceAnimationIndex,
        bool restart = true,
        std::string* errorMessage = nullptr);

    // 現在Motionから指定ClipへCrossFadeします。
    // restart=falseではAnimator側が現在NormalizedTimeを遷移先へ引き継ぐため、
    // Walk <-> Runのような同周期Locomotion Clip切り替えに使用します。
    bool CrossFade(
        std::size_t skinIndex,
        const std::string& animationName,
        float duration,
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

    // ========================================================================
    // Stage 3: Idle / Walk / Run
    // ========================================================================
    // 指定SkinへLocomotion Clip群を設定します。
    // Clip名・閾値をすべて検証してから設定を反映するため、途中失敗で半端な構成を残しません。
    bool ConfigureLocomotion(
        std::size_t skinIndex,
        const LocomotionClipSet& clipSet,
        std::string* errorMessage = nullptr);

    // Characterの移動速度[m/s相当の任意単位]をAnimation側へ渡します。
    // 現段階では速度閾値からIdle / Walk / Runを選択し、状態変化時だけCrossFadeを開始します。
    // 毎Frame同じ状態へPlayし直さないため、Animation周期は継続します。
    bool SetLocomotionSpeed(
        std::size_t skinIndex,
        float movementSpeed,
        std::string* errorMessage = nullptr);

    bool GetLocomotionMode(
        std::size_t skinIndex,
        LocomotionMode& outMode,
        std::string* errorMessage = nullptr) const;

    // Animatorを進め、評価済みPoseを全Primitiveへ同期し、Mesh変形まで一括更新します。
    bool Update(float deltaTime, std::string* errorMessage = nullptr);

private:
    struct SkinAnimationState
    {
        std::size_t SkinIndex = InvalidGltfIndex;
        Animator AnimatorInstance;
        std::vector<RuntimeAnimationClip> Clips;
    };

    struct LocomotionState
    {
        std::size_t SkinIndex = InvalidGltfIndex;
        LocomotionClipSet ClipSet{};
        LocomotionMode CurrentMode = LocomotionMode::Idle;
        float MovementSpeed = 0.0f;
        bool Configured = false;
        bool Started = false;
    };

    SkinAnimationState* FindSkinState(std::size_t skinIndex);
    const SkinAnimationState* FindSkinState(std::size_t skinIndex) const;

    LocomotionState* FindLocomotionState(std::size_t skinIndex);
    const LocomotionState* FindLocomotionState(std::size_t skinIndex) const;

    const RuntimeAnimationClip* FindRuntimeClip(
        const SkinAnimationState& state,
        const std::string& animationName) const;

    bool ApplyPoseToSkin(
        const SkinAnimationState& state,
        std::string* errorMessage);

private:
    // SkinnedMeshRuntimeAsset側がMesh / Deformerを所有します。
    // このControllerは再生対象を参照するだけなので、targetAssetは本クラスより長く生存する必要があります。
    SkinnedMeshRuntimeAsset* m_TargetAsset = nullptr;
    std::vector<SkinAnimationState> m_SkinStates;
    std::vector<LocomotionState> m_LocomotionStates;
};

} // namespace Gltf
} // namespace Raven
