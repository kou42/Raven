#pragma once

namespace Raven
{

class Scene;

// ============================================================================
// MeshDeformationSystem
// ============================================================================
// MeshDeformationComponentを持つEntityを一括更新するECS Systemです。
// SceneやゲームコードがWave/Skeletal/Morph/SoftBodyなどの具体的なDeformerを
// 知らなくて済むよう、変形更新の入口をここへ集約します。
class MeshDeformationSystem
{
public:
    static void Update(Scene& scene, float deltaTime);
};

} // namespace Raven
