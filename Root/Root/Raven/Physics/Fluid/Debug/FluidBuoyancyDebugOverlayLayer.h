#pragma once

#include <imgui.h>

#include "Raven/Core/Application.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Math/MathQuatanion.h"
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
// Fluid SPH Demoが生成するBox / SphereのWorld座標を表示し、同じ初期条件へ何度でも
// 戻せる検証専用HUDです。Fluid solverやCoupling本体へDebug入力依存を持ち込まず、
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
                bool boxFound = false;
                bool sphereFound = false;

                for (auto [entity, tag, transform, rigidBody]
                    : scene->View<TagComponent, TransformComponent, RigidBodyComponent>())
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
    static void DrawBodyState(
        const char* label,
        const TransformComponent& transform,
        const RigidBodyComponent& rigidBody)
    {
        ImGui::Text(
            "%s Pos : (%.2f, %.2f, %.2f)",
            label,
            transform.Position.x,
            transform.Position.y,
            transform.Position.z);
        ImGui::Text(
            "%s Vel : (%.2f, %.2f, %.2f)",
            label,
            rigidBody.LinearVelocity.x,
            rigidBody.LinearVelocity.y,
            rigidBody.LinearVelocity.z);
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
            // Teleport直後のBodyが確実に起床した状態で次の試行を開始できるようにします。
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
