// Raven/Physics/Fluid/Debug/FluidDemoCameraViewLayer.h
#pragma once

#include "Raven/Character/Debug/CharacterControllerDemoLayer.h"
#include "Raven/Core/Input.h"
#include "Raven/Core/KeyCodes.h"
#include "Raven/Core/MouseCodes.h"
#include "Raven/Math/MathVector.h"
#include "Raven/Renderer/Layer/Layer.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Entity.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneCameraSystem.h"

namespace Raven
{

// ============================================================================
// FluidDemoCameraViewLayer
// ============================================================================
// CharacterがFluidデモ確認位置へ入ったときだけ、Runtime Cameraを水槽全体が見える固定視点へ
// 上書きするScene-owned Debug Layerです。
//
// CharacterControllerDemoLayer自身へFluid固有の座標・視点を持ち込まず、Fluid側のDebug責務として
// Camera Transformだけを最終的に上書きします。Scene LayerはCharacter Layerより後へ登録するため、
// Characterの通常Orbit Camera更新後、Scene描画前にこの視点を適用できます。
//
// 右MouseでCameraを操作し始めた場合は固定視点を解除します。再度F Teleportするか、いったん
// Activation範囲外へ出て入り直すと固定視点が再び有効になります。
class FluidDemoCameraViewLayer final : public Layer
{
public:
    FluidDemoCameraViewLayer(
        Scene& scene,
        CharacterControllerDemoLayer& characterLayer,
        const math::Vec3& activationCenter,
        const math::Vec3& cameraPosition,
        const math::Vec3& cameraRotation,
        float activationRadius)
        : m_Scene(&scene)
        , m_CharacterLayer(&characterLayer)
        , m_ActivationCenter(activationCenter)
        , m_CameraPosition(cameraPosition)
        , m_CameraRotation(cameraRotation)
        , m_ActivationRadius(activationRadius)
    {
    }

    void OnDetach() override
    {
        m_CharacterLayer = nullptr;
        m_Scene = nullptr;
    }

    void OnUpdate(float deltaTime) override
    {
        static_cast<void>(deltaTime);

        if (m_Scene == nullptr || m_CharacterLayer == nullptr)
        {
            return;
        }

        const bool teleportKeyPressed = Input::IsKeyPressed(Key::F);
        const bool teleportRequested =
            teleportKeyPressed == true && m_WasTeleportKeyPressed == false;
        m_WasTeleportKeyPressed = teleportKeyPressed;

        // FはCharacterPositionDebugOverlayLayerが同Frame後半でTeleportを実行します。
        // ここでは次Frameの固定視点を再有効化する要求としてだけ扱い、Teleport処理を二重化しません。
        if (teleportRequested == true)
        {
            m_CameraOverrideSuppressed = false;
        }

        const math::Vec3 characterOffset =
            m_CharacterLayer->GetCharacterWorldPosition() - m_ActivationCenter;
        const float activationRadiusSquared = m_ActivationRadius * m_ActivationRadius;
        const bool isInsideActivationArea =
            characterOffset.LengthSq() <= activationRadiusSquared;

        if (isInsideActivationArea == false)
        {
            m_WasInsideActivationArea = false;
            return;
        }

        // Debug地点へ新しく入った場合は、Button Teleportも含めて固定Cameraを有効にします。
        // これにより入力経路ごとにCamera状態を別管理せず、Character位置だけを共通の判定基準にできます。
        if (m_WasInsideActivationArea == false)
        {
            m_CameraOverrideSuppressed = false;
        }
        m_WasInsideActivationArea = true;

        // 固定視点中でも右Mouseを押せば通常Orbit操作へ戻せるようにします。
        // Character側のOrbit CameraはこのLayerより先に更新済みなので、このFrameからその姿勢を採用できます。
        if (Input::IsMouseButtonPressed(Mouse::Right) == true)
        {
            m_CameraOverrideSuppressed = true;
        }

        if (m_CameraOverrideSuppressed == true)
        {
            return;
        }

        Entity cameraEntity = SceneCameraSystem::ResolveRuntimeCameraEntity(*m_Scene);
        if (static_cast<bool>(cameraEntity) == false
            || m_Scene->IsEntityAlive(cameraEntity) == false
            || cameraEntity.HasComponent<TransformComponent>() == false
            || cameraEntity.HasComponent<CameraComponent>() == false)
        {
            return;
        }

        // CameraのView MatrixはSceneCameraSystemがRender直前にTransformから再構築します。
        // そのためここではTransformだけを書き換え、Camera内部行列との二重管理を避けます。
        TransformComponent& cameraTransform = cameraEntity.GetComponent<TransformComponent>();
        cameraTransform.Position = m_CameraPosition;
        cameraTransform.Rotation = m_CameraRotation;
    }

private:
    Scene* m_Scene = nullptr;
    CharacterControllerDemoLayer* m_CharacterLayer = nullptr;

    math::Vec3 m_ActivationCenter{};
    math::Vec3 m_CameraPosition{};
    math::Vec3 m_CameraRotation{};
    float m_ActivationRadius = 2.0f;

    bool m_WasTeleportKeyPressed = false;
    bool m_WasInsideActivationArea = false;
    bool m_CameraOverrideSuppressed = false;
};

} // namespace Raven
