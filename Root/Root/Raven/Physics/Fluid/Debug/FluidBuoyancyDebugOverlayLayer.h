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
    }

    // Retained UIの構築はApplication UIContextのTreeに統合します。
    void BuildRavenUI()
    {
        auto window = CreateScope<UIWindow>();
        window->SetTitle("Fluid Buoyancy Debug");
        window->SetPosition(math::Vec2(540.0f, 10.0f));
        window->SetSize(math::Vec2(550.0f, 990.0f));
        window->SetPreferredSize(math::Vec2(550.0f, 990.0f));

        const char* lines[] = {
            "Fluid Buoyancy Debug", "Box : not found", "Sphere : not found",
            "Coupling : not registered", "Static Coupling : --", "Rigid Coupling : --",
            "Particle Radius : --", "Restitution : --", "Drag : --",
            "Pressure Reaction : --", "Buoyancy : --",
            "Static Statistics : --", "Rigid Statistics : --", "Impulse : --",
            "Applied : --", "Displaced Mass : --", "Measurement : --",
            "Fixed Time : --", "Contacts : --", "Impulse Total : --",
            "Applied Count : --", "Displaced Mass Total : --"
        };
        for (std::size_t i = 0u; i < 22u; ++i)
        {
            m_Labels.push_back(AddLabel(*window, 32.0f + static_cast<float>(i) * 25.0f, lines[i]));
        }
        AddButton(*window, 592.0f, 10.0f, 100.0f, "Water",
            [this]() { ApplyPreset(ph::FluidCouplingPreset::Water); });
        AddButton(*window, 592.0f, 120.0f, 110.0f, "Heavy Fluid",
            [this]() { ApplyPreset(ph::FluidCouplingPreset::HeavyFluid); });
        AddButton(*window, 592.0f, 240.0f, 100.0f, "High Drag",
            [this]() { ApplyPreset(ph::FluidCouplingPreset::HighDrag); });
        AddButton(*window, 592.0f, 350.0f, 100.0f, "Off",
            [this]() { ApplyPreset(ph::FluidCouplingPreset::CouplingOff); });
        AddButton(*window, 630.0f, 10.0f, 155.0f, "Static On/Off",
            [this]() { ToggleStatic(); });
        AddButton(*window, 630.0f, 175.0f, 155.0f, "Rigid On/Off",
            [this]() { ToggleRigid(); });
        AddButton(*window, 630.0f, 340.0f, 155.0f, "Pause/Start",
            [this]() { ToggleMeasurement(); });
        AddButton(*window, 674.0f, 10.0f, 155.0f, "Reset Bodies",
            [this]() { ResetTestBodies(); ResetMeasurement(); });
        AddButton(*window, 674.0f, 175.0f, 155.0f, "Reset Measure",
            [this]() { ResetMeasurement(); });
        AddButton(*window, 674.0f, 340.0f, 155.0f, "Reset Water",
            [this]() { ApplyPreset(ph::FluidCouplingPreset::Water); });
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
            m_SliderLabels.push_back(AddLabel(*window, y, coefficientNames[i]));
            auto slider = CreateScope<UISlider>();
            slider->SetPosition(math::Vec2(280.0f, y + 2.0f));
            slider->SetSize(math::Vec2(240.0f, 22.0f));
            slider->SetRange(minimum[i], maximum[i]);
            slider->SetKeyboardStep(i == 0u ? 0.005f : 0.01f);
            slider->SetFocusable(true);
            slider->SetOnValueChanged([this, i](float value) { SetCoefficient(i, value); });
            m_Sliders.push_back(static_cast<UISlider*>(window->AddChild(std::move(slider))));
        }
        m_Window = m_Application.GetUIContext().GetRootElement().AddChild(std::move(window));
    }

    UILabel* AddLabel(UIWindow& window, float y, const std::string& text)
    {
        auto label = CreateScope<UILabel>();
        label->SetFont(m_Application.GetRuntimeUIFont());
        label->SetText(text);
        label->SetPosition(math::Vec2(12.0f, y));
        label->SetSize(math::Vec2(526.0f, 24.0f));
        label->SetHitTestVisible(false);
        return static_cast<UILabel*>(window.AddChild(std::move(label)));
    }

    void AddButton(UIWindow& window, float y, float x, float width,
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

    ph::FluidCouplingBinding* GetFirstBinding()
    {
        ph::FluidWorld* fluid = GetFluidWorld();
        if (fluid == nullptr) { return nullptr; }
        const auto& bindings = fluid->GetCouplingBindings();
        if (bindings.empty() == true) { return nullptr; }
        return bindings.front();
    }

    void SetCoefficient(std::size_t index, float value)
    {
        if (m_SynchronizingSliders == true) { return; }
        ph::FluidCouplingBinding* binding = GetFirstBinding();
        if (binding == nullptr) { return; }
        // StaticとRigidの粒子半径・反発係数は同じ表面を扱うため同期します。
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
        ph::FluidCouplingBinding* binding = GetFirstBinding();
        if (binding == nullptr) { return; }
        ph::ApplyFluidCouplingPreset(*binding, preset);
        // 比較条件を揃えるためPreset変更後はBodyと計測値を同時にResetします。
        ResetTestBodies();
        ResetMeasurement();
    }

    void ToggleStatic()
    {
        ph::FluidCouplingBinding* binding = GetFirstBinding();
        if (binding != nullptr)
        {
            binding->StaticColliderCouplingEnabled = binding->StaticColliderCouplingEnabled == false;
        }
    }

    void ToggleRigid()
    {
        ph::FluidCouplingBinding* binding = GetFirstBinding();
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
        SetLine(3u, "Coupling Bindings : %llu", static_cast<unsigned long long>(bindings.size()));
        ph::FluidCouplingBinding* binding = GetFirstBinding();
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
    std::vector<UILabel*> m_Labels;
    std::vector<UILabel*> m_SliderLabels;
    std::vector<UISlider*> m_Sliders;
    bool m_SynchronizingSliders = false;
};

} // namespace Raven
