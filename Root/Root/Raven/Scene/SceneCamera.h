#pragma once

#include "Raven/Renderer/Camera/Camera.h"

namespace Raven
{

// ============================================================================
// SceneCamera
// ============================================================================
// Runtime Scene内のEntityが利用するゲーム用Cameraです。
// EditorCameraとは異なり、入力や位置・回転そのものは所有せず、CameraComponentを持つEntityの
// Transformから計算されたView行列と、Camera固有のProjection設定を保持します。
//
// この責務分離により、CameraComponent導入後は次の形にできます。
//   Entity Transform      -> View Matrix
//   SceneCamera Settings  -> Projection Matrix
//   SceneViewportRenderer -> Camera共通インターフェースだけを利用
//
// 現段階では次工程のCameraComponentへ依存させず、SceneCamera単体でCameraとして成立する
// 最小実装に留めています。
class SceneCamera : public Camera
{
public:
    SceneCamera();
    ~SceneCamera() override = default;

    // CameraComponentを所有するEntityのTransformから計算したView行列を設定します。
    // SceneCamera自身にTransformを持たせないことで、Entity Transformとの二重管理を避けます。
    void SetViewMatrix(const math::Mat4& viewMatrix);

    // Game Viewの表示領域変更時にAspect Ratioを更新します。
    // width/heightそのものはCamera設定ではないため保持せず、Projection再計算に必要な比率だけを保持します。
    void SetViewportSize(float width, float height);

    // Perspective Projection設定です。
    // FOVはRavenの既存Math APIに合わせてradianで受け取ります。
    void SetPerspective(float verticalFov, float nearClip, float farClip);

    float GetPerspectiveVerticalFov() const { return m_PerspectiveVerticalFov; }
    float GetPerspectiveNearClip() const { return m_PerspectiveNearClip; }
    float GetPerspectiveFarClip() const { return m_PerspectiveFarClip; }
    float GetAspectRatio() const { return m_AspectRatio; }

    const math::Mat4& GetViewMatrix() const override { return m_ViewMatrix; }
    const math::Mat4& GetProjectionMatrix() const override { return m_ProjectionMatrix; }

private:
    void RecalculateProjection();

private:
    // ViewはEntity Transform由来です。CameraComponent導入後にScene側から更新します。
    math::Mat4 m_ViewMatrix = math::Mat4::Identity();
    math::Mat4 m_ProjectionMatrix = math::Mat4::Identity();

    // 16:9を初期値にし、CameraComponent生成直後でも有効なProjectionを持たせます。
    float m_AspectRatio = 16.0f / 9.0f;
    float m_PerspectiveVerticalFov = 0.78539816339f; // 45 degrees
    float m_PerspectiveNearClip = 0.1f;
    float m_PerspectiveFarClip = 1000.0f;
};

} // namespace Raven
