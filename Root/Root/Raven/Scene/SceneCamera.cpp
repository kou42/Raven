#include "Raven/Scene/SceneCamera.h"

namespace Raven
{

SceneCamera::SceneCamera()
{
    RecalculateProjection();
}

void SceneCamera::SetViewMatrix(const math::Mat4& viewMatrix)
{
    m_ViewMatrix = viewMatrix;
}

void SceneCamera::SetViewportSize(float width, float height)
{
    // 最小化中などに0サイズが渡された場合、直前の有効なProjectionを維持します。
    // 0除算を避けるだけでなく、一時的なUI状態でCamera設定を壊さないための防御です。
    if (width <= 0.0f || height <= 0.0f)
    {
        return;
    }

    const float aspectRatio = width / height;
    if (aspectRatio == m_AspectRatio)
    {
        return;
    }

    m_AspectRatio = aspectRatio;
    RecalculateProjection();
}

void SceneCamera::SetPerspective(float verticalFov, float nearClip, float farClip)
{
    // Projection Matrixが破綻する設定をCamera内部へ保持しないようにします。
    // Inspectorから編集可能にする段階でも、この条件をCamera側の最後の防御として利用できます。
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

    m_PerspectiveVerticalFov = verticalFov;
    m_PerspectiveNearClip = nearClip;
    m_PerspectiveFarClip = farClip;

    RecalculateProjection();
}

void SceneCamera::RecalculateProjection()
{
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
