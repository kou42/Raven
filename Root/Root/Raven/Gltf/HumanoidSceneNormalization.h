// Raven/Gltf/HumanoidSceneNormalization.h
#pragma once

#include <string>
#include <vector>

#include "Raven/Gltf/SkinnedMeshSceneSpawner.h"

namespace Raven
{
namespace Gltf
{

// ============================================================================
// NormalizeHumanoidSceneInstance
// ============================================================================
// Spawn済みHumanoidを「直立・指定身長・足元原点」へ正規化する共通入口です。
//
// 重要:
// - Humanoidの上方向はAABB長軸から推測しません。
// - SkeletalMeshDeformerが保持するBind Space補正を通して
//   Skeleton Parent -> Mesh Local -> Entity World のHips/Pelvis -> Head方向を追跡します。
// - AABBは直立方向を決めた後の身長計測・中央寄せ・足元合わせだけに使用します。
// - targetHeightを呼び出し側から受け取るため、20mのDebug表示と約1.8mのCharacter表示を
//   同じSkinning補正ロジックで安全に使い分けられます。
//
// CharacterControllerへ接続する場合は、Capsule全高
//   2 * (CapsuleHalfLength + CapsuleRadius)
// をtargetHeightへ渡すことで、表示HumanとCollision Capsuleのスケール基準を一致させられます。
bool NormalizeHumanoidSceneInstance(
    const SkinnedMeshSceneInstance& instance,
    std::vector<SpawnedSkinnedPrimitive>& primitives,
    float targetHeight,
    std::string* errorMessage = nullptr);

} // namespace Gltf
} // namespace Raven
