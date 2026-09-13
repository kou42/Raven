#include "Raven/Physics/Thermal/ThermalSystem.h"

#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Physics/Thermal/ThermalComponents.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
void ThermalSystem::SynchronizeWorld(Scene& scene)
{
    ThermalWorld& thermalWorld = scene.GetPhysicsSimulationWorld().GetThermalWorld();

    // ComponentStorageはdense vectorのため、Component追加・削除で要素アドレスが変化し得ます。
    // ThermalWorldへpointerを長期保存せず、Fixed Step群の直前にRegistryを作り直すことで、
    // ECSのlifetimeを正規データとしてdangling pointerを次frameへ持ち越さないようにします。
    thermalWorld.Clear();

    for (auto [entity, thermalBodyComponent] : scene.View<ThermalBodyComponent>())
    {
        if (thermalBodyComponent.Enabled == false)
        {
            continue;
        }

        thermalWorld.RegisterBody(thermalBodyComponent.Body);
    }

    for (auto [entity, thermalContactComponent] : scene.View<ThermalContactComponent>())
    {
        if (thermalContactComponent.Enabled == false
            || thermalContactComponent.TargetEntity.IsValid() == false
            || scene.IsEntityAlive(thermalContactComponent.TargetEntity) == false)
        {
            continue;
        }

        ThermalBodyComponent* sourceBodyComponent =
            scene.TryGetComponent<ThermalBodyComponent>(entity.GetIndex());
        ThermalBodyComponent* targetBodyComponent =
            scene.TryGetComponent<ThermalBodyComponent>(thermalContactComponent.TargetEntity.m_Index);

        if (sourceBodyComponent == nullptr
            || targetBodyComponent == nullptr
            || sourceBodyComponent->Enabled == false
            || targetBodyComponent->Enabled == false)
        {
            continue;
        }

        ThermalContact contact{};
        contact.BodyA = &sourceBodyComponent->Body;
        contact.BodyB = &targetBodyComponent->Body;
        contact.ContactArea = thermalContactComponent.ContactArea;
        contact.ConductionDistance = thermalContactComponent.ConductionDistance;
        contact.ConductivityScale = thermalContactComponent.ConductivityScale;

        // ThermalWorld側でも数値範囲と登録済みBodyを検証します。
        // ECS System側はEntity lifetimeとComponent有効状態、Solver側は熱モデルの契約を担当します。
        thermalWorld.RegisterContact(contact);
    }
}

} // namespace Raven::ph
