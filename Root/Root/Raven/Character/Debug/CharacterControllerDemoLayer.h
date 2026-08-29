// Raven/Character/Debug/CharacterControllerDemoLayer.h
#pragma once

#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "Raven/Animation/HumanoidAnimationProfile.h"
#include "Raven/Character/CharacterController.h"
#include "Raven/Core/Base.h"
#include "Raven/Core/Input.h"
#include "Raven/Gltf/SkinnedBlendTreeRuntime.h"
#include "Raven/Gltf/SkinnedMeshSceneSpawner.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Entity.h"

namespace Raven
{

class Material;
class Mesh;
class Scene;

// ============================================================================
// CharacterLocomotionDebugSnapshot
// ============================================================================
// CharacterControllerDemoLayerが保持しているLocomotion診断値を、RendererやEditorへ依存しない
// 1つのSnapshotへまとめた構造体です。
//
// Debug UI側がCharacterController / BlendTree Runtimeの内部へ直接触ると、表示のためだけに
// Gameplay/Animation層への依存が増えます。そのためDemoLayerを診断情報の境界とし、
// Overlay側はこのSnapshotを読むだけで表示できる構成にします。
struct CharacterLocomotionDebugSnapshot
{
    bool AnimationActive = false;
    float ActualHorizontalSpeed = 0.0f;
    float ParameterValue = 0.0f;

    std::string LeftAnimationName;
    std::string RightAnimationName;
    float LeftThreshold = 0.0f;
    float RightThreshold = 0.0f;
    float LeftWeight = 0.0f;
    float RightWeight = 0.0f;
    bool IsClamped = false;

    // Foot Sliding補正のRuntime診断値です。
    // ReferenceMotionSpeedは現在Blend中のClipが1.0倍再生時に想定する速度、
    // PlaybackSpeedは実速度との差を吸収するためAnimatorへ設定された再生倍率です。
    float ReferenceMotionSpeed = 0.0f;
    float PlaybackSpeed = 1.0f;

    // Runtime調整中のAuthored Motion Speedです。
    // Overlay側はこの値を編集し、SetHumanoidLocomotionAuthoredMotionSpeeds()経由でRuntimeへ反映します。
    // Asset固有の初期値はProfileからSnapshot生成時に設定されるため、構造体の既定値は中立値にします。
    float WalkAuthoredMotionSpeed = 0.0f;
    float RunAuthoredMotionSpeed = 0.0f;
};

// ============================================================================
// CharacterControllerDemoLayer
// ============================================================================
// Keyboard / Gamepad -> CharacterControllerInput -> CharacterController -> Scene Transform
// の一連のRuntime経路を目視確認するためのデモLayerです。
//
// このLayerはApplication LayerではなくScene-owned Layerとして登録します。
// Scene::OnUpdate()の Physics -> Scene Layer -> Render の順序へ入るため、Characterの
// Transform更新が同じFrameのScene描画へ反映され、Application Layerで発生する1Frameの表示遅延を避けます。
//
// CharacterController本体はKinematic Capsuleを内部のShape Castとして扱い、ECS上に
// Character用RigidBody/Colliderを生成しません。そのため表示用EntityにもColliderを付けず、
// Character自身のCapsule Castが自分の表示Entityへ衝突する自己衝突を避けます。
//
// 現段階の標準操作:
//   WASD / Left Stick : Runtime Camera基準でXZ平面を移動
//   Right Stick       : Characterを中心にRuntime CameraをYaw / Pitch回転
//   Space / A         : Jump
//   Left Shift / RT   : Run
//
// Raven_human_test.glbを読み込める場合はHumanoidをCharacter表示として使用し、
// CharacterController Capsule全高へ正規化します。Asset読込または正規化に失敗した場合だけ
// 従来のCube表示へfallbackするため、入力・Physics検証自体は継続できます。
//
// Humanoid表示が有効な場合は同じSkinnedMeshRuntimeAssetへSkinnedBlendTreeRuntimeを接続し、
// CharacterControllerが衝突・加減速まで解決した「実水平速度」を毎Frame Speed Parameterへ渡します。
// これにより入力のRunフラグではなく、壁Slideや加速途中を含む実際の移動結果で
// Idle / Walk / Run Poseが連続補間されます。
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

    // CharacterControllerが衝突解決まで終えた後の実水平速度です。
    // InputのWalk/Run要求値ではないため、壁衝突や加減速を含めてAnimationが実際に受け取った値を確認できます。
    float GetHumanoidActualHorizontalSpeed() const
    {
        return m_HumanoidActualHorizontalSpeed;
    }

    // SkinnedBlendTreeRuntime自身が解決した現在の左右ChildとWeightを返します。
    // Debug UI側で補間計算を再実装せず、Runtimeと同じ結果をそのまま表示するためのSnapshotです。
    const BlendTree1DDebugInfo& GetHumanoidLocomotionDebugInfo() const
    {
        return m_HumanoidLocomotionDebugInfo;
    }

    // ========================================================================
    // Locomotion runtime tuning
    // ========================================================================
    // Walk / Run Clipを1.0倍再生したときの想定移動速度を実行中に変更します。
    // BlendTree自体を再構築しないためAnimation位相は維持され、足滑りだけを連続的に調整できます。
    bool SetHumanoidLocomotionAuthoredMotionSpeeds(
        float walkAuthoredMotionSpeed,
        float runAuthoredMotionSpeed,
        std::string* errorMessage = nullptr)
    {
        if (m_HumanoidLocomotionAnimationActive == false
            || m_HumanoidAnimationSkinIndex == Gltf::InvalidGltfIndex)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "Humanoid Locomotion Animationが有効ではありません";
            }
            return false;
        }

        if (m_HumanoidLocomotionRuntime.SetLocomotionAuthoredMotionSpeeds(
                m_HumanoidAnimationSkinIndex,
                walkAuthoredMotionSpeed,
                runAuthoredMotionSpeed,
                errorMessage) == false)
        {
            return false;
        }

        m_HumanoidWalkAuthoredMotionSpeed = walkAuthoredMotionSpeed;
        m_HumanoidRunAuthoredMotionSpeed = runAuthoredMotionSpeed;
        return true;
    }

    // ========================================================================
    // Locomotion Debug UI boundary
    // ========================================================================
    // BlendTree1DDebugInfoのChildIndexを、Character側で解決済みのAnimation名へ変換します。
    // LocomotionTreeはConfigure時にThreshold昇順の Idle / Walk / Run となるため、Index 0/1/2を
    // それぞれ解決済み名へ対応させられます。未知Indexは空文字列にして誤表示を避けます。
    CharacterLocomotionDebugSnapshot GetHumanoidLocomotionDebugSnapshot() const
    {
        CharacterLocomotionDebugSnapshot snapshot{};
        snapshot.AnimationActive = m_HumanoidLocomotionAnimationActive;
        snapshot.ActualHorizontalSpeed = m_HumanoidActualHorizontalSpeed;
        snapshot.ParameterValue = m_HumanoidLocomotionDebugInfo.ParameterValue;
        snapshot.LeftAnimationName = ResolveLocomotionDebugChildName(
            m_HumanoidLocomotionDebugInfo.LeftChildIndex);
        snapshot.RightAnimationName = ResolveLocomotionDebugChildName(
            m_HumanoidLocomotionDebugInfo.RightChildIndex);
        snapshot.LeftThreshold = m_HumanoidLocomotionDebugInfo.LeftThreshold;
        snapshot.RightThreshold = m_HumanoidLocomotionDebugInfo.RightThreshold;
        snapshot.LeftWeight = m_HumanoidLocomotionDebugInfo.LeftWeight;
        snapshot.RightWeight = m_HumanoidLocomotionDebugInfo.RightWeight;
        snapshot.IsClamped = m_HumanoidLocomotionDebugInfo.IsClamped;
        snapshot.WalkAuthoredMotionSpeed = m_HumanoidWalkAuthoredMotionSpeed;
        snapshot.RunAuthoredMotionSpeed = m_HumanoidRunAuthoredMotionSpeed;

        // Playback補正値もBlendTreeと同じRuntime Stateから取得します。
        // Overlay側でActual/Reference比を再計算するとClamp規則やIdle例外と表示がずれるため、
        // Animatorへ実際に適用された値をRuntimeからそのままSnapshotへ転送します。
        if (m_HumanoidLocomotionAnimationActive == true
            && m_HumanoidAnimationSkinIndex != Gltf::InvalidGltfIndex)
        {
            Gltf::LocomotionPlaybackDebugInfo playbackInfo{};
            if (m_HumanoidLocomotionRuntime.GetLocomotionPlaybackDebugInfo(
                    m_HumanoidAnimationSkinIndex,
                    playbackInfo,
                    nullptr) == true)
            {
                snapshot.ReferenceMotionSpeed = playbackInfo.ReferenceMotionSpeed;
                snapshot.PlaybackSpeed = playbackInfo.PlaybackSpeed;
            }
        }

        return snapshot;
    }

    // 現在のRendererには汎用Text/ImGui Overlayがまだ無いため、まず表示層へそのまま渡せる
    // 複数行TextもDemoLayer側で生成します。将来Overlay Rendererを追加した際は、この戻り値を
    // 画面左上へ描画するだけでRuntimeと同じBlend診断値を表示できます。
    std::string GetHumanoidLocomotionDebugText() const
    {
        const CharacterLocomotionDebugSnapshot snapshot = GetHumanoidLocomotionDebugSnapshot();

        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2);
        stream << "Locomotion Animation : "
            << (snapshot.AnimationActive == true ? "Active" : "Inactive") << '\n';
        stream << "Actual Speed         : " << snapshot.ActualHorizontalSpeed << '\n';
        stream << "Blend Parameter      : " << snapshot.ParameterValue << '\n';
        stream << "Reference Speed      : " << snapshot.ReferenceMotionSpeed << '\n';
        stream << "Playback Speed       : " << snapshot.PlaybackSpeed << "x\n";
        stream << "Walk Authored Speed  : " << snapshot.WalkAuthoredMotionSpeed << '\n';
        stream << "Run Authored Speed   : " << snapshot.RunAuthoredMotionSpeed << '\n';
        stream << "Blend                : "
            << FormatDebugAnimationName(snapshot.LeftAnimationName)
            << " -> "
            << FormatDebugAnimationName(snapshot.RightAnimationName) << '\n';
        stream << "Left Weight          : " << snapshot.LeftWeight << '\n';
        stream << "Right Weight         : " << snapshot.RightWeight << '\n';
        stream << "Threshold            : "
            << snapshot.LeftThreshold << " -> " << snapshot.RightThreshold << '\n';
        stream << "Clamped              : "
            << (snapshot.IsClamped == true ? "true" : "false");
        return stream.str();
    }

private:
    // DebugInfoのChildIndexを解決済みLocomotion名へ変換します。
    // ここを1か所に集約することで、Debug UIがBlendTreeのChild配置規約を知る必要を無くします。
    std::string ResolveLocomotionDebugChildName(std::size_t childIndex) const
    {
        if (childIndex == 0u)
        {
            return m_ResolvedHumanoidIdleAnimationName;
        }
        if (childIndex == 1u)
        {
            return m_ResolvedHumanoidWalkAnimationName;
        }
        if (childIndex == 2u)
        {
            return m_ResolvedHumanoidRunAnimationName;
        }
        return {};
    }

    static std::string FormatDebugAnimationName(const std::string& animationName)
    {
        if (animationName.empty() == true)
        {
            return "<none>";
        }
        return animationName;
    }

    // CharacterControllerが更新するTransformは「足元Root」です。
    // Humanを利用できないfallback時は、原点中心CubeをRootからCapsule全高の半分だけ上へずらします。
    void SyncVisualTransform();

    // HumanをSceneへSpawnし、CharacterController Capsule全高へ正規化します。
    // 正規化直後の各Primitive TransformはCharacter Root相対のVisual Local Transformとして保存し、
    // 以後のFrameではRoot移動・回転だけを合成します。
    bool TryInitializeHumanoidVisual();
    void DestroyHumanoidVisual();
    bool SyncHumanoidVisualTransform(std::string* errorMessage = nullptr);

    // Spawn済みHumanoidと同じRuntime AssetへIdle / Walk / Run BlendTreeを接続します。
    // Animation初期化だけ失敗した場合はHumanoid表示自体を破棄せずBind Pose表示を継続します。
    // これによりAnimation名の不一致とCharacter表示/Physicsの不具合を独立して切り分けられます。
    bool TryInitializeHumanoidLocomotionAnimation(std::string* errorMessage = nullptr);
    bool UpdateHumanoidLocomotionAnimation(float deltaTime, std::string* errorMessage = nullptr);

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

    // Human読込失敗時のfallback表示です。
    Entity m_CharacterEntity{};
    Ref<Mesh> m_CharacterMesh;
    Ref<Material> m_CharacterMaterial;

    // ========================================================================
    // Humanoid character visual
    // ========================================================================
    // SkinnedMeshSceneInstanceがPrimitive EntityとSkinning RuntimeのLifetimeを保持します。
    // m_HumanoidLocalTransformsは「足元原点・Capsule身長へ正規化済み」のTransform Snapshotで、
    // Character RootのWorld Transformを毎Frame左から合成して表示Humanを追従させます。
    Gltf::SkinnedMeshSceneInstance m_HumanoidInstance{};
    std::vector<TransformComponent> m_HumanoidLocalTransforms;
    std::string m_HumanoidModelPath = "Raven/Assets/Models/Raven_human_test.glb";
    bool m_HumanoidVisualActive = false;

    // ========================================================================
    // Humanoid Idle / Walk / Run animation
    // ========================================================================
    // BlendTree RuntimeはSceneInstanceが所有するSkinnedMeshRuntimeAssetを参照します。
    // そのためDestroyHumanoidVisual()ではRuntimeを先に初期状態へ戻してからSceneInstanceを破棄します。
    Gltf::SkinnedBlendTreeRuntime m_HumanoidLocomotionRuntime{};
    std::size_t m_HumanoidAnimationSkinIndex = Gltf::InvalidGltfIndex;
    bool m_HumanoidLocomotionAnimationActive = false;

    // Raven_human_test.glb固有のLocomotion初期設定です。
    // Asset固有のClip名・Threshold・Authored Motion SpeedはDemo Layerへ直接記述せず、
    // HumanoidAnimationProfileを唯一の設定元として保持します。
    HumanoidAnimationProfile m_HumanoidAnimationProfile = CreateRavenHumanTestAnimationProfile();

    // Profileの要求名をGetAnimationNames()でAsset側の実名へ解決した結果を保持します。
    // 要求名の複製メンバーは持たず、再初期化時も常にProfileを正規の参照元として使用します。
    // Runtimeから取得したAnimation一覧と、実際にLocomotionへ採用した名前を保持します。
    // DebuggerからAsset命名と自動解決結果を比較できるよう、初期化後もSnapshotを残します。
    std::vector<std::string> m_HumanoidAvailableAnimationNames;
    std::string m_ResolvedHumanoidIdleAnimationName;
    std::string m_ResolvedHumanoidWalkAnimationName;
    std::string m_ResolvedHumanoidRunAnimationName;

    // Foot Sliding補正のRuntime調整値です。
    // Profile値は初期値としてのみ使用し、Debug UIから調整後は現在Runtime値として独立して保持します。
    // これによりProfileというAsset初期設定と、実行中の調整値を混同しません。
    float m_HumanoidWalkAuthoredMotionSpeed =
        m_HumanoidAnimationProfile.Locomotion.WalkAuthoredMotionSpeed;
    float m_HumanoidRunAuthoredMotionSpeed =
        m_HumanoidAnimationProfile.Locomotion.RunAuthoredMotionSpeed;

    // 毎Frame更新するLocomotion診断値です。
    // SpeedとBlend Weightを同じFrameのSnapshotとして保持し、後続Debug Overlayから参照できるようにします。
    float m_HumanoidActualHorizontalSpeed = 0.0f;
    BlendTree1DDebugInfo m_HumanoidLocomotionDebugInfo{};

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
