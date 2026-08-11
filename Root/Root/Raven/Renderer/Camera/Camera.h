#pragma once

#include "Raven/Math/MathMatrix.h"

namespace Raven
{

// ============================================================================
// Camera
// ============================================================================
// Renderer / Sceneが「どの種類のCameraか」を意識せずに描画するための最小共通インターフェースです。
//
// Camera基底へEditor入力、Transform、FOV、Near/Far等を持たせないことが重要です。
// それらはCameraの利用目的ごとに責務が異なるため、EditorCamera / SceneCamera側で管理します。
// Renderer側が必要とするView / Projectionだけを共通化することで、今後SceneCameraを追加しても
// SceneViewportRendererの描画入口を変更せずに利用できます。
class Camera
{
public:
    virtual ~Camera() = default;

    // World座標をCamera View空間へ変換する行列を返します。
    virtual const math::Mat4& GetViewMatrix() const = 0;

    // Camera View空間をClip空間へ変換するProjection行列を返します。
    virtual const math::Mat4& GetProjectionMatrix() const = 0;
};

} // namespace Raven
