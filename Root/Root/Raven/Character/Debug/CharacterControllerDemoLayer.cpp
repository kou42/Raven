// Raven/Character/Debug/CharacterControllerDemoLayer.cpp
#include "Raven/Character/Debug/CharacterControllerDemoLayer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

#include <GLFW/glfw3.h>

#include "Raven/Gltf/HumanoidSceneNormalization.h"
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

bool DecomposeComposedTransform(
    const math::Mat4& matrix,
    TransformComponent& outTransform)
{
    constexpr float AffineTolerance = 1.0e-4f;
    constexpr float ScaleTolerance = 1.0e-6f;
    constexpr float OrthogonalTolerance = 2.0e-4f;

    // ========================================================================
    // Character Root * Visual Local Matrix -> Raven TRS
    // ========================================================================
    // Human PrimitiveはImport時のNode Rotationを既に持つため、Root YawをRotation.yへ単純加算すると
    // Euler積順が変わり、AssetによってBody/Clothesの向きが崩れます。
    // そのためMatrixとして正しい順序で合成した後、TransformComponentのRx * Ry * Rz規約へ戻します。
    if (std::fabs(matrix[3][0]) > AffineTolerance
        || std::fabs(matrix[3][1]) > AffineTolerance
        || std::fabs(matrix[3][2]) > AffineTolerance
        || std::fabs(matrix[3][3] - 1.0f) > AffineTolerance)
    {
        return false;
    }

    const math::Vec3 column0{ matrix[0][0], matrix[1][0], matrix[2][0] };
    const math::Vec3 column1{ matrix[0][1], matrix[1][1], matrix[2][1] };
    const math::Vec3 column2{ matrix[0][2], matrix[1][2], matrix[2][2] };

    const float scaleX = column0.Length();
    const float scaleY = column1.Length();
    const float scaleZ = column2.Length();
    if (std::isfinite(scaleX) == false
        || std::isfinite(scaleY) == false
        || std::isfinite(scaleZ) == false
        || scaleX <= ScaleTolerance
        || scaleY <= ScaleTolerance
        || scaleZ <= ScaleTolerance)
    {
        return false;
    }

    const math::Vec3 axisX = column0 / scaleX;
    const math::Vec3 axisY = column1 / scaleY;
    const math::Vec3 axisZ = column2 / scaleZ;

    if (std::fabs(math::Vec3::Dot(axisX, axisY)) > OrthogonalTolerance
        || std::fabs(math::Vec3::Dot(axisX, axisZ)) > OrthogonalTolerance
        || std::fabs(math::Vec3::Dot(axisY, axisZ)) > OrthogonalTolerance)
    {
        return false;
    }

    const float determinant = math::Vec3::Dot(axisX, math::Vec3::Cross(axisY, axisZ));
    if (std::isfinite(determinant) == false
        || std::fabs(determinant - 1.0f) > 1.0e-3f)
    {
        return false;
    }

    const float r00 = axisX.x;
    const float r10 = axisX.y;
    const float r01 = axisY.x;
    const float r11 = axisY.y;
    const float r02 = axisZ.x;
    const float r12 = axisZ.y;
    const float r22 = axisZ.z;

    const float clampedSinY = std::clamp(r02, -1.0f, 1.0f);
    const float rotationY = std::asin(clampedSinY);
    const float cosY = std::cos(rotationY);

    float rotationX = 0.0f;
    float rotationZ = 0.0f;
    if (std::fabs(cosY) > 1.0e-5f)
    {
        rotationX = std::atan2(-r12, r22);
        rotationZ = std::atan2(-r01, r00);
    }
    else
    {
        // Gimbal LockではX/Zを一意に分離できないため、Z=0を代表解にします。
        const float signY = clampedSinY >= 0.0f ? 1.0f : -1.0f;
        rotationX = std::atan2(signY * r10, r11);
        rotationZ = 0.0f;
    }

    if (std::isfinite(rotationX) == false
        || std::isfinite(rotationY) == false
        || std::isfinite(rotationZ) == false)
    {
        return false;
    }

    outTransform.Position = math::Vec3{ matrix[0][3], matrix[1][3], matrix[2][3] };
    outTransform.Rotation = math::Vec3{ rotationX, rotationY, rotationZ };
    outTransform.Scale = math::Vec3{ scaleX, scaleY, scaleZ };
    return true;
}

} // namespace

void CharacterControllerDemoLayer::OnAttach()
{
    // ========================================================================
    // Character visual resource
    // ========================================================================
    // HumanのMaterial Importはまだ未実装なので、Cube fallbackとHumanoidの両方で共通Materialを使います。
    // CharacterControllerの衝突形状は内部Capsule Castであり、表示Meshとは独立しています。
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
    // Fallback visual Entity
    // ========================================================================
    // ColliderComponent / RigidBodyComponentは意図的に追加しません。
    // CharacterのCollisionはCharacterController::UpdateWithMovingPlatforms()内のCapsule Castが担当し、
    // 表示EntityまでPhysicsWorldへ登録すると同じCharacterが二重の衝突形状を持ってしまうためです。
    m_CharacterEntity = m_Scene.CreateEntity("Character Controller Fallback Visual");
    m_CharacterEntity.AddComponent<MeshRendererComponent>(
        MeshRendererComponent{ m_CharacterMesh, m_CharacterMaterial });

    // Humanを利用できる場合はCapsule全高へ正規化して本表示として採用します。
    // 失敗した場合は上で作成したCubeを残し、Character Controller自体の検証を止めません。
    if (TryInitializeHumanoidVisual() == true)
    {
        if (static_cast<bool>(m_CharacterEntity)
            && m_Scene.IsEntityAlive(m_CharacterEntity))
        {
            m_Scene.DestroyEntity(m_CharacterEntity);
        }
        m_CharacterEntity = {};
    }

    SyncVisualTransform();

    // 初回からCharacterを画面中央付近へ捉えるため、Gamepad入力が無くてもCameraを一度同期します。
    UpdateGamepadCamera(0.0f);

    std::cout
        << "[CharacterController] Controls: WASD / Left Stick Camera-relative Move, "
        << "Right Stick Camera, Space / A Jump, Left Shift / RT Run\n";
}

void CharacterControllerDemoLayer::OnDetach()
{
    DestroyHumanoidVisual();

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
    // 表示Entityの有無はGameplay/Physics更新の成立条件にしません。
    // Human/Cubeの表示初期化に失敗してもCharacterControllerの入力・衝突検証は継続できるようにします。
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

bool CharacterControllerDemoLayer::TryInitializeHumanoidVisual()
{
    if (m_CharacterMaterial == nullptr)
    {
        return false;
    }

    std::string errorMessage;
    if (Gltf::SkinnedMeshSceneSpawner::SpawnFromGlb(
            m_Scene,
            m_HumanoidModelPath,
            m_CharacterMaterial,
            m_HumanoidInstance,
            &errorMessage) == false)
    {
        std::cerr
            << "[CharacterController] Humanoid表示の読込に失敗したためCubeへfallbackします: "
            << errorMessage << '\n';
        return false;
    }

    std::vector<Gltf::SpawnedSkinnedPrimitive>& primitives = m_HumanoidInstance.GetPrimitives();
    if (primitives.empty())
    {
        std::cerr << "[CharacterController] Humanoid Primitiveが0件のためCubeへfallbackします。\n";
        DestroyHumanoidVisual();
        return false;
    }

    const CharacterControllerConfig& config = m_CharacterController.GetConfig();
    const float capsuleHeight = 2.0f * (config.CapsuleHalfLength + config.CapsuleRadius);

    // Human Debug Layerの20m固定正規化とは分離し、Character表示ではCollision Capsule全高を使います。
    // これにより「見た目は20m、Colliderは1.8m」というDebug都合のScale差をGameplay側へ持ち込みません。
    if (Gltf::NormalizeHumanoidSceneInstance(
            m_HumanoidInstance,
            primitives,
            capsuleHeight,
            &errorMessage) == false)
    {
        std::cerr
            << "[CharacterController] HumanoidのCapsule身長正規化に失敗したためCubeへfallbackします: "
            << errorMessage << '\n';
        DestroyHumanoidVisual();
        return false;
    }

    // 正規化後はHumanの足元がWorld原点、身長がCapsule全高になっています。
    // この状態をCharacter Root相対のVisual Local Transformとして一度だけ保存します。
    // 毎Frame現在TransformへRoot差分を累積すると誤差が蓄積するため、常にこのSnapshotから再構築します。
    m_HumanoidLocalTransforms.clear();
    m_HumanoidLocalTransforms.reserve(primitives.size());
    for (const Gltf::SpawnedSkinnedPrimitive& primitive : primitives)
    {
        if (static_cast<bool>(primitive.EntityHandle) == false
            || m_Scene.IsEntityAlive(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            std::cerr << "[CharacterController] Humanoid Primitive Transformが無効です。\n";
            DestroyHumanoidVisual();
            return false;
        }

        m_HumanoidLocalTransforms.push_back(
            primitive.EntityHandle.GetComponent<TransformComponent>());
    }

    m_HumanoidVisualActive = true;

    if (SyncHumanoidVisualTransform(&errorMessage) == false)
    {
        std::cerr
            << "[CharacterController] Humanoid初期Root同期に失敗したためCubeへfallbackします: "
            << errorMessage << '\n';
        DestroyHumanoidVisual();
        return false;
    }

    std::cout
        << "[CharacterController] Raven_human_test.glbをCharacter表示へ接続しました。"
        << " Height=" << capsuleHeight << '\n';
    return true;
}

void CharacterControllerDemoLayer::DestroyHumanoidVisual()
{
    if (m_HumanoidInstance.IsValid() == true)
    {
        Gltf::SkinnedMeshSceneSpawner::Destroy(m_Scene, m_HumanoidInstance);
    }

    m_HumanoidInstance = {};
    m_HumanoidLocalTransforms.clear();
    m_HumanoidVisualActive = false;
}

bool CharacterControllerDemoLayer::SyncHumanoidVisualTransform(std::string* errorMessage)
{
    if (m_HumanoidVisualActive == false)
    {
        return true;
    }

    std::vector<Gltf::SpawnedSkinnedPrimitive>& primitives = m_HumanoidInstance.GetPrimitives();
    if (primitives.size() != m_HumanoidLocalTransforms.size())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Humanoid Primitive数とVisual Local Transform数が一致しません";
        }
        return false;
    }

    const math::Mat4 rootTransform = m_CharacterRootTransform.GetTransform();

    for (std::size_t primitiveIndex = 0u; primitiveIndex < primitives.size(); ++primitiveIndex)
    {
        Gltf::SpawnedSkinnedPrimitive& primitive = primitives[primitiveIndex];
        if (static_cast<bool>(primitive.EntityHandle) == false
            || m_Scene.IsEntityAlive(primitive.EntityHandle) == false
            || primitive.EntityHandle.HasComponent<TransformComponent>() == false)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Humanoid Primitive Entity/TransformがRuntime中に無効になりました";
            }
            return false;
        }

        // RootはCharacterControllerが決定する足元位置・向き、LocalはAsset正規化後の見た目配置です。
        // Matrix積として合成することでPrimitive固有Rotation/Scaleを保ったままCharacterへ追従させます。
        const math::Mat4 composedTransform = rootTransform
            * m_HumanoidLocalTransforms[primitiveIndex].GetTransform();

        TransformComponent composedComponent{};
        if (DecomposeComposedTransform(composedTransform, composedComponent) == false)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Character RootとHumanoid Visual Local TransformのTRS合成に失敗しました";
            }
            return false;
        }

        primitive.EntityHandle.GetComponent<TransformComponent>() = composedComponent;
    }

    return true;
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
    if (m_HumanoidVisualActive == true)
    {
        std::string errorMessage;
        if (SyncHumanoidVisualTransform(&errorMessage) == false)
        {
            std::cerr
                << "[CharacterController] Humanoid Visual同期に失敗しました: "
                << errorMessage << '\n';
        }
        return;
    }

    if (static_cast<bool>(m_CharacterEntity) == false
        || m_Scene.IsEntityAlive(m_CharacterEntity) == false
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
