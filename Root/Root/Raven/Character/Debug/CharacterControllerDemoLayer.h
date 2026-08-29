// Raven/Character/Debug/CharacterControllerDemoLayer.h
#pragma once

#include "Raven/Character/CharacterController.h"
#include "Raven/Core/Base.h"
#include "Raven/Core/Input.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class Material;
class Mesh;
class Scene;

// ============================================================================
// CharacterControllerDemoLayer
// ============================================================================
// Gamepad -> CharacterControllerInput -> CharacterController -> Scene Transform
// の一連のRuntime経路を目視確認するための最小デモLayerです。
//
// このLayerはApplication LayerではなくScene-owned Layerとして登録します。
// Scene::OnUpdate()の Physics -> Scene Layer -> Render の順序へ入るため、Characterの
// Transform更新が同じFrameのScene描画へ反映され、Application Layerで発生する1Frameの表示遅延を避けます。
//
// CharacterController本体はKinematic Capsuleを内部のShape Castとして扱い、ECS上に
// Character用RigidBody/Colliderを生成しません。そのため表示用EntityにもColliderを付けず、
// Character自身のCapsule Castが自分の表示Entityへ衝突する自己衝突を避けます。
//
// 現段階の標準Gamepad操作:
//   Left Stick  : Runtime Camera基準でXZ平面を移動
//   Right Stick : Characterを中心にRuntime CameraをYaw / Pitch回転
//   A           : Jump
//   RT          : Run
//
// Raw Gamepad値とCharacterControllerInput変換後の値を保持し、
// Dead Zone / Trigger Threshold / Button MappingをDebuggerや後続Debug UIから比較できるようにします。
// Camera-relative movementは入力Device層ではなく、このGameplay/Camera統合LayerでWorld方向へ変換します。
// これによりCharacterController本体はCameraを知らず、従来どおりWorld XZ入力だけを処理できます。
class CharacterControllerDemoLayer final : public Layer
{
public:
    explicit CharacterControllerDemoLayer(Scene& scene)
        : m_Scene(scene)
    {
    }

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float deltaTime) override;

    bool IsGamepadConnected() const
    {
        return m_GamepadConnected;
    }

    const GamepadState& GetRawGamepadState() const
    {
        return m_RawGamepadState;
    }

    const CharacterControllerInput& GetResolvedInput() const
    {
        return m_ResolvedInput;
    }

private:
    // CharacterControllerが更新するTransformは「足元Root」です。
    // 表示用Cubeは原点中心なので、RootからCapsule全高の半分だけ上へずらして同期します。
    void SyncVisualTransform();

    // Raw Device値をGameplay入力へ変換する前に保存します。
    // CharacterController::ReadDefaultGamepadInput()がDead Zone等を適用した結果と並べて確認することで、
    // Controller側の挙動不良がDevice入力なのかMappingなのかを切り分けられます。
    void CaptureGamepadDebugState();

    // Left StickのDevice非依存MoveをRuntime Camera基準のWorld XZ Moveへ変換します。
    // Camera Pitchは移動方向へ含めず、地面に投影したForward/Rightだけを利用します。
    void ApplyCameraRelativeMovement(CharacterControllerInput& input) const;

    // Primary Runtime CameraをCharacter中心のOrbit Cameraとして更新します。
    // CameraComponentのView Matrixを直接変更せずTransformComponentだけを更新し、
    // SceneCameraSystemをCamera姿勢同期の唯一の入口として維持します。
    void UpdateGamepadCamera(float deltaTime);

private:
    Scene& m_Scene;

    CharacterController m_CharacterController{};
    TransformComponent m_CharacterRootTransform{};

    Entity m_CharacterEntity{};
    Ref<Mesh> m_CharacterMesh;
    Ref<Material> m_CharacterMaterial;

    bool m_GamepadConnected = false;
    GamepadState m_RawGamepadState{};
    CharacterControllerInput m_ResolvedInput{};

    // ========================================================================
    // Third-person Runtime Camera state
    // ========================================================================
    // Yaw/PitchはCamera Entity Transformへ書き戻す正規の角度状態です。
    // Pitchを制限することで真上/真下付近でForwardとUpが平行になる特異姿勢を避けます。
    float m_CameraYaw = 0.0f;
    float m_CameraPitch = -0.30f;
    float m_CameraDistance = 8.0f;
    float m_CameraTargetHeight = 1.1f;
    float m_CameraYawSpeed = 2.2f;
    float m_CameraPitchSpeed = 1.8f;
    float m_CameraStickDeadZone = 0.15f;
    float m_CameraMinPitch = -1.20f;
    float m_CameraMaxPitch = 0.65f;
};

} // namespace Raven
