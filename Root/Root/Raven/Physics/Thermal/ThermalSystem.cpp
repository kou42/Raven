#include "Raven/Physics/Thermal/ThermalSystem.h"

#include <algorithm>
#include <cmath>

#include "Raven/Physics/Contact.h"
#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Physics/Thermal/ThermalComponents.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
// 明示ThermalContactとRigid Contact由来の自動接触を同じPairへ二重登録しないための確認です。
// 明示設定を優先することで、ユーザーが指定した接触面積・伝導距離をRigid近似で上書きしません。
bool HasRegisteredThermalPair(const ThermalWorld& thermalWorld, const ThermalBody& bodyA, const ThermalBody& bodyB)
{
    for (const ThermalContact& contact : thermalWorld.GetContacts())
    {
        const bool sameOrder = contact.BodyA == &bodyA && contact.BodyB == &bodyB;
        const bool reverseOrder = contact.BodyA == &bodyB && contact.BodyB == &bodyA;
        if (sameOrder == true || reverseOrder == true)
        {
            return true;
        }
    }
    return false;
}
}

void ThermalSystem::SynchronizeWorld(Scene& scene)
{
    ThermalWorld& thermalWorld = scene.GetPhysicsSimulationWorld().GetThermalWorld();

    // ComponentStorageはdense vectorなので、ECSの追加・削除でComponent addressが変わる可能性があります。
    // そのためThermalWorldの非所有pointerをframe間で信頼せず、各Fixed Stepで必ず再構築します。
    // ECSを唯一の正規データにすることで、Entity破棄後のdangling pointerも持ち越しません。
    thermalWorld.Clear();

    TemperatureFieldRegistry& fieldRegistry = thermalWorld.GetTemperatureFieldRegistry();
    fieldRegistry.ClearTransientFields();

    // Temperature VolumeもECS storage内のField addressを永続保持しません。
    // 同期時にTransform位置をCenterへ反映してTransient登録し、Entity移動・破棄・storage再配置へ追従します。
    for (auto [entity, volumeComponent] : scene.View<SphericalTemperatureVolumeComponent>())
    {
        if (volumeComponent.Enabled == false)
        {
            continue;
        }
        const TransformComponent* transformComponent = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
        if (transformComponent == nullptr)
        {
            continue;
        }
        volumeComponent.Field.SetCenter(transformComponent->Position);
        fieldRegistry.RegisterTransientField(volumeComponent.Field);
    }
    for (auto [entity, volumeComponent] : scene.View<BoxTemperatureVolumeComponent>())
    {
        if (volumeComponent.Enabled == false)
        {
            continue;
        }
        const TransformComponent* transformComponent = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
        if (transformComponent == nullptr)
        {
            continue;
        }
        volumeComponent.Field.SetCenter(transformComponent->Position);
        fieldRegistry.RegisterTransientField(volumeComponent.Field);
    }

    // 先に全Bodyを登録します。Contact登録時は両端BodyがWorldへ存在することを検証するため、
    // Body -> 境界/Contactの順序を固定しています。
    for (auto [entity, thermalBodyComponent] : scene.View<ThermalBodyComponent>())
    {
        if (thermalBodyComponent.Enabled == false)
        {
            continue;
        }
        thermalWorld.RegisterBody(thermalBodyComponent.Body);
    }

    // ECS上の明示的なBody間伝導リンクをRuntime ThermalContactへ変換します。
    // TargetEntityはgenerationを含むHandleで生存確認し、破棄済みEntityへの参照を登録しません。
    for (auto [entity, thermalContactComponent] : scene.View<ThermalContactComponent>())
    {
        if (thermalContactComponent.Enabled == false
            || thermalContactComponent.TargetEntity.IsValid() == false
            || scene.IsEntityAlive(thermalContactComponent.TargetEntity) == false)
        {
            continue;
        }
        ThermalBodyComponent* sourceBodyComponent = scene.TryGetComponent<ThermalBodyComponent>(entity.GetIndex());
        ThermalBodyComponent* targetBodyComponent = scene.TryGetComponent<ThermalBodyComponent>(thermalContactComponent.TargetEntity.m_Index);
        if (sourceBodyComponent == nullptr || targetBodyComponent == nullptr
            || sourceBodyComponent->Enabled == false || targetBodyComponent->Enabled == false)
        {
            continue;
        }

        // Solverへ形状依存式を持ち込まないため、ECS境界で k_eff*A/d をG [W/K]へ正規化します。
        ThermalContact contact{};
        contact.BodyA = &sourceBodyComponent->Body;
        contact.BodyB = &targetBodyComponent->Body;
        contact.ContactArea = thermalContactComponent.ContactArea;
        contact.ConductionDistance = thermalContactComponent.ConductionDistance;
        contact.ConductivityScale = thermalContactComponent.ConductivityScale;
        contact.ThermalConductance = ThermalWorld::CalculateConductance(
            sourceBodyComponent->Body, targetBodyComponent->Body,
            contact.ContactArea, contact.ConductionDistance, contact.ConductivityScale);
        thermalWorld.RegisterContact(contact);
    }

    // 対流もhと面積をECS側の入力として保持し、RuntimeではG=h*Aへ変換します。
    // Environmentは無限Reservoirなので、ThermalBodyをもう1つ生成して熱容量を持たせる必要はありません。
    // TemperatureFieldは空間側の環境境界なので、各Entityのworld-space Transform位置で評価します。
    // Field未登録時はRegistryがComponentのAmbientTemperatureへfallbackするため、既存Sceneの挙動は変わりません。
    for (auto [entity, convectionComponent] : scene.View<ThermalConvectionComponent>())
    {
        if (convectionComponent.Enabled == false)
        {
            continue;
        }
        ThermalBodyComponent* bodyComponent = scene.TryGetComponent<ThermalBodyComponent>(entity.GetIndex());
        const TransformComponent* transformComponent = scene.TryGetComponent<TransformComponent>(entity.GetIndex());
        if (bodyComponent == nullptr || bodyComponent->Enabled == false || transformComponent == nullptr)
        {
            continue;
        }

        ThermalEnvironmentContact environmentContact{};
        environmentContact.Body = &bodyComponent->Body;
        environmentContact.AmbientTemperature = fieldRegistry.Evaluate(transformComponent->Position, convectionComponent.AmbientTemperature);
        environmentContact.HeatTransferCoefficient = convectionComponent.HeatTransferCoefficient;
        environmentContact.SurfaceArea = convectionComponent.SurfaceArea;
        environmentContact.ThermalConductance = ThermalWorld::CalculateConvectionConductance(
            environmentContact.HeatTransferCoefficient, environmentContact.SurfaceArea);
        thermalWorld.RegisterEnvironmentContact(environmentContact);
    }

    // 放射境界もECSにはRuntime pointerを保持せず、Fixed StepごとにThermalBodyへ解決します。
    // 対流とは異なりT^4非線形なので、World側で各substepの現在温度から熱流を再評価します。
    for (auto [entity, radiationComponent] : scene.View<ThermalRadiationComponent>())
    {
        if (radiationComponent.Enabled == false)
        {
            continue;
        }
        ThermalBodyComponent* bodyComponent = scene.TryGetComponent<ThermalBodyComponent>(entity.GetIndex());
        if (bodyComponent == nullptr || bodyComponent->Enabled == false)
        {
            continue;
        }
        ThermalRadiationContact radiationContact{};
        radiationContact.Body = &bodyComponent->Body;
        radiationContact.EnvironmentTemperature = radiationComponent.EnvironmentTemperature;
        radiationContact.Emissivity = radiationComponent.Emissivity;
        radiationContact.SurfaceArea = radiationComponent.SurfaceArea;
        thermalWorld.RegisterRadiationContact(radiationContact);
    }
}

void ThermalSystem::AppendRigidBodyContacts(Scene& scene, const std::vector<ContactManifold>& manifolds)
{
    ThermalWorld& thermalWorld = scene.GetPhysicsSimulationWorld().GetThermalWorld();

    // PhysicsWorldが「このFixed Step」で確定したManifoldだけを熱接触へ変換します。
    // これにより、離れたBody間へ前Stepの熱接触が残ることを避け、機械接触と熱接触を同期させます。
    for (const ContactManifold& manifold : manifolds)
    {
        // Triggerは物理的な接触面を表さないため伝導対象にせず、接触点なしのManifoldも除外します。
        if (manifold.IsTrigger == true || manifold.PointCount == 0u)
        {
            continue;
        }
        const EntityHandle handleA = manifold.A.GetHandle();
        const EntityHandle handleB = manifold.B.GetHandle();
        if (scene.IsEntityAlive(handleA) == false || scene.IsEntityAlive(handleB) == false)
        {
            continue;
        }
        ThermalBodyComponent* bodyComponentA = scene.TryGetComponent<ThermalBodyComponent>(handleA.m_Index);
        ThermalBodyComponent* bodyComponentB = scene.TryGetComponent<ThermalBodyComponent>(handleB.m_Index);
        const ThermalRigidContactComponent* settingsA = scene.TryGetComponent<ThermalRigidContactComponent>(handleA.m_Index);
        const ThermalRigidContactComponent* settingsB = scene.TryGetComponent<ThermalRigidContactComponent>(handleB.m_Index);
        if (bodyComponentA == nullptr || bodyComponentB == nullptr || settingsA == nullptr || settingsB == nullptr
            || bodyComponentA->Enabled == false || bodyComponentB->Enabled == false
            || settingsA->Enabled == false || settingsB->Enabled == false)
        {
            continue;
        }

        // 明示ThermalContactが存在するPairはそちらを正とし、自動生成による二重熱伝導を防ぎます。
        if (HasRegisteredThermalPair(thermalWorld, bodyComponentA->Body, bodyComponentB->Body) == true)
        {
            continue;
        }
        if (settingsA->NominalContactAreaPerPoint <= 0.0f || settingsB->NominalContactAreaPerPoint <= 0.0f
            || settingsA->ConductionDistance <= 0.0f || settingsB->ConductionDistance <= 0.0f
            || settingsA->ConductivityScale < 0.0f || settingsB->ConductivityScale < 0.0f)
        {
            continue;
        }

        // 現在のContact Manifoldには厳密な接触面積がないため、Point数×代表面積で近似します。
        // Pair両側で設定値が異なる場合、面積は過大評価を避けるため小さい側、距離は平均、
        // Scaleは対称性を保ち片側だけに偏らないよう幾何平均を採用します。
        const float areaPerPoint = std::min(settingsA->NominalContactAreaPerPoint, settingsB->NominalContactAreaPerPoint);
        ThermalContact contact{};
        contact.BodyA = &bodyComponentA->Body;
        contact.BodyB = &bodyComponentB->Body;
        contact.ContactArea = areaPerPoint * static_cast<float>(manifold.PointCount);
        contact.ConductionDistance = 0.5f * (settingsA->ConductionDistance + settingsB->ConductionDistance);
        contact.ConductivityScale = std::sqrt(settingsA->ConductivityScale * settingsB->ConductivityScale);
        contact.ThermalConductance = ThermalWorld::CalculateConductance(
            bodyComponentA->Body, bodyComponentB->Body,
            contact.ContactArea, contact.ConductionDistance, contact.ConductivityScale);
        thermalWorld.RegisterContact(contact);
    }
}

} // namespace Raven::ph
