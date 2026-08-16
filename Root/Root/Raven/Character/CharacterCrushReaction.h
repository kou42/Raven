// Raven/Character/CharacterCrushReaction.h
#pragma once

#include <string>

namespace Raven
{
class CharacterController;
class Scene;

namespace Gltf
{
class SkinnedRagdollRuntime;
}

// ============================================================================
// CharacterCrushReactionConfig
// ============================================================================
// CharacterControllerが計測したCrush情報をGameplay Reactionへ変換するための閾値です。
// Controller本体へDamage / Ragdollのポリシーを持ち込まず、ゲームごとに調整できる層として分離します。
struct CharacterCrushReactionConfig
{
    // この継続時間[秒]を超えたらDamage対象とします。0ならCrushしたFrameから即対象です。
    float DamageDelay = 0.25f;

    // この継続時間[秒]を超えたらRagdoll開始条件を満たします。
    // 0以下の場合は時間条件を無効化し、Exposure条件だけで判定できます。
    float RagdollDurationThreshold = 0.75f;

    // strength * deltaTime の累積値がこの値以上ならRagdoll開始条件を満たします。
    // 0以下の場合はExposure条件を無効化します。
    float RagdollExposureThreshold = 2.0f;
};

// ============================================================================
// CharacterCrushReactionState
// ============================================================================
// 1Frame分のReaction判定結果です。Damage量そのものはHP System側の責務として持たせません。
struct CharacterCrushReactionState
{
    bool ShouldApplyDamage = false;
    bool ShouldEnterRagdoll = false;
    float Strength = 0.0f;
    float Duration = 0.0f;
    float Exposure = 0.0f;
};

// CharacterControllerの現在Crush状態を、Damage / Ragdoll開始要求へ変換します。
// 副作用を持たないため、Gameplay側で毎Frame安全に評価できます。
bool EvaluateCharacterCrushReaction(
    const CharacterController& characterController,
    const CharacterCrushReactionConfig& config,
    CharacterCrushReactionState& outState,
    std::string* errorMessage = nullptr);

// Crush ReactionでRagdoll開始が必要になった場合の統合Helperです。
// EnterRagdoll -> Constraint初期化 -> Physics Body生成を1つの明示的な入口へまとめます。
// CharacterControllerはKinematic制御を止める側の状態を持たないため、呼び出し側は成功後に
// 通常Character Updateを行わず、SkinnedRagdollRuntimeのPhysics駆動更新へ切り替えてください。
bool EnterCharacterRagdollFromCrush(
    Scene& scene,
    Gltf::SkinnedRagdollRuntime& ragdollRuntime,
    CharacterController& characterController,
    const std::string& entityNamePrefix = "CharacterCrushRagdoll",
    std::string* errorMessage = nullptr);

} // namespace Raven
