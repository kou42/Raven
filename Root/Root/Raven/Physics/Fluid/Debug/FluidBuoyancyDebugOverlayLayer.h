#pragma once

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "Raven/UI/Widgets/UIWindow.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UISlider.h"
#include "Raven/UI/Widgets/UIScrollView.h"
#include "Raven/UI/Widgets/UIPanel.h"

#include "Raven/Core/Application.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Math/MathQuatanion.h"
#include "Raven/Physics/Fluid/Debug/FluidCouplingPreset.h"
#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Physics/PhysicsWorld.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Scene.h"

namespace Raven
{

// ============================================================================
// FluidBuoyancyDebugOverlayLayer
// ============================================================================
// Fluid SPH Demoが生成するBox / SphereのWorld座標とCoupling診断値を表示し、同じ初期条件へ何度でも
// 戻して設定を比較検証できる専用HUDです。Fluid solverやCoupling本体へDebug入力依存を持ち込まず、
// Entity名を手掛かりにScene上の検証Bodyだけを操作します。
class FluidBuoyancyDebugOverlayLayer final : public Layer
{
public:
    explicit FluidBuoyancyDebugOverlayLayer(Application& application)
        : m_Application(application)
    {
    }

    void OnAttach() override
    {
        // 起動直後から見つけやすいサイズ・水面付近の位置へ揃えます。
        // FluidSPHDemoLayerのOnAttach後にこのOverlayを登録するため、ここで対象Entityを取得できます。
        ResetTestBodies();
        ResetMeasurement();
        BuildRavenUI();
    }

    void OnDetach() override
    {
        if (m_Window != nullptr)
        {
            m_Application.GetUIContext().GetRootElement().RemoveChild(m_Window);
        }
        m_Window = nullptr;
        m_Labels.clear();
        m_Sliders.clear();
        m_SliderLabels.clear();
        m_ScrollView = nullptr;
        m_SelectedBindingIndex = 0u;
    }

    // Resetの入力は描画フックに依存させず、ImGui無効時にも1押下1回で処理します。
    void OnUpdate(float deltaTime) override
    {
        static_cast<void>(deltaTime);
        const bool resetKeyPressed = Input::IsKeyPressed(Key::R);
        const bool resetRequested = resetKeyPressed == true && m_WasResetKeyPressed == false;
        m_WasResetKeyPressed = resetKeyPressed;
        if (resetRequested == true)
        {
            ResetTestBodies();
            ResetMeasurement();
        }
        UpdateRavenUI();
        // UI Windowを縮小した場合でも、ScrollViewのViewportを追従させます。
        if (m_Window != nullptr && m_ScrollView != nullptr)
        {
            const math::Vec2 size = m_Window->GetSize();
            m_ScrollView->SetSize(math::Vec2(std::max(120.0f, size.x - 20.0f),
                std::max(48.0f, size.y - 42.0f)));
        }
    }

    // Retained UIの構築はApplication UIContextのTreeに統合します。
    void BuildRavenUI()
    {
        auto window = CreateScope<UIWindow>();
        window->SetTitle("Fluid Buoyancy Debug");
        window->SetPosition(math::Vec2(540.0f, 10.0f));
        window->SetSize(math::Vec2(550.0f, 650.0f));
        window->SetPreferredSize(math::Vec2(550.0f, 650.0f));

        // Window直下にScrollViewを置き、固定高さのContentへ既存操作群を配置します。
        // UIWindowのResizeはViewportだけを変更し、Contentの座標系は維持します。
        auto scroll = CreateScope<UIScrollView>();
        scroll->SetPosition(math::Vec2(10.0f, 32.0f));
        scroll->SetSize(math::Vec2(530.0f, 608.0f));
        scroll->SetHorizontalScrollBarEnabled(false);
        auto content = CreateScope<UIPanel>();
        content->SetPosition(math::Vec2(0.0f, 0.0f));
        content->SetSize(math::Vec2(530.0f, 960.0f));
        content->SetPreferredSize(math::Vec2(530.0f, 960.0f));
        UIPanel& contentPanel = *content;
        const char* lines[] = {
            "Fluid Buoyancy Debug", "Box : not found", "Sphere : not found",
            "Coupling : not registered", "Static Coupling : --", "Rigid Coupling : --",
            "Particle Radius : --", "Restitution : --", "Drag : --",
            "Pressure Reaction : --", "Buoyancy : --",
            "Static Statistics : --", "Rigid Statistics : --", "Impulse : --",
            "Applied : --", "Displaced Mass : --", "Measurement : --",
            "Fixed Time : --", "Contacts : --", "Impulse Total : --",
            "Applied Count : --", "Displaced Mass Total : --",
            "Impulse / Fixed Step : --"
        };
        for (std::size_t i = 0u; i < 23u; ++i)
        {
            m_Labels.push_back(AddLabel(contentPanel, 32.0f + static_cast<float>(i) * 25.0f, lines[i]));
        }
        AddButton(contentPanel, 592.0f, 10.0f, 100.0f, "Water",
            [this]() { ApplyPreset(ph::FluidCouplingPreset::Water); });
        AddButton(contentPanel, 592.0f, 120.0f, 110.0f, "Heavy Fluid",
            [this]() { ApplyPreset(ph::FluidCouplingPreset::HeavyFluid); });
        AddButton(contentPanel, 592.0f, 240.0f, 100.0f, "High Drag",
            [this]() { ApplyPreset(ph::FluidCouplingPreset::HighDrag); });
        AddButton(contentPanel, 592.0f, 350.0f, 100.0f, "Off",
            [this]() { ApplyPreset(ph::FluidCouplingPreset::CouplingOff); });
        AddButton(contentPanel, 630.0f, 10.0f, 155.0f, "Static On/Off",
            [this]() { ToggleStatic(); });
        AddButton(contentPanel, 630.0f, 175.0f, 155.0f, "Rigid On/Off",
            [this]() { ToggleRigid(); });
        AddButton(contentPanel, 630.0f, 340.0f, 155.0f, "Pause/Start",
            [this]() { ToggleMeasurement(); });
        AddButton(contentPanel, 674.0f, 10.0f, 155.0f, "Reset Bodies",
            [this]() { ResetTestBodies(); ResetMeasurement(); });
        AddButton(contentPanel, 674.0f, 175.0f, 155.0f, "Reset Measure",
            [this]() { ResetMeasurement(); });
        // Bindingが複数ある場合も、選択中の1つだけを操作して意図しない一括変更を防ぎます。
        AddButton(contentPanel, 674.0f, 340.0f, 155.0f, "Next Binding",
            [this]() { SelectNextBinding(); });
        // 既存ImGuiの5係数調整をUISliderで復元します。
        // Binding自体はScene交換で破棄されるためCallbackにpointerをCaptureしません。
        const char* coefficientNames[] = {
            "Particle Radius", "Restitution", "Drag", "Pressure Reaction", "Buoyancy"
        };
        const float minimum[] = { 0.001f, 0.0f, 0.0f, 0.0f, 0.0f };
        const float maximum[] = { 2.0f, 1.0f, 5.0f, 10.0f, 10.0f };
        for (std::size_t i = 0u; i < 5u; ++i)
        {
            const float y = 726.0f + static_cast<float>(i) * 43.0f;
            m_SliderLabels.push_back(AddLabel(contentPanel, y, coefficientNames[i]));
            auto slider = CreateScope<UISlider>();
            slider->SetPosition(math::Vec2(280.0f, y + 2.0f));
            slider->SetSize(math::Vec2(240.0f, 22.0f));
            slider->SetRange(minimum[i], maximum[i]);
            slider->SetKeyboardStep(i == 0u ? 0.005f : 0.01f);
            slider->SetFocusable(true);
            slider->SetOnValueChanged([this, i](float value) { SetCoefficient(i, value); });
            m_Sliders.push_back(static_cast<UISlider*>(contentPanel.AddChild(std::move(slider))));
        }
        scroll->SetContent(std::move(content));
        m_ScrollView = static_cast<UIScrollView*>(window->AddChild(std::move(scroll)));
        m_Window = m_Application.GetUIContext().GetRootElement().AddChild(std::move(window));
    }

    UILabel* AddLabel(UIElement& window, float y, const std::string& text)
    {
        auto label = CreateScope<UILabel>();
        label->SetFont(m_Application.GetRuntimeUIFont());
        label->SetText(text);
        label->SetPosition(math::Vec2(12.0f, y));
        label->SetSize(math::Vec2(526.0f, 24.0f));
        label->SetHitTestVisible(false);
        return static_cast<UILabel*>(window.AddChild(std::move(label)));
    }

    void AddButton(UIElement& window, float y, float x, float width,
        const std::string& caption, std::function<void()> action)
    {
        auto button = CreateScope<UIButton>();
        button->SetPosition(math::Vec2(x, y));
        button->SetSize(math::Vec2(width, 32.0f));
        button->SetFocusable(true);
        button->SetOnClick(std::move(action));
        auto label = CreateScope<UILabel>();
        label->SetFont(m_Application.GetRuntimeUIFont());
        label->SetText(caption);
        label->SetPosition(math::Vec2(5.0f, 3.0f));
        label->SetSize(math::Vec2(width - 10.0f, 26.0f));
        label->SetHitTestVisible(false);
        button->AddChild(std::move(label));
        window.AddChild(std::move(button));
    }

    ph::FluidWorld* GetFluidWorld()
    {
        Scene* scene = m_Application.GetScene();
        if (scene == nullptr) { return nullptr; }
        return &scene->GetPhysicsSimulationWorld().GetFluidWorld();
    }

    ph::FluidCouplingBinding* GetSelectedBinding()
    {
        ph::FluidWorld* fluid = GetFluidWorld();
        if (fluid == nullptr) { return nullptr; }
        const auto& bindings = fluid->GetCouplingBindings();
        if (bindings.empty() == true || m_SelectedBindingIndex >= bindings.size()) { return nullptr; }
        return bindings[m_SelectedBindingIndex];
    }

    void SelectNextBinding()
    {
        ph::FluidWorld* fluid = GetFluidWorld();
        if (fluid == nullptr) { return; }
        const std::size_t count = fluid->GetCouplingBindings().size();
        if (count == 0u) { return; }
        m_SelectedBindingIndex = (m_SelectedBindingIndex + 1u) % count;
    }

    void SetCoefficient(std::size_t index, float value)
    {
        if (m_SynchronizingSliders == true) { return; }
        ph::FluidCouplingBinding* binding = GetSelectedBinding();
        if (binding == nullptr) { return; }
        // Debug HUDは登録済みBindingの実体を直接編集します。
        // FluidWorldはBindingをコピーせず非所有参照しているため、変更値は再登録なしで
        // 次のfixed-stepのResolveCouplings()から利用されます。
        // DemoではStatic/Dynamic Couplingが同じParticle表面を扱うため、半径を同期します。
        // 反発係数も同じ接触条件として両方へ反映します。
        switch (index)
        {
        case 0u:
            binding->StaticColliderSettings.ParticleRadius = std::max(value, 0.001f);
            binding->RigidBodySettings.ParticleRadius = std::max(value, 0.001f);
            break;
        case 1u:
            binding->StaticColliderSettings.Restitution = value;
            binding->RigidBodySettings.Restitution = value;
            break;
        case 2u: binding->RigidBodySettings.DragCoefficient = std::max(value, 0.0f); break;
        case 3u: binding->RigidBodySettings.PressureReactionCoefficient = std::max(value, 0.0f); break;
        case 4u: binding->RigidBodySettings.BuoyancyCoefficient = std::max(value, 0.0f); break;
        default: break;
        }
    }

    void ApplyPreset(ph::FluidCouplingPreset preset)
    {
        ph::FluidCouplingBinding* binding = GetSelectedBinding();
        if (binding == nullptr) { return; }
        // WaterはDemoの基準値です。手動調整後もPresetで比較条件へ戻せます。
        ph::ApplyFluidCouplingPreset(*binding, preset);
        // Preset比較ではBodyの位置・速度・回転状態を同一にすることが重要です。
        // 選択BindingへのPreset適用後に一度だけResetし、重複Teleportを避けます。
        // 比較条件を揃えるためPreset変更後はBodyと計測値を同時にResetします。
        ResetTestBodies();
        ResetMeasurement();
    }

    void ToggleStatic()
    {
        ph::FluidCouplingBinding* binding = GetSelectedBinding();
        if (binding != nullptr)
        {
            binding->StaticColliderCouplingEnabled = binding->StaticColliderCouplingEnabled == false;
        }
    }

    void ToggleRigid()
    {
        ph::FluidCouplingBinding* binding = GetSelectedBinding();
        if (binding != nullptr)
        {
            binding->RigidBodyCouplingEnabled = binding->RigidBodyCouplingEnabled == false;
        }
    }

    void ToggleMeasurement()
    {
        ph::FluidWorld* fluid = GetFluidWorld();
        if (fluid != nullptr)
        {
            fluid->SetCouplingMeasurementEnabled(fluid->IsCouplingMeasurementEnabled() == false);
        }
    }

    template<typename... Args>
    void SetLine(std::size_t index, const char* format, Args... args)
    {
        char buffer[384];
        std::snprintf(buffer, sizeof(buffer), format, args...);
        m_Labels[index]->SetText(buffer);
    }

    void UpdateRavenUI()
    {
        if (m_Window == nullptr) { return; }
        Scene* scene = m_Application.GetScene();
        m_Window->SetVisible(scene != nullptr);
        if (scene == nullptr) { return; }
        bool boxFound = false;
        bool sphereFound = false;
        for (auto [entity, tag, transform, rigidBody]
            : scene->View<TagComponent, TransformComponent, RigidBodyComponent>())
        {
            if (tag.Tag == "Fluid Buoyancy Test Box")
            {
                SetLine(1u, "Box Pos : %.2f %.2f %.2f / Vel : %.2f %.2f %.2f",
                    transform.Position.x, transform.Position.y, transform.Position.z,
                    rigidBody.LinearVelocity.x, rigidBody.LinearVelocity.y, rigidBody.LinearVelocity.z);
                boxFound = true;
            }
            else if (tag.Tag == "Fluid Buoyancy Test Sphere")
            {
                SetLine(2u, "Sphere Pos : %.2f %.2f %.2f / Vel : %.2f %.2f %.2f",
                    transform.Position.x, transform.Position.y, transform.Position.z,
                    rigidBody.LinearVelocity.x, rigidBody.LinearVelocity.y, rigidBody.LinearVelocity.z);
                sphereFound = true;
            }
        }
        if (boxFound == false) { m_Labels[1]->SetText("Box : not found"); }
        if (sphereFound == false) { m_Labels[2]->SetText("Sphere : not found"); }
        ph::FluidWorld& fluid = scene->GetPhysicsSimulationWorld().GetFluidWorld();
        const auto& bindings = fluid.GetCouplingBindings();
        if (m_SelectedBindingIndex >= bindings.size()) { m_SelectedBindingIndex = 0u; }
        SetLine(3u, "Coupling Bindings : %llu / Selected : %llu",
            static_cast<unsigned long long>(bindings.size()),
            static_cast<unsigned long long>(m_SelectedBindingIndex));
        ph::FluidCouplingBinding* binding = GetSelectedBinding();
        if (binding != nullptr)
        {
            SetLine(4u, "Static Coupling : %s", binding->StaticColliderCouplingEnabled == true ? "On" : "Off");
            SetLine(5u, "Rigid Coupling : %s", binding->RigidBodyCouplingEnabled == true ? "On" : "Off");
            SetLine(6u, "Particle Radius : %.3f", binding->RigidBodySettings.ParticleRadius);
            SetLine(7u, "Restitution : %.2f", binding->RigidBodySettings.Restitution);
            SetLine(8u, "Drag : %.2f", binding->RigidBodySettings.DragCoefficient);
            SetLine(9u, "Pressure Reaction : %.2f", binding->RigidBodySettings.PressureReactionCoefficient);
            SetLine(10u, "Buoyancy : %.2f", binding->RigidBodySettings.BuoyancyCoefficient);
        }
        else
        {
            for (std::size_t i = 4u; i <= 10u; ++i) { m_Labels[i]->SetText("Coupling : unavailable"); }
        }
        if (binding != nullptr)
        {
            const float values[] = {
                binding->RigidBodySettings.ParticleRadius,
                binding->RigidBodySettings.Restitution,
                binding->RigidBodySettings.DragCoefficient,
                binding->RigidBodySettings.PressureReactionCoefficient,
                binding->RigidBodySettings.BuoyancyCoefficient
            };
            m_SynchronizingSliders = true;
            for (std::size_t i = 0u; i < m_Sliders.size(); ++i)
            {
                m_Sliders[i]->SetValue(values[i]);
                char buffer[96];
                std::snprintf(buffer, sizeof(buffer), "%s : %.3f",
                    i == 0u ? "Radius" : i == 1u ? "Restitution" :
                    i == 2u ? "Drag" : i == 3u ? "Pressure" : "Buoyancy", values[i]);
                m_SliderLabels[i]->SetText(buffer);
            }
            m_SynchronizingSliders = false;
        }
        // FluidWorldが保持する直近Fixed Stepの診断値を表示します。
        // HUD側はCoupling実装へ直接依存せず、Domain境界として公開されたStatisticsだけを参照します。
        const auto& stat = fluid.GetLastStaticColliderCouplingStatistics();
        const auto& rigid = fluid.GetLastRigidBodyCouplingStatistics();
        SetLine(11u, "Static : collider=%llu candidate=%llu resolved=%llu",
            static_cast<unsigned long long>(stat.SupportedColliderCount),
            static_cast<unsigned long long>(stat.CandidatePairCount),
            static_cast<unsigned long long>(stat.ResolvedContactCount));
        SetLine(12u, "Rigid : body=%llu candidate=%llu resolved=%llu",
            static_cast<unsigned long long>(rigid.DynamicBodyCount),
            static_cast<unsigned long long>(rigid.CandidatePairCount),
            static_cast<unsigned long long>(rigid.ResolvedContactCount));
        SetLine(13u, "Impulse : normal=%.4f drag=%.4f pressure=%.4f buoyancy=%.4f",
            rigid.TotalNormalImpulse, rigid.TotalDragImpulse,
            rigid.TotalPressureImpulse, rigid.TotalBuoyancyImpulse);
        SetLine(14u, "Applied : N=%llu D=%llu P=%llu B=%llu",
            static_cast<unsigned long long>(rigid.AppliedImpulseCount),
            static_cast<unsigned long long>(rigid.AppliedDragImpulseCount),
            static_cast<unsigned long long>(rigid.AppliedPressureImpulseCount),
            static_cast<unsigned long long>(rigid.AppliedBuoyancyImpulseCount));
        SetLine(15u, "Displaced Mass : %.4f", rigid.TotalDisplacedFluidMass);
        // MeasurementはFluidWorldのfixed-step終了時に更新されるため、
        // 描画FPSやcatch-up回数に依存しない累積値として扱います。
        const auto& measurement = fluid.GetCouplingMeasurement();
        SetLine(16u, "Measurement : %s", fluid.IsCouplingMeasurementEnabled() == true ? "Running" : "Paused");
        SetLine(17u, "Fixed Time : %.2f / Steps : %llu", measurement.ElapsedFixedTime,
            static_cast<unsigned long long>(measurement.FixedStepCount));
        SetLine(18u, "Contacts : %llu", static_cast<unsigned long long>(measurement.ResolvedContactCount));
        SetLine(19u, "Impulse Total : N=%.4f D=%.4f P=%.4f B=%.4f",
            measurement.TotalNormalImpulse, measurement.TotalDragImpulse,
            measurement.TotalPressureImpulse, measurement.TotalBuoyancyImpulse);
        SetLine(20u, "Applied Count : N=%llu D=%llu P=%llu B=%llu",
            static_cast<unsigned long long>(measurement.AppliedNormalImpulseCount),
            static_cast<unsigned long long>(measurement.AppliedDragImpulseCount),
            static_cast<unsigned long long>(measurement.AppliedPressureImpulseCount),
            static_cast<unsigned long long>(measurement.AppliedBuoyancyImpulseCount));
        SetLine(21u, "Displaced Mass Total : %.4f", measurement.TotalDisplacedFluidMass);
        // 計測値はFixed Step境界で蓄積されるため、描画FPSではなくFixedStepCountで正規化します。
        if (measurement.FixedStepCount > 0u)
        {
            const float inverse = 1.0f / static_cast<float>(measurement.FixedStepCount);
            SetLine(22u, "Impulse / Step : N=%.4f D=%.4f P=%.4f B=%.4f",
                measurement.TotalNormalImpulse * inverse,
                measurement.TotalDragImpulse * inverse,
                measurement.TotalPressureImpulse * inverse,
                measurement.TotalBuoyancyImpulse * inverse);
        }
        else
        {
            m_Labels[22u]->SetText("Impulse / Step : -- (no fixed steps)");
        }
    }

    void ResetMeasurement()
    {
        Scene* scene = m_Application.GetScene();
        if (scene == nullptr)
        {
            return;
        }

        scene->GetPhysicsSimulationWorld().GetFluidWorld().ResetCouplingMeasurement();
    }

    void ResetTestBodies()
    {
        Scene* scene = m_Application.GetScene();
        if (scene == nullptr)
        {
            return;
        }

        constexpr math::Vec3 BoxResetPosition{ 48.7f, 6.9f, 50.0f };
        constexpr math::Vec3 SphereResetPosition{ 51.3f, 6.9f, 50.0f };
        constexpr float BodyVisualSize = 1.20f;
        constexpr float SphereRadius = BodyVisualSize * 0.5f;

        ph::PhysicsWorld& physicsWorld = scene->GetPhysicsWorld();
        for (auto [entity, tag, transform, rigidBody, collider]
            : scene->View<TagComponent, TransformComponent, RigidBodyComponent, ColliderComponent>())
        {
            math::Vec3 resetPosition{};
            bool isTarget = false;

            if (tag.Tag == "Fluid Buoyancy Test Box")
            {
                resetPosition = BoxResetPosition;
                transform.Scale = { BodyVisualSize, BodyVisualSize, BodyVisualSize };
                collider.HalfExtents = transform.Scale * 0.5f;
                isTarget = true;
            }
            else if (tag.Tag == "Fluid Buoyancy Test Sphere")
            {
                resetPosition = SphereResetPosition;
                transform.Scale = { BodyVisualSize, BodyVisualSize, BodyVisualSize };
                collider.Radius = SphereRadius;
                isTarget = true;
            }

            if (isTarget == false)
            {
                continue;
            }

            // Scene::View()の先頭要素は既にGenerationを含むEntityです。
            // Dynamic RigidBodyの位置変更はこのEntityをそのままPhysicsWorldへ渡し、
            // Transform直接更新ではなく通常のTeleport経路で起床処理まで行います。
            physicsWorld.Teleport(*scene, entity, resetPosition);

            // Reset前の落下速度・回転・Fluidから受けたImpulse履歴を次の試行へ持ち越さないよう、
            // RigidBodyの運動状態を初期化します。LinearVelocityもPhysicsWorldの制御APIを通し、
            // Teleport直後のBodyが確実に起床した状態で次の比較を開始します。
            transform.Rotation = { 0.0f, 0.0f, 0.0f };
            physicsWorld.SetLinearVelocity(*scene, entity, { 0.0f, 0.0f, 0.0f });
            rigidBody.AngularVelocity = { 0.0f, 0.0f, 0.0f };
            rigidBody.Force = { 0.0f, 0.0f, 0.0f };
            rigidBody.Torque = { 0.0f, 0.0f, 0.0f };
            rigidBody.Orientation = math::Quat::Identity();
            rigidBody.OrientationInitialized = true;
            rigidBody.IsSleeping = false;
            rigidBody.SleepTimer = 0.0f;
        }
    }

private:
    Application& m_Application;
    bool m_WasResetKeyPressed = false;
    UIElement* m_Window = nullptr;
    UIScrollView* m_ScrollView = nullptr;
    std::vector<UILabel*> m_Labels;
    std::vector<UILabel*> m_SliderLabels;
    std::vector<UISlider*> m_Sliders;
    bool m_SynchronizingSliders = false;
    std::size_t m_SelectedBindingIndex = 0u;
};

} // namespace Raven
