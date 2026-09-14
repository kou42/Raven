#pragma once

#include <algorithm>

#include <imgui.h>

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

// Fluid SPH Demoが生成するBox / Sphereの状態とCoupling診断値を表示し、
// 実行中のCoupling設定を比較検証するためのDebug HUDです。
class FluidBuoyancyDebugOverlayLayer final : public Layer
{
public:
    explicit FluidBuoyancyDebugOverlayLayer(Application& application)
        : m_Application(application)
    {
    }

    void OnAttach() override
    {
        ResetTestBodies();
    }

    void OnImGuiRender(float deltaTime) override
    {
        static_cast<void>(deltaTime);

        const bool resetKeyPressed = Input::IsKeyPressed(Key::R);
        const bool resetRequested = resetKeyPressed == true && m_WasResetKeyPressed == false;
        m_WasResetKeyPressed = resetKeyPressed;
        if (resetRequested == true)
        {
            ResetTestBodies();
        }

        ImGui::SetNextWindowBgAlpha(0.82f);
        if (ImGui::Begin("Fluid Buoyancy Debug") == true)
        {
            Scene* scene = m_Application.GetScene();
            if (scene == nullptr)
            {
                ImGui::TextUnformatted("Scene unavailable");
            }
            else
            {
                DrawTestBodyStates(*scene);
                ImGui::Separator();
                DrawCouplingControls(*scene);
                ImGui::Separator();
                DrawCouplingStatistics(*scene);
                ImGui::Separator();
                ImGui::TextUnformatted("R : Reset Box / Sphere");
                if (ImGui::Button("Reset Buoyancy Test Bodies") == true)
                {
                    ResetTestBodies();
                }
            }
        }
        ImGui::End();
    }

private:
    static void DrawTestBodyStates(Scene& scene)
    {
        bool boxFound = false;
        bool sphereFound = false;

        for (auto [entity, tag, transform, rigidBody]
            : scene.View<TagComponent, TransformComponent, RigidBodyComponent>())
        {
            if (tag.Tag == "Fluid Buoyancy Test Box")
            {
                boxFound = true;
                DrawBodyState("Box", transform, rigidBody);
            }
            else if (tag.Tag == "Fluid Buoyancy Test Sphere")
            {
                sphereFound = true;
                DrawBodyState("Sphere", transform, rigidBody);
            }
        }

        if (boxFound == false)
        {
            ImGui::TextUnformatted("Box : not found");
        }
        if (sphereFound == false)
        {
            ImGui::TextUnformatted("Sphere : not found");
        }
    }

    static void DrawBodyState(
        const char* label,
        const TransformComponent& transform,
        const RigidBodyComponent& rigidBody)
    {
        ImGui::Text("%s Pos : (%.2f, %.2f, %.2f)", label,
            transform.Position.x, transform.Position.y, transform.Position.z);
        ImGui::Text("%s Vel : (%.2f, %.2f, %.2f)", label,
            rigidBody.LinearVelocity.x, rigidBody.LinearVelocity.y, rigidBody.LinearVelocity.z);
    }

    static void DrawPresetButtons(ph::FluidCouplingBinding& binding)
    {
        ImGui::TextUnformatted("Presets");

        if (ImGui::Button("Water") == true)
        {
            ph::ApplyFluidCouplingPreset(binding, ph::FluidCouplingPreset::Water);
        }
        ImGui::SameLine();
        if (ImGui::Button("Heavy Fluid") == true)
        {
            ph::ApplyFluidCouplingPreset(binding, ph::FluidCouplingPreset::HeavyFluid);
        }
        ImGui::SameLine();
        if (ImGui::Button("High Drag") == true)
        {
            ph::ApplyFluidCouplingPreset(binding, ph::FluidCouplingPreset::HighDrag);
        }

        if (ImGui::Button("Coupling Off") == true)
        {
            ph::ApplyFluidCouplingPreset(binding, ph::FluidCouplingPreset::CouplingOff);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to Water") == true)
        {
            // WaterをDemoの基準値として扱い、手動調整後もワンクリックで比較条件へ戻せるようにします。
            ph::ApplyFluidCouplingPreset(binding, ph::FluidCouplingPreset::Water);
        }
    }

    static void DrawCouplingControls(Scene& scene)
    {
        ph::FluidWorld& fluidWorld = scene.GetPhysicsSimulationWorld().GetFluidWorld();
        const std::vector<ph::FluidCouplingBinding*>& bindings = fluidWorld.GetCouplingBindings();

        ImGui::TextUnformatted("Fluid Coupling Runtime Controls");
        if (bindings.empty() == true)
        {
            ImGui::TextUnformatted("Coupling Binding : not registered");
            return;
        }

        for (std::size_t bindingIndex = 0u; bindingIndex < bindings.size(); ++bindingIndex)
        {
            ph::FluidCouplingBinding* binding = bindings[bindingIndex];
            if (binding == nullptr)
            {
                continue;
            }

            ImGui::PushID(static_cast<int>(bindingIndex));
            ImGui::Text("Binding %llu", static_cast<unsigned long long>(bindingIndex));
            DrawPresetButtons(*binding);

            ImGui::Checkbox("Static Collider", &binding->StaticColliderCouplingEnabled);
            ImGui::Checkbox("Rigid Body", &binding->RigidBodyCouplingEnabled);

            float particleRadius = binding->RigidBodySettings.ParticleRadius;
            if (ImGui::DragFloat("Particle Radius", &particleRadius, 0.005f, 0.001f, 2.0f, "%.3f") == true)
            {
                particleRadius = std::max(particleRadius, 0.001f);
                binding->StaticColliderSettings.ParticleRadius = particleRadius;
                binding->RigidBodySettings.ParticleRadius = particleRadius;
            }

            float restitution = binding->RigidBodySettings.Restitution;
            if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f, "%.2f") == true)
            {
                binding->StaticColliderSettings.Restitution = restitution;
                binding->RigidBodySettings.Restitution = restitution;
            }

            ImGui::DragFloat("Drag", &binding->RigidBodySettings.DragCoefficient,
                0.01f, 0.0f, 5.0f, "%.2f");
            binding->RigidBodySettings.DragCoefficient =
                std::max(binding->RigidBodySettings.DragCoefficient, 0.0f);

            ImGui::DragFloat("Pressure Reaction", &binding->RigidBodySettings.PressureReactionCoefficient,
                0.05f, 0.0f, 10.0f, "%.2f");
            binding->RigidBodySettings.PressureReactionCoefficient =
                std::max(binding->RigidBodySettings.PressureReactionCoefficient, 0.0f);

            ImGui::DragFloat("Buoyancy", &binding->RigidBodySettings.BuoyancyCoefficient,
                0.05f, 0.0f, 10.0f, "%.2f");
            binding->RigidBodySettings.BuoyancyCoefficient =
                std::max(binding->RigidBodySettings.BuoyancyCoefficient, 0.0f);

            if (bindings.size() > 1u && bindingIndex + 1u < bindings.size())
            {
                ImGui::Separator();
            }
            ImGui::PopID();
        }
    }

    static void DrawCouplingStatistics(const Scene& scene)
    {
        const ph::FluidWorld& fluidWorld = scene.GetPhysicsSimulationWorld().GetFluidWorld();
        const ph::FluidStaticColliderCouplingStatistics& staticStatistics =
            fluidWorld.GetLastStaticColliderCouplingStatistics();
        const ph::FluidRigidBodyCouplingStatistics& rigidStatistics =
            fluidWorld.GetLastRigidBodyCouplingStatistics();

        ImGui::TextUnformatted("Fluid Coupling Statistics");
        ImGui::Text("Static : collider=%llu candidate=%llu resolved=%llu",
            static_cast<unsigned long long>(staticStatistics.SupportedColliderCount),
            static_cast<unsigned long long>(staticStatistics.CandidatePairCount),
            static_cast<unsigned long long>(staticStatistics.ResolvedContactCount));
        ImGui::Text("Rigid  : body=%llu candidate=%llu resolved=%llu",
            static_cast<unsigned long long>(rigidStatistics.DynamicBodyCount),
            static_cast<unsigned long long>(rigidStatistics.CandidatePairCount),
            static_cast<unsigned long long>(rigidStatistics.ResolvedContactCount));
        ImGui::Text("Impulse: normal=%.4f drag=%.4f",
            rigidStatistics.TotalNormalImpulse, rigidStatistics.TotalDragImpulse);
        ImGui::Text("         pressure=%.4f buoyancy=%.4f",
            rigidStatistics.TotalPressureImpulse, rigidStatistics.TotalBuoyancyImpulse);
        ImGui::Text("Applied: normal=%llu drag=%llu pressure=%llu buoyancy=%llu",
            static_cast<unsigned long long>(rigidStatistics.AppliedImpulseCount),
            static_cast<unsigned long long>(rigidStatistics.AppliedDragImpulseCount),
            static_cast<unsigned long long>(rigidStatistics.AppliedPressureImpulseCount),
            static_cast<unsigned long long>(rigidStatistics.AppliedBuoyancyImpulseCount));
        ImGui::Text("Displaced Fluid Mass : %.4f", rigidStatistics.TotalDisplacedFluidMass);
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

            physicsWorld.Teleport(*scene, entity, resetPosition);
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
};

} // namespace Raven
