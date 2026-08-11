#include "Raven/Editor/EditorCamera.h"

#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Core/MouseCodes.h"

#include <algorithm>
#include <cmath>

namespace Raven
{

EditorCamera::EditorCamera()
{
    RecalculateProjection();
    RecalculateView();
}

void EditorCamera::Update(float deltaTime, bool inputEnabled)
{
    // Scene Viewが入力対象でない場合はCamera状態を変更しません。
    // 右ドラッグ中にWindow外へ移動した場合も、次回hoverした時に大きなMouse deltaが出ないよう
    // 回転状態だけ明示的に解除します。
    if (inputEnabled == false)
    {
        m_Rotating = false;
        return;
    }

    const bool rightMousePressed = Input::IsMouseButtonPressed(Mouse::Right);
    const auto [mouseX, mouseY] = Input::GetMousePosition();
    const math::Vec2 mousePosition{ mouseX, mouseY };

    // ========================================================================
    // Mouse look
    // ========================================================================
    // 右ボタンを押した瞬間は現在座標を基準点として保存し、次frameから差分を適用します。
    // これにより押下直後に古いMouse位置との差分でCameraが飛ぶことを防ぎます。
    if (rightMousePressed)
    {
        if (m_Rotating == false)
        {
            m_Rotating = true;
            m_LastMousePosition = mousePosition;
        }
        else
        {
            const math::Vec2 delta = mousePosition - m_LastMousePosition;
            m_LastMousePosition = mousePosition;

            m_Yaw += delta.x * m_RotationSensitivity;
            m_Pitch -= delta.y * m_RotationSensitivity;

            // Pitchを±90度の少し手前に制限し、ForwardとWorld Upが平行になって
            // Cross productからRight方向を作れなくなる特異点を避けます。
            constexpr float maxPitch = 1.55334306f; // 約89度
            m_Pitch = std::clamp(m_Pitch, -maxPitch, maxPitch);

            RecalculateView();
        }
    }
    else
    {
        m_Rotating = false;
    }

    // ========================================================================
    // Fly movement
    // ========================================================================
    // 移動は右Mouseを押している間だけ有効にします。
    // Scene View上でEntity選択など左Mouse操作を追加した際にWASDが意図せずCameraへ入ることを防ぎます。
    if (rightMousePressed == false)
    {
        return;
    }

    math::Vec3 movement{};

    if (Input::IsKeyPressed(Key::W))
    {
        movement += m_ForwardDirection;
    }

    if (Input::IsKeyPressed(Key::S))
    {
        movement -= m_ForwardDirection;
    }

    if (Input::IsKeyPressed(Key::D))
    {
        movement += m_RightDirection;
    }

    if (Input::IsKeyPressed(Key::A))
    {
        movement -= m_RightDirection;
    }

    // Q/EはWorld YではなくCamera Upではなく、Editor空間の上下としてWorld Yを使います。
    // Cameraが傾いた場合でも「上へ移動」の感覚を一定に保つためです。
    if (Input::IsKeyPressed(Key::E))
    {
        movement.y += 1.0f;
    }

    if (Input::IsKeyPressed(Key::Q))
    {
        movement.y -= 1.0f;
    }

    if (movement.LengthSq() > math::Epsilon * math::Epsilon)
    {
        movement.Normalize();
        m_Position += movement * (m_MoveSpeed * deltaTime);
        RecalculateView();
    }
}

void EditorCamera::SetViewportSize(float width, float height)
{
    if (width <= 0.0f || height <= 0.0f)
    {
        return;
    }

    if (width == m_ViewportWidth && height == m_ViewportHeight)
    {
        return;
    }

    m_ViewportWidth = width;
    m_ViewportHeight = height;
    RecalculateProjection();
}

void EditorCamera::RecalculateView()
{
    // Yaw=0 / Pitch=0でForward=(0,0,-1)になるよう定義します。
    // Sceneの既存Camera/Projectionと同じ右手系のLookAtへ渡せる方向です。
    const float cosPitch = std::cos(m_Pitch);
    m_ForwardDirection = {
        std::sin(m_Yaw) * cosPitch,
        std::sin(m_Pitch),
        -std::cos(m_Yaw) * cosPitch
    };
    m_ForwardDirection.Normalize();

    const math::Vec3 worldUp{ 0.0f, 1.0f, 0.0f };
    m_RightDirection = math::Vec3::Cross(m_ForwardDirection, worldUp).Normalized();
    m_UpDirection = math::Vec3::Cross(m_RightDirection, m_ForwardDirection).Normalized();

    m_ViewMatrix = math::Mat4::LookAt(
        m_Position,
        m_Position + m_ForwardDirection,
        m_UpDirection);
}

void EditorCamera::RecalculateProjection()
{
    if (m_ViewportWidth <= 0.0f || m_ViewportHeight <= 0.0f)
    {
        return;
    }

    const float aspectRatio = m_ViewportWidth / m_ViewportHeight;
    m_ProjectionMatrix = math::Mat4::Perspective(
        m_FieldOfViewY,
        aspectRatio,
        m_NearClip,
        m_FarClip);
}

} // namespace Raven
