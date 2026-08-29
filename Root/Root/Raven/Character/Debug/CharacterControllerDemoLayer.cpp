// Raven/Character/Debug/CharacterControllerDemoLayer.cpp
#include "Raven/Character/Debug/CharacterControllerDemoLayer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

#include <GLFW/glfw3.h>

#include "Raven/Renderer/Material/Material.h"
#include "Raven/Renderer/Mesh/Mesh.h"
#include "Raven/Renderer/Mesh/PrimitiveMeshFactory.h"
#include "Raven/Renderer/Pipeline/Pipeline.h"
#include "Raven/Renderer/Shader/Shader.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneCameraSystem.h"

namespace Raven
{
namespace
{

math::Vec2 ApplyCameraStickDeadZone(const math::Vec2& stick, float deadZone)
{
    // CameraもCharacter移動と同じ円形Dead Zoneを利用します。
    // X/Yを個別に切る矩形Dead Zoneでは斜め入力時の開始角度が歪むため、Stick全体の長さで判定します。
    const float clampedDeadZone = std::clamp(deadZone, 0.0f, 0.999f);
    const float lengthSquared = stick.x * stick.x + stick.y * stick.y;
    const float deadZoneSquared = clampedDeadZone * clampedDeadZone;

    if (lengthSquared <= deadZoneSquared)
    {
        return math::Vec2{ 0.0f, 0.0f };
    }

    const float length = std::sqrt(lengthSquared);
    if (length <= 1.0e-6f)
    {
        return math::Vec2{ 0.0f, 0.0f };
    }

    const float clampedLength = std::min(length, 1.0f);
    const float remappedLength = (clampedLength - clampedDeadZone) / (1.0f - clampedDeadZone);
    const float scale = remappedLength / length;
    return math::Vec2{ stick.x * scale, stick.y * scale };
}

} // namespace

void CharacterControllerDemoLayer::OnAttach()
{
    // ========================================================================
    // Character visual resource
    // ========================================================================
    // CharacterControllerの衝突形状は内部Capsule Castであり、表示Meshとは独立しています。
    // 現段階ではPrimitiveMeshFactoryにCapsule Meshが無いため、Capsuleの外接寸法に合わせた
    // Cubeを表示し、「足元位置・向き・移動」を分かりやすく確認できるようにします。
    m_CharacterMesh = PrimitiveMeshFactory::CreateCube();
    if (m_CharacterMesh == nullptr)
    {
        return;
    }

    ShaderLibrary shaderLibrary{};
    Ref<Shader> shader = shaderLibrary.Load(
        "CharacterControllerDemo",
        "Raven/Assets/Shaders/Vertex/test.vert",
        "Raven/Assets/Shaders/Fragment/test.frag");
    if (shader == nullptr)
    {
        m_CharacterMesh.reset();
        return;
    }

    PipelineSpecification pipelineSpecification{};
    pipelineSpecification.DebugName = "Character Controller Demo Pipeline";
    pipelineSpecification.Shader = shader;
    pipelineSpecification.Topology = PrimitiveTopology::Triangles;
    pipelineSpecification.Cull = CullMode::None;
    pipelineSpecification.FrontFaceMode = FrontFace::CounterClockwise;
    pipelineSpecification.DepthTest = true;
    pipelineSpecification.DepthWrite = true;
    pipelineSpecification.DepthCompare = DepthCompareOperator::Less;
    pipelineSpecification.Blend = true;

    m_CharacterMaterial = CreateRef<Material>(Pipeline::Create(pipelineSpecification));
    m_CharacterMaterial->SetUniform("u_Tint", math::Vec3{ 0.20f, 0.65f, 1.00f });
    m_CharacterMaterial->SetUniform("u_Alpha", 1.0f);

    // ========================================================================
    // Character Controller root
    // ========================================================================
    // CharacterControllerではTransform::PositionをCapsuleの中心ではなく「足元Root」として扱います。
    // SceneGameの床はY=0なので、初期RootもY=0へ置けば最初のGround Queryで安定して接地できます。
    // 他の検証Entityと重なりにくいよう、初期位置だけ+Zへ離しています。
    m_CharacterRootTransform = TransformComponent{};
    m_CharacterRootTransform.Position = { 0.0f, 0.0f, 18.0f };
    m_CharacterRootTransform.Rotation = { 0.0f, 0.0f, 0.0f };
    m_CharacterRootTransform.Scale = { 1.0f, 1.0f, 1.0f };

    m_CharacterController = CharacterController{};
    m_GamepadConnected = false;
    m_RawGamepadState = GamepadState{};
    m_ResolvedInput = CharacterControllerInput{};

    // ========================================================================
    // Visual Entity
    // ========================================================================
    // ColliderComponent / RigidBodyComponentは意図的に追加しません。
    // CharacterのCollisionはCharacterController::UpdateWithMovingPlatforms()内のCapsule Castが担当し、
    // 表示EntityまでPhysicsWorldへ登録すると同じCharacterが二重の衝突形状を持ってしまうためです。
    m_CharacterEntity = m_Scene.CreateEntity("Gamepad Character Controller Demo");
    m_CharacterEntity.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_CharacterMesh, m_CharacterMaterial });

    SyncVisualTransform();

    // 初回からCharacterを画面中央付近へ捉えるため、Gamepad入力が無くてもCameraを一度同期します。
    UpdateGamepadCamera(0.0f);

    std::cout
        << "[CharacterController] Controls: WASD / Left Stick Camera-relative Move, "
        << "Right Stick Camera, Space / A Jump, Left Shift / RT Run\n";
}

void CharacterControllerDemoLayer::OnDetach()
{
    if (static_cast<bool>(m_CharacterEntity)
        && m_Scene.IsEntityAlive(m_CharacterEntity))
    {
        m_Scene.DestroyEntity(m_CharacterEntity);
    }

    m_CharacterEntity = {};
    m_CharacterMesh.reset();
    m_CharacterMaterial.reset();
    m_CharacterController = CharacterController{};
    m_CharacterRootTransform = TransformComponent{};
    m_GamepadConnected = false;
    m_RawGamepadState = GamepadState{};
    m_ResolvedInput = CharacterControllerInput{};
}

void CharacterControllerDemoLayer::OnUpdate(float deltaTime)
{
    if (static_cast<bool>(m_CharacterEntity) == false
        || m_Scene.IsEntityAlive(m_CharacterEntity) == false)
    {
        return;
    }

    // Scene側と同じく極端に大きなFrame DeltaをClampします。
    // Debugger停止後などの単発巨大dtでCharacterが長距離Capsule Castすることを防ぎます。
    const float safeDeltaTime = std::clamp(deltaTime, 0.0f, 0.05f);

    // ========================================================================
    // Gamepad diagnostic snapshot
    // ========================================================================
    // Raw値を先に取得し、その後にCharacterControllerInputへ変換します。
    // これにより例えば「Stick Raw値は動いているのにMoveが0」の場合はDead Zone、
    // 「RT Raw値は上がるのにRun=false」の場合はThresholdを疑う、という切り分けができます。
    CaptureGamepadDebugState();

    // ========================================================================
    // Keyboard + Gamepad -> Device-independent Character input
    // ========================================================================
    // ReadDefaultPlayerInput()はKeyboardとGamepadを同じCharacterControllerInputへ統合します。
    // Gamepad未接続時はKeyboardだけ、接続時は両方を利用でき、同時入力時のMove長も1以内へClampされます。
    // CharacterController本体は入力Deviceを知らず、最終的なGameplay入力だけを受け取ります。
    m_ResolvedInput = CharacterController::ReadDefaultPlayerInput();

    // Device入力としてのMoveは「右=+X / 前=+Y」の2D値です。
    // CharacterControllerへ渡す直前にRuntime CameraのYawを基準としたWorld XZへ変換します。
    // KeyboardのWASDとGamepad Left Stickの両方へ同じCamera-relative規則を適用することで、
    // Deviceを切り替えても移動方向の意味が変わらないようにします。
    ApplyCameraRelativeMovement(m_ResolvedInput);

    std::string errorMessage;
    if (m_CharacterController.UpdateWithMovingPlatforms(
            m_ResolvedInput,
            safeDeltaTime,
            m_Scene,
            m_CharacterRootTransform,
            1.0f,
            &errorMessage) == false)
    {
        std::cerr
            << "[CharacterController] Player Character更新に失敗しました: "
            << errorMessage << '\n';
        return;
    }

    SyncVisualTransform();

    // Character移動後のRootをTargetにすることで、Cameraは同じFrame内で最新位置へ追従します。
    // Scene-owned LayerはRender前に更新されるため、このCamera Transformも同じFrameの描画へ反映されます。
    UpdateGamepadCamera(safeDeltaTime);
}

void CharacterControllerDemoLayer::CaptureGamepadDebugState()
{
    // ReadDefaultGamepadInput()と同じDefault Deviceを観測します。
    // GLFW定数を使うのはDebug Layerだけで、CharacterController本体へPlatform依存を増やしません。
    constexpr int DefaultGamepadIndex = GLFW_JOYSTICK_1;

    m_RawGamepadState = GamepadState{};
    m_GamepadConnected = Input::GetGamepadState(DefaultGamepadIndex, m_RawGamepadState);

    if (m_GamepadConnected == false)
    {
        // Input::GetGamepadState()も失敗時にNeutralへ戻しますが、Layer側でも明示的に初期化して
        // Debuggerで「切断後の前Frame値」が見える余地を残さないようにします。
        m_RawGamepadState = GamepadState{};
    }
}

void CharacterControllerDemoLayer::ApplyCameraRelativeMovement(CharacterControllerInput& input) const
{
    const float inputLengthSquared = input.Move.x * input.Move.x + input.Move.y * input.Move.y;
    if (inputLengthSquared <= 1.0e-8f)
    {
        input.Move = math::Vec2{ 0.0f, 0.0f };
        return;
    }

    // ========================================================================
    // Camera basis projected onto ground plane
    // ========================================================================
    // Orbit CameraのYawだけからXZ Forwardを作ることで、Cameraを上下へPitchしても
    // Stick前入力にY成分が混ざらず、Characterは常に地面平面上を移動します。
    //
    // Yaw=0のCameraはLocal Forward=-Zを向くため、画面奥方向はWorld -Zです。
    const math::Vec3 cameraForward{
        std::sin(m_CameraYaw),
        0.0f,
        -std::cos(m_CameraYaw)
    };

    // Camera RightはForwardとWorld Upの外積と等価なXZ直交Basisです。
    // Yaw=0では+Xとなり、Stick右入力が画面右方向へ移動します。
    const math::Vec3 cameraRight{
        std::cos(m_CameraYaw),
        0.0f,
        std::sin(m_CameraYaw)
    };

    const math::Vec3 worldMove = cameraRight * input.Move.x + cameraForward * input.Move.y;

    // CharacterControllerInput::Moveは歴史的にVec2(x, forward)を受け取り、内部で
    // desiredDirection = Vec3{ Move.x, 0, Move.y } としてWorld XZへ展開します。
    // そのためここではWorld X/Zを再びMove.x/Move.yへ格納します。
    input.Move = math::Vec2{ worldMove.x, worldMove.z };
}

void CharacterControllerDemoLayer::UpdateGamepadCamera(float deltaTime)
{
    Entity cameraEntity = SceneCameraSystem::ResolveRuntimeCameraEntity(m_Scene);
    if (static_cast<bool>(cameraEntity) == false
        || m_Scene.IsEntityAlive(cameraEntity) == false
        || cameraEntity.HasComponent<TransformComponent>() == false
        || cameraEntity.HasComponent<CameraComponent>() == false)
    {
        return;
    }

    // WindowsInput側でRightStickYは「上へ倒すと+」になるよう正規化済みです。
    // 右Stick上でCameraを上側へ回したいのでPitchへ正方向として加算します。
    const math::Vec2 cameraStick = ApplyCameraStickDeadZone(
        math::Vec2{ m_RawGamepadState.RightStickX, m_RawGamepadState.RightStickY },
        m_CameraStickDeadZone);

    m_CameraYaw += cameraStick.x * m_CameraYawSpeed * deltaTime;
    m_CameraPitch += cameraStick.y * m_CameraPitchSpeed * deltaTime;
    m_CameraPitch = std::clamp(m_CameraPitch, m_CameraMinPitch, m_CameraMaxPitch);

    // Raven Runtime CameraのLocal Forwardは-Zです。
    // SceneCameraSystemのX -> Y回転と同じ意味になるよう、Yaw/PitchからWorld Forwardを構築します。
    // Pitchが負なら下向き、Yaw=0なら-Z向きです。
    const float cosPitch = std::cos(m_CameraPitch);
    math::Vec3 cameraForward{
        std::sin(m_CameraYaw) * cosPitch,
        std::sin(m_CameraPitch),
        -std::cos(m_CameraYaw) * cosPitch
    };
    cameraForward.Normalize();

    const math::Vec3 target = m_CharacterRootTransform.Position
        + math::Vec3{ 0.0f, m_CameraTargetHeight, 0.0f };

    TransformComponent& cameraTransform = cameraEntity.GetComponent<TransformComponent>();
    cameraTransform.Position = target - cameraForward * m_CameraDistance;
    cameraTransform.Rotation = math::Vec3{ m_CameraPitch, m_CameraYaw, 0.0f };

    // View Matrixはここでは更新しません。
    // SceneCameraSystem::UpdatePrimaryCamera()がRender直前にTransformからViewを再構築するため、
    // Camera姿勢の正規データをTransformComponentへ一本化したまま維持できます。
}

void CharacterControllerDemoLayer::SyncVisualTransform()
{
    if (static_cast<bool>(m_CharacterEntity) == false
        || m_CharacterEntity.HasComponent<TransformComponent>() == false)
    {
        return;
    }

    const CharacterControllerConfig& config = m_CharacterController.GetConfig();

    // Capsule全高 = Cylinder部分(2 * HalfLength) + 上下Hemisphere(2 * Radius)。
    // Primitive Cubeは原点中心なので、その半分だけ足元Rootから上へ配置します。
    const float capsuleHeight = 2.0f * (config.CapsuleHalfLength + config.CapsuleRadius);
    const float capsuleDiameter = 2.0f * config.CapsuleRadius;

    TransformComponent& visualTransform = m_CharacterEntity.GetComponent<TransformComponent>();
    visualTransform.Position = m_CharacterRootTransform.Position
        + math::Vec3{ 0.0f, capsuleHeight * 0.5f, 0.0f };
    visualTransform.Rotation = m_CharacterRootTransform.Rotation;
    visualTransform.Scale = math::Vec3{ capsuleDiameter, capsuleHeight, capsuleDiameter };
}

} // namespace Raven
