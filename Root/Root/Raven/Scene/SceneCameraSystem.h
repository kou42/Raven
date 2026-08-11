#pragma once

#include "Raven/Scene/Entity.h"

namespace Raven
{

class Scene;
class SceneCamera;

// ============================================================================
// SceneCameraSystem
// ============================================================================
// Scene内のCameraComponentを探索し、Game Viewで利用するPrimary Cameraを解決するための
// Scene側Systemです。
//
// CameraComponentは「Camera設定データ」、TransformComponentは「Entityの姿勢」を保持し、
// このSystemが両者を結合してSceneCameraのView Matrixを更新します。
// CameraComponent自身へScene探索責務を持たせないことで、Componentを純粋なデータとして保ちます。
class SceneCameraSystem
{
public:
    // Primary=trueのCameraComponentを持つ最初の生存Entityを返します。
    // 見つからない場合は無効Entityを返します。
    static Entity FindPrimaryCameraEntity(Scene& scene);

    // Primary Cameraを探索し、TransformからView Matrixを更新したSceneCameraを返します。
    // Game Viewのサイズも同時に反映するため、ProjectionのAspect Ratioも最新状態になります。
    // Primary Cameraが存在しない場合はnullptrを返します。
    static SceneCamera* UpdatePrimaryCamera(
        Scene& scene,
        float viewportWidth,
        float viewportHeight);
};

} // namespace Raven
