#pragma once

namespace Raven
{

class Scene;

// ============================================================================
// AnimationSystem
// ============================================================================
// AnimatorComponentを持つEntityを走査し、Animatorの再生結果をTransformへ反映する
// Scene/ECSとAnimation runtimeの橋渡しを担当します。
//
// AnimationClip / Animator側はSceneを知らないため、Animation単体の再利用性を維持しつつ、
// ECS固有の処理だけをこのSystemへ閉じ込められます。
class AnimationSystem
{
public:
    static void Update(Scene& scene, float deltaTime);
};

} // namespace Raven
