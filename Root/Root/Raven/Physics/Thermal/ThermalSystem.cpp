#include "Raven/Physics/Thermal/ThermalSystem.h"

#include <algorithm>
#include <cmath>

#include "Raven/Physics/Contact.h"
#include "Raven/Physics/PhysicsSimulationWorld.h"
#include "Raven/Physics/Thermal/ThermalComponents.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
bool HasRegisteredThermalPair(
    const ThermalWorld& thermalWorld,
    const ThermalBody& bodyA,
    const ThermalBody& bodyB)
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
        contact.ThermalConductance = ThermalWorld::CalculateConductance(
            sourceBodyComponent->Body,
            targetBodyComponent->Body,
            thermalContactComponent.ContactArea,
            thermalContactComponent.ConductionDistance,
            thermalContactComponent.ConductivityScale);

        thermalWorld.RegisterContact(contact);
    }
}

void ThermalSystem::AppendRigidBodyContacts(
    Scene& scene,
    const std::vector<ContactManifold>& manifolds)
{
    ThermalWorld& thermalWorld = scene.GetPhysicsSimulationWorld().GetThermalWorld();

    for (const ContactManifold& manifold : manifolds)
    {
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

        ThermalBodyComponent* bodyComponentA =
            scene.TryGetComponent<ThermalBodyComponent>(handleA.m_Index);
        ThermalBodyComponent* bodyComponentB =
            scene.TryGetComponent<ThermalBodyComponent>(handleB.m_Index);
        const ThermalRigidContactComponent* settingsA =
            scene.TryGetComponent<ThermalRigidContactComponent>(handleA.m_Index);
        const ThermalRigidContactComponent* settingsB =
            scene.TryGetComponent<ThermalRigidContactComponent>(handleB.m_Index);

        if (bodyComponentA == nullptr
            || bodyComponentB == nullptr
            || settingsA == nullptr
            || settingsB == nullptr
            || bodyComponentA->Enabled == false
            || bodyComponentB->Enabled == false
            || settingsA->Enabled == false
            || settingsB->Enabled == false)
        {
            continue;
        }

        if (HasRegisteredThermalPair(
            thermalWorld,
            bodyComponentA->Body,
            bodyComponentB->Body) == true)
        {
            continue;
        }

        if (settingsA->NominalContactAreaPerPoint <= 0.0f
            || settingsB->NominalContactAreaPerPoint <= 0.0f
            || settingsA->ConductionDistance <= 0.0f
            || settingsB->ConductionDistance <= 0.0f
            || settingsA->ConductivityScale < 0.0f
            || settingsB->ConductivityScale < 0.0f)
        {
            continue;
        }

        const float areaPerPoint = std::min(
            settingsA->NominalContactAreaPerPoint,
            settingsB->NominalContactAreaPerPoint);
        const float contactArea = areaPerPoint * static_cast<float>(manifold.PointCount);
        const float conductionDistance = 0.5f
            * (settingsA->ConductionDistance + settingsB->ConductionDistance);
        const float conductivityScale = std::sqrt(
            settingsA->ConductivityScale * settingsB->ConductivityScale);

        ThermalContact contact{};
        contact.BodyA = &bodyComponentA->Body;
        contact.BodyB = &bodyComponentB->Body;
        contact.ThermalConductance = ThermalWorld::CalculateConductance(
            bodyComponentA->Body,
            bodyComponentB->Body,
            contactArea,
            conductionDistance,
            conductivityScale);

        thermalWorld.RegisterContact(contact);
    }
}

} // namespace Raven::ph
