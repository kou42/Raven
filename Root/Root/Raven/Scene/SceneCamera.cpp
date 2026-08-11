#include "Raven/Scene/SceneCamera.h"

namespace Raven
{

SceneCamera::SceneCamera()
{
    // SceneCamera生成直後から有効なProjectionを持たせます。
    // CameraComponent追加直後やViewport同期前でもIdentity Projectionにならないため、
    // Runtime Cameraとして安全に扱えます。
    RecalculateProjection();
}

void SceneCamera::SetViewMatrix(const math::Mat4& viewMatrix)
{
    // View Matrixの生成責務はSceneCameraSystem側にあります。
    // SceneCameraはEntity Transformを直接知らず、Cameraとして必要な最終行列だけを保持します。
    m_ViewMatrix = viewMatrix;
}

void SceneCamera::SetViewportSize(float width, float height)
{
    // ========================================================================
    // Viewport validation
    // ========================================================================
    // Window最小化やImGui Panelの一時的な0サイズによってCamera設定を壊さないよう、
    // 有効な幅・高さが得られた場合だけAspect Ratioを更新します。
    if (width <= 0.0f || height <= 0.0f)
    {
        return;
    }

    const float aspectRatio = width / height;

    // 同じViewportサイズが毎frame渡されてもProjectionを不要に再計算しません。
    // 現状は単純なMatrix生成ですが、Camera更新経路の責務を明確にするためここで弾きます。
    if (aspectRatio == m_AspectRatio)
    {
        return;
    }

    m_AspectRatio = aspectRatio;
    RecalculateProjection();
}

void SceneCamera::SetPerspective(float verticalFov, float nearClip, float farClip)
{
    // ========================================================================
    // Perspective validation
    // ========================================================================
    // SceneCameraはInspectorやSceneロードなど複数経路から設定されるため、Projectionが破綻する値を
    // 内部状態へ保存しないことをCamera自身の契約とします。
    //
    // nearClip > 0 / farClip > nearClip はPerspective Matrix成立に必要な最低条件です。
    // FOVのUI上限などEditor固有の制約はInspector側で行い、SceneCamera側では正値のみ要求します。
    if (verticalFov <= 0.0f)
    {
        return;
    }

    if (nearClip <= 0.0f)
    {
        return;
    }

    if (farClip <= nearClip)
    {
        return;
    }

    // 値が変わっていない場合はProjection再計算を省略します。
    // Inspectorが毎frame現在値を読み出す構造でも余計なMatrix生成を行いません。
    if (verticalFov == m_PerspectiveVerticalFov
        && nearClip == m_PerspectiveNearClip
        && farClip == m_PerspectiveFarClip)
    {
        return;
    }

    m_PerspectiveVerticalFov = verticalFov;
    m_PerspectiveNearClip = nearClip;
    m_PerspectiveFarClip = farClip;

    RecalculateProjection();
}

void SceneCamera::RecalculateProjection()
{
    // ========================================================================
    // Projection Matrix generation
    // ========================================================================
    // Projectionの生成は必ずこの関数へ集約します。
    // SetViewportSize()とSetPerspective()の双方から同じ計算経路を利用することで、
    // Aspect/FOV/Near/Farのどれを変更しても同じ規則でProjectionを更新できます。
    if (m_AspectRatio <= 0.0f)
    {
        return;
    }

    m_ProjectionMatrix = math::Mat4::Perspective(
        m_PerspectiveVerticalFov,
        m_AspectRatio,
        m_PerspectiveNearClip,
        m_PerspectiveFarClip);
}

} // namespace Raven
