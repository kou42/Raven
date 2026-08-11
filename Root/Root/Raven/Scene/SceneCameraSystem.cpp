#include "Raven/Scene/SceneCameraSystem.h"

#include "Raven/Math/MathUtility.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"
#include "Raven/Scene/SceneCamera.h"

namespace Raven
{
namespace
{
math::Mat4 BuildCameraViewMatrix(const TransformComponent& transform)
{
    // Cameraの位置・回転はTransformComponentを唯一の正規データとして扱います。
    // ScaleはCamera姿勢には意味を持たないため、View Matrix構築には使用しません。
    //
    // TransformComponent::GetTransform()と同じX -> Y -> Zの回転順でRotation Matrixを作り、
    // CameraのLocal Forward(-Z) / Local Up(+Y)をWorld空間へ変換します。
    math::Mat4 rotation = math::Mat4::Identity();
    rotation = math::Rotate(rotation, transform.Rotation.x, math::Vec3{ 1.0f, 0.0f, 0.0f });
    rotation = math::Rotate(rotation, transform.Rotation.y, math::Vec3{ 0.0f, 1.0f, 0.0f });
    rotation = math::Rotate(rotation, transform.Rotation.z, math::Vec3{ 0.0f, 0.0f, 1.0f });

    const math::Vec4 localForward{ 0.0f, 0.0f, -1.0f, 0.0f };
    const math::Vec4 localUp{ 0.0f, 1.0f, 0.0f, 0.0f };

    const math::Vec4 rotatedForward = rotation * localForward;
    const math::Vec4 rotatedUp = rotation * localUp;

    math::Vec3 forward{
        rotatedForward.x,
        rotatedForward.y,
        rotatedForward.z
    };
    math::Vec3 up{
        rotatedUp.x,
        rotatedUp.y,
        rotatedUp.z
    };

    forward.Normalize();
    up.Normalize();

    return math::Mat4::LookAt(
        transform.Position,
        transform.Position + forward,
        up);
}
} // namespace

Entity SceneCameraSystem::FindPrimaryCameraEntity(Scene& scene)
{
    // TransformとCameraComponentの両方を持つEntityだけを候補にします。
    // Cameraだけ存在してTransformが欠けているEntityはRuntime Cameraとして成立しないため除外します。
    for (auto [entity, transform, cameraComponent] : scene.View<TransformComponent, CameraComponent>())
    {
        static_cast<void>(transform);

        if (cameraComponent.Primary == false)
        {
            continue;
        }

        return entity;
    }

    return Entity{};
}

bool SceneCameraSystem::SetPrimaryCamera(Scene& scene, Entity target)
{
    // Primary切り替え途中で既存Primaryを先に解除すると、targetが無効だった場合に
    // SceneからPrimaryが失われます。そのため対象の妥当性をすべて確認してから変更します。
    if (static_cast<bool>(target) == false)
    {
        return false;
    }

    if (target.GetScene() != &scene)
    {
        return false;
    }

    if (scene.IsEntityAlive(target) == false)
    {
        return false;
    }

    if (target.HasComponent<TransformComponent>() == false
        || target.HasComponent<CameraComponent>() == false)
    {
        return false;
    }

    // PrimaryはScene内で排他的に扱います。
    // 将来Camera Entityが増えても、Editor/UI側で他Cameraを個別に探して解除する必要はありません。
    for (auto [entity, transform, cameraComponent] : scene.View<TransformComponent, CameraComponent>())
    {
        static_cast<void>(transform);
        cameraComponent.Primary = (entity == target);
    }

    return true;
}

Entity SceneCameraSystem::ResolveRuntimeCameraEntity(Scene& scene)
{
    // ========================================================================
    // 1. Explicit Primary Camera
    // ========================================================================
    // 通常経路ではPrimary=trueのCameraを最優先します。
    // Primaryが複数ある場合は既存ECSの反復順に従い最初の1件を採用します。
    Entity primaryCamera = FindPrimaryCameraEntity(scene);
    if (static_cast<bool>(primaryCamera))
    {
        return primaryCamera;
    }

    // ========================================================================
    // 2. Safe fallback
    // ========================================================================
    // EditorでPrimaryを切り替える途中や、Sceneロード直後にPrimary指定が欠けていても、
    // Camera Entity自体が存在するならGame Viewを維持します。
    //
    // ここでCameraComponent::Primaryを書き換えないことが重要です。
    // fallbackは「今回どのCameraを描画に使うか」だけを解決する責務とし、
    // Sceneデータそのものを暗黙変更しません。
    for (auto [entity, transform, cameraComponent] : scene.View<TransformComponent, CameraComponent>())
    {
        static_cast<void>(transform);
        static_cast<void>(cameraComponent);
        return entity;
    }

    return Entity{};
}

SceneCamera* SceneCameraSystem::UpdatePrimaryCamera(
    Scene& scene,
    float viewportWidth,
    float viewportHeight)
{
    Entity cameraEntity = ResolveRuntimeCameraEntity(scene);
    if (static_cast<bool>(cameraEntity) == false)
    {
        return nullptr;
    }

    TransformComponent& transform = cameraEntity.GetComponent<TransformComponent>();
    CameraComponent& cameraComponent = cameraEntity.GetComponent<CameraComponent>();

    // ViewportはCameraごとのProjection設定へ反映し、TransformからはViewだけを更新します。
    // SceneCamera内でProjectionとViewの責務を分離しているため、Entity移動時にProjectionを
    // 再計算する必要はありません。
    cameraComponent.Camera.SetViewportSize(viewportWidth, viewportHeight);
    cameraComponent.Camera.SetViewMatrix(BuildCameraViewMatrix(transform));

    return &cameraComponent.Camera;
}

} // namespace Raven
