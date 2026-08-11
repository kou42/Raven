#pragma once

#include "Raven/Renderer/Camera/Camera.h"
#include "Raven/Math/MathVector.h"

namespace Raven
{

// ============================================================================
// EditorCamera
// ============================================================================
// Scene View専用の自由移動Cameraです。
// Runtime Sceneが所有するゲーム用Cameraとは完全に独立した状態を持ち、Editor操作によって
// ゲーム本来のCamera位置やProjectionが変更されないようにします。
//
// Camera基底はRendererが必要とするView / Projectionだけを公開します。
// Editor固有の入力・移動・Yaw/Pitch等はこのクラスへ残すことで、将来追加するSceneCameraへ
// Editor依存の責務が混入しない構造にしています。
//
// 操作方針:
//   Right Mouse Drag : Yaw / Pitchによる視点回転
//   W / S            : Forward / Backward
//   A / D            : Left / Right
//   Q / E            : Down / Up
//
// 入力を受け付けるかどうかはEditorLayerがScene Viewのhover状態から決定し、Update()へ渡します。
// EditorCamera自身がImGui Windowを知らない構造にすることで、Cameraの数学処理とEditor UIを分離します。
class EditorCamera : public Camera
{
public:
    EditorCamera();
    ~EditorCamera() override = default;

    // Scene Viewが入力対象になっているframeだけCamera操作を更新します。
    // inputEnabled=falseの場合でもProjection/View行列は有効なまま保持されます。
    void Update(float deltaTime, bool inputEnabled);

    // Scene ViewのContentRegionサイズ変更へProjectionを追従させます。
    // 0以下のサイズは無効値として無視し、直前のProjectionを維持します。
    void SetViewportSize(float width, float height);

    // Camera共通インターフェースです。
    // SceneViewportRendererはEditorCameraという具体型を知らず、この2つだけを利用します。
    const math::Mat4& GetViewMatrix() const override { return m_ViewMatrix; }
    const math::Mat4& GetProjectionMatrix() const override { return m_ProjectionMatrix; }

    const math::Vec3& GetPosition() const { return m_Position; }
    const math::Vec3& GetForwardDirection() const { return m_ForwardDirection; }

private:
    // Yaw/PitchからCameraのForward / Right / Up基底とView行列を再構築します。
    void RecalculateView();

    // 現在のFOV / Near / FarとViewport AspectからProjectionを再構築します。
    void RecalculateProjection();

private:
    // Runtime Cameraと見分けやすい初期位置からScene原点方向を見る設定です。
    math::Vec3 m_Position{ 0.0f, 8.0f, 18.0f };

    math::Vec3 m_ForwardDirection{ 0.0f, 0.0f, -1.0f };
    math::Vec3 m_RightDirection{ 1.0f, 0.0f, 0.0f };
    math::Vec3 m_UpDirection{ 0.0f, 1.0f, 0.0f };

    math::Mat4 m_ViewMatrix = math::Mat4::Identity();
    math::Mat4 m_ProjectionMatrix = math::Mat4::Identity();

    float m_ViewportWidth = 1280.0f;
    float m_ViewportHeight = 720.0f;

    // Ravenの角度はradianで扱います。
    // Yaw=0で-Z方向を向くようRecalculateView()側の式を定義しています。
    float m_Yaw = 0.0f;
    float m_Pitch = -0.28f;

    float m_FieldOfViewY = 0.78539816339f; // 45 degrees
    float m_NearClip = 0.1f;
    float m_FarClip = 1000.0f;

    float m_MoveSpeed = 12.0f;
    float m_RotationSensitivity = 0.0035f;

    // Mouse deltaは前frame座標との差分で求めるため、右ドラッグ開始時の初期化状態を保持します。
    bool m_Rotating = false;
    math::Vec2 m_LastMousePosition{};
};

} // namespace Raven
