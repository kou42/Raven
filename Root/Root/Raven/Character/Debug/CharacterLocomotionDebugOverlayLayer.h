// Raven/Character/Debug/CharacterLocomotionDebugOverlayLayer.h
#pragma once
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "Raven/Character/Debug/CharacterControllerDemoLayer.h"
#include "Raven/Core/Application.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/UI/Widgets/UIWindow.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UISlider.h"

namespace Raven
{
// Scene-owned Characterへの参照は保存せず、調整イベントごとに再取得します。
// ImGuiの描画フックを使わず、Application-owned Raven UI Treeとして診断値を更新します。
class CharacterLocomotionDebugOverlayLayer final : public Layer
{
public:
    explicit CharacterLocomotionDebugOverlayLayer(Application& application)
        : m_Application(&application) {}

    void OnAttach() override
    {
        if (m_Application == nullptr) { return; }
        auto window = CreateScope<UIWindow>();
        window->SetTitle("Character Locomotion Debug");
        window->SetPosition(math::Vec2(10.0f, 10.0f));
        window->SetSize(math::Vec2(510.0f, 840.0f));
        window->SetPreferredSize(math::Vec2(510.0f, 840.0f));
        AddText(*window, 32.0f, "Character Locomotion");
        const char* names[] = {
            "Animation", "Actual Speed", "Parameter", "Reference", "Playback",
            "Playback Clamp", "Blend", "Weight", "Threshold", "Gameplay Goal",
            "Runtime Axis", "Profile Axis", "Clamped", "Tuning Error"
        };
        for (std::size_t i = 0; i < 14u; ++i)
        {
            m_Diagnostics.push_back(AddText(*window, 62.0f + static_cast<float>(i) * 26.0f, names[i]));
        }

        AddText(*window, 438.0f, "Blend Threshold Tuning");
        const char* sliderNames[] = {
            "Idle Threshold", "Walk Threshold", "Run Threshold", "Sprint Threshold",
            "Walk Authored m/s", "Run Authored m/s", "Sprint Authored m/s"
        };
        const float minimum[] = { 0.0f, 0.05f, 0.10f, 0.15f, 0.10f, 0.15f, 0.20f };
        const float maximum[] = { 5.0f, 10.0f, 15.0f, 20.0f, 10.0f, 15.0f, 20.0f };
        for (std::size_t i = 0; i < 7u; ++i)
        {
            const float y = 466.0f + static_cast<float>(i) * 40.0f;
            m_TuningLabels.push_back(AddText(*window, y, sliderNames[i]));
            auto slider = CreateScope<UISlider>();
            slider->SetPosition(math::Vec2(258.0f, y + 4.0f));
            slider->SetSize(math::Vec2(226.0f, 22.0f));
            slider->SetFocusable(true);
            slider->SetRange(minimum[i], maximum[i]);
            slider->SetKeyboardStep(0.01f);
            slider->SetOnValueChanged([this, i](float value) { ApplySliderValue(i, value); });
            m_Sliders.push_back(static_cast<UISlider*>(window->AddChild(std::move(slider))));
        }

        AddButton(*window, 756.0f, 10.0f, 150.0f, "Reset Thresholds",
            [this]() { ResetThresholds(); });
        AddButton(*window, 756.0f, 170.0f, 150.0f, "Reset Speeds",
            [this]() { ResetSpeeds(); });
        AddButton(*window, 756.0f, 330.0f, 150.0f, "Print Config",
            [this]() { PrintConfig(); });
        AddButton(*window, 796.0f, 10.0f, 150.0f, "Save Profile",
            [this]() { SaveProfile(); });
        m_Window = m_Application->GetUIContext().GetRootElement().AddChild(std::move(window));
    }

    void OnDetach() override
    {
        if (m_Application != nullptr && m_Window != nullptr)
        {
            m_Application->GetUIContext().GetRootElement().RemoveChild(m_Window);
        }
        m_Window = nullptr;
        m_Diagnostics.clear();
        m_TuningLabels.clear();
        m_Sliders.clear();
        m_Application = nullptr;
    }

    void OnUpdate(float deltaTime) override
    {
        static_cast<void>(deltaTime);
        CharacterControllerDemoLayer* character = ResolveCharacterLayer();
        if (m_Window == nullptr) { return; }
        m_Window->SetVisible(character != nullptr);
        if (character == nullptr) { return; }
        const CharacterLocomotionDebugSnapshot s = character->GetHumanoidLocomotionDebugSnapshot();
        SetDiagnostic(0, "Animation : %s", s.AnimationActive == true ? "Active" : "Inactive");
        SetDiagnostic(1, "Actual Speed : %.2f", s.ActualHorizontalSpeed);
        SetDiagnostic(2, "Parameter : %.2f", s.ParameterValue);
        SetDiagnostic(3, "Reference : %.2f", s.ReferenceMotionSpeed);
        SetDiagnostic(4, "Playback : %.2fx", s.PlaybackSpeed);
        const char* clamp = "Free";
        if (s.AnimationActive == true && s.ReferenceMotionSpeed > 0.0f)
        {
            if (s.PlaybackSpeed <= 0.501f) { clamp = "LOWER (0.50x)"; }
            else if (s.PlaybackSpeed >= 1.999f) { clamp = "UPPER (2.00x)"; }
        }
        SetDiagnostic(5, "Playback Clamp : %s", clamp);
        SetDiagnostic(6, "Blend : %s -> %s",
            s.LeftAnimationName.empty() ? "<none>" : s.LeftAnimationName.c_str(),
            s.RightAnimationName.empty() ? "<none>" : s.RightAnimationName.c_str());
        SetDiagnostic(7, "Weight : %.2f / %.2f", s.LeftWeight, s.RightWeight);
        SetDiagnostic(8, "Threshold : %.2f -> %.2f", s.LeftThreshold, s.RightThreshold);
        SetDiagnostic(9, "Gameplay Goal : Walk %.2f / Run %.2f / Sprint %.2f",
            s.GameplayWalkSpeed, s.GameplayRunSpeed, s.GameplaySprintSpeed);
        SetDiagnostic(10, "Runtime Axis : %.2f / %.2f / %.2f / %.2f",
            s.IdleThreshold, s.WalkThreshold, s.RunThreshold, s.SprintThreshold);
        SetDiagnostic(11, "Profile Axis : %.2f / %.2f / %.2f / %.2f",
            s.ProfileIdleThreshold, s.ProfileWalkThreshold, s.ProfileRunThreshold, s.ProfileSprintThreshold);
        SetDiagnostic(12, "Clamped : %s", s.IsClamped == true ? "true" : "false");
        SetDiagnostic(13, "Tuning Error : %s", m_LastTuningError.c_str());

        const float values[] = {
            s.IdleThreshold, s.WalkThreshold, s.RunThreshold, s.SprintThreshold,
            s.WalkAuthoredMotionSpeed, s.RunAuthoredMotionSpeed, s.SprintAuthoredMotionSpeed
        };
        // UISlider::SetValueは通知を発行するため、Snapshot同期とユーザー入力を分離します。
        m_Synchronizing = true;
        for (std::size_t i = 0; i < m_Sliders.size(); ++i)
        {
            m_Sliders[i]->SetValue(values[i]);
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer), "%s : %.2f",
                i < 4u ? (i == 0u ? "Idle" : i == 1u ? "Walk" : i == 2u ? "Run" : "Sprint")
                       : (i == 4u ? "Walk Authored" : i == 5u ? "Run Authored" : "Sprint Authored"),
                values[i]);
            m_TuningLabels[i]->SetText(buffer);
        }
        m_Synchronizing = false;
    }

private:
    UILabel* AddText(UIWindow& window, float y, const std::string& text)
    {
        auto label = CreateScope<UILabel>();
        label->SetFont(m_Application->GetRuntimeUIFont());
        label->SetText(text);
        label->SetPosition(math::Vec2(12.0f, y));
        label->SetSize(math::Vec2(480.0f, 24.0f));
        label->SetHitTestVisible(false);
        return static_cast<UILabel*>(window.AddChild(std::move(label)));
    }

    void AddButton(UIWindow& window, float y, float x, float width,
        const std::string& caption, std::function<void()> callback)
    {
        auto button = CreateScope<UIButton>();
        button->SetPosition(math::Vec2(x, y));
        button->SetSize(math::Vec2(width, 32.0f));
        button->SetFocusable(true);
        button->SetOnClick(std::move(callback));
        auto label = CreateScope<UILabel>();
        label->SetFont(m_Application->GetRuntimeUIFont());
        label->SetText(caption);
        label->SetPosition(math::Vec2(6.0f, 3.0f));
        label->SetSize(math::Vec2(width - 12.0f, 26.0f));
        label->SetHitTestVisible(false);
        button->AddChild(std::move(label));
        window.AddChild(std::move(button));
    }

    template<typename... Args>
    void SetDiagnostic(std::size_t index, const char* format, Args... args)
    {
        char buffer[384];
        std::snprintf(buffer, sizeof(buffer), format, args...);
        m_Diagnostics[index]->SetText(buffer);
    }

    void ApplySliderValue(std::size_t index, float value)
    {
        if (m_Synchronizing == true) { return; }
        CharacterControllerDemoLayer* character = ResolveCharacterLayer();
        if (character == nullptr) { return; }
        const CharacterLocomotionDebugSnapshot s = character->GetHumanoidLocomotionDebugSnapshot();
        if (index < 4u)
        {
            float values[] = { s.IdleThreshold, s.WalkThreshold, s.RunThreshold, s.SprintThreshold };
            values[index] = value;
            constexpr float gap = 0.05f;
            values[0] = std::max(values[0], 0.0f);
            values[1] = std::max(values[1], values[0] + gap);
            values[2] = std::max(values[2], values[1] + gap);
            values[3] = std::max(values[3], values[2] + gap);
            ApplyThresholds(values[0], values[1], values[2], values[3]);
        }
        else
        {
            float values[] = { s.WalkAuthoredMotionSpeed, s.RunAuthoredMotionSpeed, s.SprintAuthoredMotionSpeed };
            values[index - 4u] = value;
            constexpr float gap = 0.05f;
            values[0] = std::max(values[0], 0.10f);
            values[1] = std::max(values[1], values[0] + gap);
            values[2] = std::max(values[2], values[1] + gap);
            ApplyAuthoredMotionSpeeds(values[0], values[1], values[2]);
        }
    }

    void ResetThresholds()
    {
        CharacterControllerDemoLayer* character = ResolveCharacterLayer();
        if (character == nullptr) { return; }
        const auto s = character->GetHumanoidLocomotionDebugSnapshot();
        ApplyThresholds(s.ProfileIdleThreshold, s.ProfileWalkThreshold,
            s.ProfileRunThreshold, s.ProfileSprintThreshold);
    }

    void ResetSpeeds()
    {
        CharacterControllerDemoLayer* character = ResolveCharacterLayer();
        if (character == nullptr) { return; }
        const auto s = character->GetHumanoidLocomotionDebugSnapshot();
        ApplyAuthoredMotionSpeeds(s.ProfileWalkAuthoredMotionSpeed,
            s.ProfileRunAuthoredMotionSpeed, s.ProfileSprintAuthoredMotionSpeed);
    }

    void PrintConfig()
    {
        CharacterControllerDemoLayer* character = ResolveCharacterLayer();
        if (character != nullptr)
        {
            std::cout << "[CharacterController] Locomotion tuning: "
                << BuildTuningConfigText(character->GetHumanoidLocomotionDebugSnapshot()) << '\n';
        }
    }

    void SaveProfile()
    {
        CharacterControllerDemoLayer* character = ResolveCharacterLayer();
        if (character == nullptr) { return; }
        std::string error;
        if (character->SaveHumanoidLocomotionProfileTuning(&error) == false)
        {
            m_LastTuningError = error;
        }
        else
        {
            m_LastTuningError.clear();
        }
    }

    void ApplyAuthoredMotionSpeeds(
        float walkAuthoredSpeed,
        float runAuthoredSpeed,
        float sprintAuthoredSpeed)
    {
        CharacterControllerDemoLayer* characterLayer = ResolveCharacterLayer();
        if (characterLayer == nullptr)
        {
            return;
        }

        std::string tuningError;
        if (characterLayer->SetHumanoidLocomotionAuthoredMotionSpeeds(
                walkAuthoredSpeed,
                runAuthoredSpeed,
                sprintAuthoredSpeed,
                &tuningError) == false)
        {
            m_LastTuningError = tuningError;
        }
        else
        {
            m_LastTuningError.clear();
        }
    }

    void ApplyThresholds(
        float idleThreshold,
        float walkThreshold,
        float runThreshold,
        float sprintThreshold)
    {
        CharacterControllerDemoLayer* characterLayer = ResolveCharacterLayer();
        if (characterLayer == nullptr)
        {
            return;
        }

        std::string tuningError;
        if (characterLayer->SetHumanoidLocomotionThresholds(
                idleThreshold,
                walkThreshold,
                runThreshold,
                sprintThreshold,
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
            << "profile.Locomotion.IdleThreshold = "
            << snapshot.IdleThreshold << "f; "
            << "profile.Locomotion.WalkThreshold = "
            << snapshot.WalkThreshold << "f; "
            << "profile.Locomotion.RunThreshold = "
            << snapshot.RunThreshold << "f; "
            << "profile.Locomotion.SprintThreshold = "
            << snapshot.SprintThreshold << "f; "
            << "profile.Locomotion.WalkAuthoredMotionSpeed = "
            << snapshot.WalkAuthoredMotionSpeed << "f; "
            << "profile.Locomotion.RunAuthoredMotionSpeed = "
            << snapshot.RunAuthoredMotionSpeed << "f; "
            << "profile.Locomotion.SprintAuthoredMotionSpeed = "
            << snapshot.SprintAuthoredMotionSpeed << "f;";
        return stream.str();
    }

private:
    CharacterControllerDemoLayer* ResolveCharacterLayer() const
    {
        // CharacterControllerDemoLayerのLifetimeはSceneが所有します。
        // pointerをmemberへ保存せず、その操作中だけActive Sceneから借用することが重要です。
        if (m_Application == nullptr)
        {
            return nullptr;
        }

        Scene* scene = m_Application->GetScene();
        if (scene == nullptr)
        {
            return nullptr;
        }

        return scene->FindLayer<CharacterControllerDemoLayer>();
    }

    Application* m_Application = nullptr;
    std::string m_LastTuningError;
    UIElement* m_Window = nullptr;
    std::vector<UILabel*> m_Diagnostics;
    std::vector<UILabel*> m_TuningLabels;
    std::vector<UISlider*> m_Sliders;
    bool m_Synchronizing = false;
};
} // namespace Raven
