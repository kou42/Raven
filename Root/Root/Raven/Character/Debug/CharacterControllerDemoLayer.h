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
    float WalkAuthoredMotionSpeed = 1.8f;
    float RunAuthoredMotionSpeed = 5.5f;
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

    void SyncVisualTransform();
    bool TryInitializeHumanoidVisual();
    void DestroyHumanoidVisual();
    bool SyncHumanoidVisualTransform(std::string* errorMessage = nullptr);
    bool TryInitializeHumanoidLocomotionAnimation(std::string* errorMessage = nullptr);
    bool UpdateHumanoidLocomotionAnimation(float deltaTime, std::string* errorMessage = nullptr);
    void CaptureGamepadDebugState();
    void ApplyCameraRelativeMovement(CharacterControllerInput& input) const;
    void UpdateGamepadCamera(float deltaTime);

private:
    Scene& m_Scene;

    CharacterController m_CharacterController{};
    TransformComponent m_CharacterRootTransform{};

    Entity m_CharacterEntity{};
    Ref<Mesh> m_CharacterMesh;
    Ref<Material> m_CharacterMaterial;

    // ========================================================================
    // Humanoid character visual / profile
    // ========================================================================
    // Raven_human_test.glb固有のAnimation設定はProfileを唯一の初期値供給元とします。
    // Demo Layer側に同じ数値やClip名を再記述しないことで、Asset差し替え時の変更箇所をProfileへ集約します。
    HumanoidAnimationProfile m_HumanoidAnimationProfile = CreateRavenHumanTestAnimationProfile();

    Gltf::SkinnedMeshSceneInstance m_HumanoidInstance{};
    std::vector<TransformComponent> m_HumanoidLocalTransforms;
    std::string m_HumanoidModelPath = "Raven/Assets/Models/Raven_human_test.glb";
    bool m_HumanoidVisualActive = false;

    // ========================================================================
    // Humanoid Idle / Walk / Run animation
    // ========================================================================
    Gltf::SkinnedBlendTreeRuntime m_HumanoidLocomotionRuntime{};
    std::size_t m_HumanoidAnimationSkinIndex = Gltf::InvalidGltfIndex;
    bool m_HumanoidLocomotionAnimationActive = false;

    // 既存の名前解決経路との互換用Snapshotです。
    // 値そのものはProfileから取得し、このLayerへAsset固有文字列をハードコードしません。
    std::string m_HumanoidIdleAnimationName = m_HumanoidAnimationProfile.Locomotion.IdleAnimationName;
    std::string m_HumanoidWalkAnimationName = m_HumanoidAnimationProfile.Locomotion.WalkAnimationName;
    std::string m_HumanoidRunAnimationName = m_HumanoidAnimationProfile.Locomotion.RunAnimationName;

    std::vector<std::string> m_HumanoidAvailableAnimationNames;
    std::string m_ResolvedHumanoidIdleAnimationName;
    std::string m_ResolvedHumanoidWalkAnimationName;
    std::string m_ResolvedHumanoidRunAnimationName;

    // Runtime tuning値はProfileのAuthored Motion Speedを初期値とし、
    // Debug UIから変更された後はProfile自体を書き換えず現在Runtime値として独立して保持します。
    float m_HumanoidWalkAuthoredMotionSpeed =
        m_HumanoidAnimationProfile.Locomotion.WalkAuthoredMotionSpeed;
    float m_HumanoidRunAuthoredMotionSpeed =
        m_HumanoidAnimationProfile.Locomotion.RunAuthoredMotionSpeed;

    float m_HumanoidActualHorizontalSpeed = 0.0f;
    BlendTree1DDebugInfo m_HumanoidLocomotionDebugInfo{};

    bool m_GamepadConnected = false;
    GamepadState m_RawGamepadState{};
    CharacterControllerInput m_ResolvedInput{};

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
