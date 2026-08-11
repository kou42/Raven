#pragma once

#include "Raven/Scene/Entity.h"

namespace Raven
{

class Scene;
class SceneCamera;

// ============================================================================
// SceneCameraSystem
// ============================================================================
// Scene内のCameraComponentを探索し、Game Viewで利用するRuntime Cameraを解決するための
// Scene側Systemです。
//
// CameraComponentは「Camera設定データ」、TransformComponentは「Entityの姿勢」を保持し、
// このSystemが両者を結合してSceneCameraのView Matrixを更新します。
// CameraComponent自身へScene探索責務を持たせないことで、Componentを純粋なデータとして保ちます。
class SceneCameraSystem
{
public:
    // Primary=trueのCameraComponentを持つ最初の生存Entityを返します。
    // Primaryが複数存在する場合も、ECS View上で最初に見つかったEntityを採用します。
    // 見つからない場合は無効Entityを返します。
    static Entity FindPrimaryCameraEntity(Scene& scene);

    // Runtime Cameraとして利用可能なEntityを解決します。
    //
    // 優先順位:
    //   1. Primary=true のCamera Entity
    //   2. Primaryが存在しない場合、最初のCamera Entityをfallbackとして利用
    //
    // Primary設定をInspector操作などで一時的に全解除してもGame Viewを失わないための
    // 安全な解決入口です。Camera Entity自体が存在しない場合のみ無効Entityを返します。
    static Entity ResolveRuntimeCameraEntity(Scene& scene);

    // Runtime Cameraを解決し、TransformからView Matrixを更新したSceneCameraを返します。
    // Game Viewのサイズも同時に反映するため、ProjectionのAspect Ratioも最新状態になります。
    // Camera Entityが1つも存在しない場合はnullptrを返します。
    static SceneCamera* UpdatePrimaryCamera(
        Scene& scene,
        float viewportWidth,
        float viewportHeight);
};

} // namespace Raven
