// Raven/Character/Debug/CharacterLocomotionDebugOverlayLayer.h
#pragma once

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

#include <imgui.h>

#include "Raven/Character/Debug/CharacterControllerDemoLayer.h"
#include "Raven/Renderer/Layer/Layer.h"

namespace Raven
{

// ============================================================================
// CharacterLocomotionDebugOverlayLayer
// ============================================================================
// CharacterControllerDemoLayerが公開するLocomotion Debug SnapshotをDear ImGuiへ表示する
// Application-owned Overlay Layerです。
//
// CharacterControllerDemoLayer本体はPhysics更新順を守るためScene-owned Layerのまま維持し、
// このOverlayだけをApplication Layerへ分離します。ApplicationはImGui::NewFrame()～Render()の間に
// Application LayerのOnImGuiRender()を呼ぶため、Runtime Characterの診断値を画面へ安全に表示できます。
//
// Lifetimeについて:
// Application終了時はApplication LayerがSceneより先にDetach/破棄されるため、借用している
// CharacterControllerDemoLayerへのpointerはOverlayのLifetime中は有効です。
class CharacterLocomotionDebugOverlayLayer final : public Layer
{
public:
    explicit CharacterLocomotionDebugOverlayLayer(
        CharacterControllerDemoLayer& characterLayer)
        : m_CharacterLayer(&characterLayer)
    {
    }

    void OnDetach() override
    {
        // 借用pointerであり所有権は持ちません。
        // Application LayerはSceneより先に破棄されますが、Detach後に誤利用しないよう明示的に切ります。
        m_CharacterLayer = nullptr;
    }

    void OnImGuiRender(float deltaTime) override
    {
        static_cast<void>(deltaTime);

        if (m_CharacterLayer == nullptr)
        {
            return;
        }

        const CharacterLocomotionDebugSnapshot snapshot =
            m_CharacterLayer->GetHumanoidLocomotionDebugSnapshot();

        // ====================================================================
        // Runtime Locomotion overlay
        // ====================================================================
        // Foot Sliding調整をその場で行えるよう、診断専用HUDから最小限のInteractive Tuning HUDへ拡張します。
        // Window位置は従来どおり左上へ固定し、移動/Resize/Dockingは許可しません。
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        constexpr float Margin = 10.0f;
        const ImVec2 windowPosition{
            viewport->WorkPos.x + Margin,
            viewport->WorkPos.y + Margin
        };

        ImGui::SetNextWindowPos(windowPosition, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);

        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoMove;

        if (ImGui::Begin("Character Locomotion Debug", nullptr, windowFlags) == true)
        {
            ImGui::TextUnformatted("Character Locomotion");
            ImGui::Separator();

            ImGui::Text(
                "Animation : %s",
                snapshot.AnimationActive == true ? "Active" : "Inactive");
            ImGui::Text("Actual Speed : %.2f", snapshot.ActualHorizontalSpeed);
            ImGui::Text("Parameter    : %.2f", snapshot.ParameterValue);

            // =================================================================
            // Foot Sliding correction diagnostic
            // =================================================================
            // Reference Speedは現在のBlend Weightで補間されたClip側の想定速度です。
            // Playback SpeedはActual / Referenceへ安全Clampを適用した、Animatorに実際に設定された倍率です。
            // UI側では再計算せずRuntime値をそのまま表示し、補正ロジックとの食い違いを防ぎます。
            ImGui::Text("Reference    : %.2f", snapshot.ReferenceMotionSpeed);
            ImGui::Text("Playback     : %.2fx", snapshot.PlaybackSpeed);

            // 現在のLocomotionBlendTreeConfig既定Clamp範囲と同じ値です。
            // Playbackが端へ張り付いている場合、Authored Motion Speedが実際のClip速度から大きく外れている、
            // またはGameplay速度との差がClamp範囲だけでは吸収できない可能性があるため、調整時に強調表示します。
            constexpr float MinPlaybackSpeed = 0.50f;
            constexpr float MaxPlaybackSpeed = 2.00f;
            constexpr float ClampDisplayEpsilon = 1.0e-3f;

            const bool playbackAtLowerClamp =
                snapshot.AnimationActive == true
                && snapshot.ReferenceMotionSpeed > 0.0f
                && snapshot.PlaybackSpeed <= MinPlaybackSpeed + ClampDisplayEpsilon;
            const bool playbackAtUpperClamp =
                snapshot.AnimationActive == true
                && snapshot.ReferenceMotionSpeed > 0.0f
                && snapshot.PlaybackSpeed >= MaxPlaybackSpeed - ClampDisplayEpsilon;

            if (playbackAtLowerClamp == true)
            {
                ImGui::TextUnformatted("Playback Clamp : LOWER (0.50x)");
            }
            else if (playbackAtUpperClamp == true)
            {
                ImGui::TextUnformatted("Playback Clamp : UPPER (2.00x)");
            }
            else
            {
                ImGui::TextUnformatted("Playback Clamp : Free");
            }

            const std::string leftName = snapshot.LeftAnimationName.empty() == true
                ? std::string("<none>")
                : snapshot.LeftAnimationName;
            const std::string rightName = snapshot.RightAnimationName.empty() == true
                ? std::string("<none>")
                : snapshot.RightAnimationName;

            ImGui::Text(
                "Blend        : %s -> %s",
                leftName.c_str(),
                rightName.c_str());
            ImGui::Text(
                "Weight       : %.2f / %.2f",
                snapshot.LeftWeight,
                snapshot.RightWeight);
            ImGui::Text(
                "Threshold    : %.2f -> %.2f",
                snapshot.LeftThreshold,
                snapshot.RightThreshold);
            ImGui::Text(
                "Clamped      : %s",
                snapshot.IsClamped == true ? "true" : "false");

            // Weightの数値だけでなく割合を直感的に確認できるよう、右側MotionのWeightをBar表示します。
            // LeftWeight + RightWeight = 1.0というBlendTree1Dの規約を利用し、RightWeightを進行率とします。
            ImGui::ProgressBar(snapshot.RightWeight, ImVec2(220.0f, 0.0f));

            ImGui::Separator();
            ImGui::TextUnformatted("Foot Sliding Tuning");

            // =================================================================
            // Runtime Authored Motion Speed tuning
            // =================================================================
            // Snapshot値を編集用一時値へコピーし、変更があったFrameだけCharacter Layer経由でRuntimeへ反映します。
            // Runtime側APIが現在Blend WeightでPlayback倍率を即時再計算するため、BlendTreeの再生位相はリスタートしません。
            float walkAuthoredSpeed = snapshot.WalkAuthoredMotionSpeed;
            float runAuthoredSpeed = snapshot.RunAuthoredMotionSpeed;

            const bool walkChanged = ImGui::DragFloat(
                "Walk Authored m/s",
                &walkAuthoredSpeed,
                0.01f,
                0.10f,
                10.0f,
                "%.2f");
            const bool runChanged = ImGui::DragFloat(
                "Run Authored m/s",
                &runAuthoredSpeed,
                0.01f,
                0.15f,
                15.0f,
                "%.2f");

            if (walkChanged == true || runChanged == true)
            {
                constexpr float MinimumSpeedGap = 0.05f;
                walkAuthoredSpeed = std::max(walkAuthoredSpeed, 0.10f);
                runAuthoredSpeed = std::max(runAuthoredSpeed, walkAuthoredSpeed + MinimumSpeedGap);

                ApplyAuthoredMotionSpeeds(walkAuthoredSpeed, runAuthoredSpeed);
            }

            // =================================================================
            // Tuning utility actions
            // =================================================================
            // ResetはRavenの現在の標準Character速度へ戻します。
            // 初期値へ戻す操作とGameplay速度の変更を混同しないよう、Authored値だけを更新します。
            if (ImGui::Button("Reset Authored Speeds") == true)
            {
                constexpr float DefaultWalkAuthoredSpeed = 1.8f;
                constexpr float DefaultRunAuthoredSpeed = 5.5f;
                ApplyAuthoredMotionSpeeds(
                    DefaultWalkAuthoredSpeed,
                    DefaultRunAuthoredSpeed);
            }

            ImGui::SameLine();

            // 調整結果はそのままLocomotionBlendTreeConfigへ転記できるC++形式にします。
            // Drag/Resetと同じFrameでCopyしても古い値を拾わないよう、Button判定時に最新Snapshotを取り直します。
            if (ImGui::Button("Copy Config") == true)
            {
                const CharacterLocomotionDebugSnapshot latestSnapshot =
                    m_CharacterLayer->GetHumanoidLocomotionDebugSnapshot();
                const std::string tuningConfigText = BuildTuningConfigText(latestSnapshot);
                ImGui::SetClipboardText(tuningConfigText.c_str());
            }

            ImGui::SameLine();
            if (ImGui::Button("Print Config") == true)
            {
                const CharacterLocomotionDebugSnapshot latestSnapshot =
                    m_CharacterLayer->GetHumanoidLocomotionDebugSnapshot();
                const std::string tuningConfigText = BuildTuningConfigText(latestSnapshot);
                std::cout
                    << "[CharacterController] Locomotion Foot Sliding tuning: "
                    << tuningConfigText << '\n';
            }

            if (m_LastTuningError.empty() == false)
            {
                ImGui::TextWrapped("Tuning Error: %s", m_LastTuningError.c_str());
            }
        }
        ImGui::End();
    }

private:
    void ApplyAuthoredMotionSpeeds(
        float walkAuthoredSpeed,
        float runAuthoredSpeed)
    {
        if (m_CharacterLayer == nullptr)
        {
            return;
        }

        std::string tuningError;
        if (m_CharacterLayer->SetHumanoidLocomotionAuthoredMotionSpeeds(
                walkAuthoredSpeed,
                runAuthoredSpeed,
                &tuningError) == false)
        {
            m_LastTuningError = tuningError;
        }
        else
        {
            m_LastTuningError.clear();
        }
    }

    static std::string BuildTuningConfigText(
        const CharacterLocomotionDebugSnapshot& snapshot)
    {
        std::ostringstream stream;
        stream.setf(std::ios::fixed);
        stream.precision(2);
        stream
            << "animationConfig.WalkAuthoredMotionSpeed = "
            << snapshot.WalkAuthoredMotionSpeed << "f; "
            << "animationConfig.RunAuthoredMotionSpeed = "
            << snapshot.RunAuthoredMotionSpeed << "f;";
        return stream.str();
    }

private:
    // CharacterControllerDemoLayerのLifetimeはSceneが所有します。
    // OverlayはRuntime調整APIを呼ぶため非constの非所有pointerとして保持します。
    CharacterControllerDemoLayer* m_CharacterLayer = nullptr;
    std::string m_LastTuningError;
};

} // namespace Raven
