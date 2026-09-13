#include "Raven/Physics/Thermal/ThermalWorld.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{
namespace
{
constexpr float MinimumThermalValue = 1.0e-8f;

float CalculateEffectiveConductivity(const ThermalBody& bodyA, const ThermalBody& bodyB)
{
    const float conductivityA = bodyA.Material.ThermalConductivity;
    const float conductivityB = bodyB.Material.ThermalConductivity;
    if (conductivityA <= MinimumThermalValue || conductivityB <= MinimumThermalValue)
    {
        return 0.0f;
    }

    return (2.0f * conductivityA * conductivityB) / (conductivityA + conductivityB);
}
}

bool ThermalWorld::RegisterBody(ThermalBody& body)
{
    if (ContainsBody(body) == true)
    {
        return false;
    }
    m_Bodies.push_back(&body);
    return true;
}

bool ThermalWorld::UnregisterBody(ThermalBody& body)
{
    const auto iterator = std::find(m_Bodies.begin(), m_Bodies.end(), &body);
    if (iterator == m_Bodies.end())
    {
        return false;
    }

    m_Bodies.erase(iterator);
    m_Contacts.erase(
        std::remove_if(m_Contacts.begin(), m_Contacts.end(), [&body](const ThermalContact& contact)
        {
            return contact.BodyA == &body || contact.BodyB == &body;
        }),
        m_Contacts.end());
    m_EnvironmentContacts.erase(
        std::remove_if(m_EnvironmentContacts.begin(), m_EnvironmentContacts.end(), [&body](const ThermalEnvironmentContact& contact)
        {
            return contact.Body == &body;
        }),
        m_EnvironmentContacts.end());
    return true;
}

bool ThermalWorld::RegisterContact(const ThermalContact& contact)
{
    if (contact.BodyA == nullptr || contact.BodyB == nullptr || contact.BodyA == contact.BodyB)
    {
        return false;
    }
    if (ContainsBody(*contact.BodyA) == false || ContainsBody(*contact.BodyB) == false)
    {
        return false;
    }

    ThermalContact normalizedContact = contact;
    if (normalizedContact.ThermalConductance <= 0.0f)
    {
        normalizedContact.ThermalConductance = CalculateConductance(
            *normalizedContact.BodyA, *normalizedContact.BodyB,
            normalizedContact.ContactArea, normalizedContact.ConductionDistance,
            normalizedContact.ConductivityScale);
    }
    if (normalizedContact.ThermalConductance <= 0.0f)
    {
        return false;
    }

    m_Contacts.push_back(normalizedContact);
    return true;
}

bool ThermalWorld::RegisterEnvironmentContact(const ThermalEnvironmentContact& contact)
{
    if (contact.Body == nullptr || ContainsBody(*contact.Body) == false || contact.AmbientTemperature < 0.0f)
    {
        return false;
    }

    ThermalEnvironmentContact normalizedContact = contact;
    if (normalizedContact.ThermalConductance <= 0.0f)
    {
        normalizedContact.ThermalConductance = CalculateConvectionConductance(
            normalizedContact.HeatTransferCoefficient,
            normalizedContact.SurfaceArea);
    }
    if (normalizedContact.ThermalConductance <= 0.0f)
    {
        return false;
    }

    m_EnvironmentContacts.push_back(normalizedContact);
    return true;
}

void ThermalWorld::ClearContacts()
{
    m_Contacts.clear();
    m_EnvironmentContacts.clear();
}

void ThermalWorld::Clear()
{
    ClearContacts();
    m_Bodies.clear();
    m_LastSubstepCount = 0u;
}

void ThermalWorld::SetSubstepSafetyFactor(float safetyFactor)
{
    if (safetyFactor <= MinimumThermalValue)
    {
        return;
    }
    m_SubstepSafetyFactor = safetyFactor;
}

void ThermalWorld::SetMaximumSubsteps(std::size_t maximumSubsteps)
{
    if (maximumSubsteps == 0u)
    {
        return;
    }
    m_MaximumSubsteps = maximumSubsteps;
}

float ThermalWorld::CalculateConductance(
    const ThermalBody& bodyA, const ThermalBody& bodyB,
    float contactArea, float conductionDistance, float conductivityScale)
{
    if (contactArea <= 0.0f || conductionDistance <= MinimumThermalValue || conductivityScale <= 0.0f)
    {
        return 0.0f;
    }
    const float effectiveConductivity = CalculateEffectiveConductivity(bodyA, bodyB);
    if (effectiveConductivity <= 0.0f)
    {
        return 0.0f;
    }
    return effectiveConductivity * conductivityScale * contactArea / conductionDistance;
}

float ThermalWorld::CalculateConvectionConductance(float heatTransferCoefficient, float surfaceArea)
{
    if (heatTransferCoefficient <= 0.0f || surfaceArea <= 0.0f)
    {
        return 0.0f;
    }
    return heatTransferCoefficient * surfaceArea;
}

void ThermalWorld::Step(float fixedDeltaTime)
{
    m_LastSubstepCount = 0u;
    if (fixedDeltaTime <= 0.0f || m_Bodies.empty() == true
        || (m_Contacts.empty() == true && m_EnvironmentContacts.empty() == true))
    {
        return;
    }

    std::vector<float> conductanceSums(m_Bodies.size(), 0.0f);
    for (const ThermalContact& contact : m_Contacts)
    {
        if (contact.BodyA == nullptr || contact.BodyB == nullptr || contact.ThermalConductance <= 0.0f)
        {
            continue;
        }
        const auto bodyAIterator = std::find(m_Bodies.begin(), m_Bodies.end(), contact.BodyA);
        const auto bodyBIterator = std::find(m_Bodies.begin(), m_Bodies.end(), contact.BodyB);
        if (bodyAIterator == m_Bodies.end() || bodyBIterator == m_Bodies.end())
        {
            continue;
        }
        conductanceSums[static_cast<std::size_t>(bodyAIterator - m_Bodies.begin())] += contact.ThermalConductance;
        conductanceSums[static_cast<std::size_t>(bodyBIterator - m_Bodies.begin())] += contact.ThermalConductance;
    }

    // 環境ReservoirもBodyから見ればGで接続された1本の熱経路なので、同じ時定数判定へ含めます。
    for (const ThermalEnvironmentContact& contact : m_EnvironmentContacts)
    {
        if (contact.Body == nullptr || contact.ThermalConductance <= 0.0f)
        {
            continue;
        }
        const auto bodyIterator = std::find(m_Bodies.begin(), m_Bodies.end(), contact.Body);
        if (bodyIterator != m_Bodies.end())
        {
            conductanceSums[static_cast<std::size_t>(bodyIterator - m_Bodies.begin())] += contact.ThermalConductance;
        }
    }

    float stableSubstepTime = fixedDeltaTime;
    for (std::size_t bodyIndex = 0; bodyIndex < m_Bodies.size(); ++bodyIndex)
    {
        const ThermalBody* body = m_Bodies[bodyIndex];
        if (body == nullptr || conductanceSums[bodyIndex] <= MinimumThermalValue)
        {
            continue;
        }
        const float heatCapacity = body->GetHeatCapacity();
        if (heatCapacity <= MinimumThermalValue)
        {
            continue;
        }
        stableSubstepTime = std::min(
            stableSubstepTime,
            m_SubstepSafetyFactor * heatCapacity / conductanceSums[bodyIndex]);
    }

    std::size_t substepCount = 1u;
    if (stableSubstepTime > MinimumThermalValue && stableSubstepTime < fixedDeltaTime)
    {
        substepCount = static_cast<std::size_t>(std::ceil(fixedDeltaTime / stableSubstepTime));
        substepCount = std::min(substepCount, m_MaximumSubsteps);
    }
    m_LastSubstepCount = substepCount;

    const float substepDeltaTime = fixedDeltaTime / static_cast<float>(substepCount);
    std::vector<float> heatDeltas(m_Bodies.size(), 0.0f);
    for (std::size_t substepIndex = 0; substepIndex < substepCount; ++substepIndex)
    {
        std::fill(heatDeltas.begin(), heatDeltas.end(), 0.0f);

        for (const ThermalContact& contact : m_Contacts)
        {
            if (contact.BodyA == nullptr || contact.BodyB == nullptr || contact.ThermalConductance <= 0.0f)
            {
                continue;
            }
            const auto bodyAIterator = std::find(m_Bodies.begin(), m_Bodies.end(), contact.BodyA);
            const auto bodyBIterator = std::find(m_Bodies.begin(), m_Bodies.end(), contact.BodyB);
            if (bodyAIterator == m_Bodies.end() || bodyBIterator == m_Bodies.end())
            {
                continue;
            }

            const float heatCapacityA = contact.BodyA->GetHeatCapacity();
            const float heatCapacityB = contact.BodyB->GetHeatCapacity();
            if (heatCapacityA <= MinimumThermalValue || heatCapacityB <= MinimumThermalValue)
            {
                continue;
            }

            float transferredHeat = contact.ThermalConductance
                * (contact.BodyB->Temperature - contact.BodyA->Temperature) * substepDeltaTime;
            const float equilibriumTemperature =
                (heatCapacityA * contact.BodyA->Temperature + heatCapacityB * contact.BodyB->Temperature)
                / (heatCapacityA + heatCapacityB);
            const float heatToEquilibrium = heatCapacityA * (equilibriumTemperature - contact.BodyA->Temperature);
            if (heatToEquilibrium >= 0.0f)
            {
                transferredHeat = std::min(transferredHeat, heatToEquilibrium);
            }
            else
            {
                transferredHeat = std::max(transferredHeat, heatToEquilibrium);
            }

            heatDeltas[static_cast<std::size_t>(bodyAIterator - m_Bodies.begin())] += transferredHeat;
            heatDeltas[static_cast<std::size_t>(bodyBIterator - m_Bodies.begin())] -= transferredHeat;
        }

        for (const ThermalEnvironmentContact& contact : m_EnvironmentContacts)
        {
            if (contact.Body == nullptr || contact.ThermalConductance <= 0.0f)
            {
                continue;
            }
            const auto bodyIterator = std::find(m_Bodies.begin(), m_Bodies.end(), contact.Body);
            if (bodyIterator == m_Bodies.end())
            {
                continue;
            }

            const float heatCapacity = contact.Body->GetHeatCapacity();
            if (heatCapacity <= MinimumThermalValue)
            {
                continue;
            }

            float transferredHeat = contact.ThermalConductance
                * (contact.AmbientTemperature - contact.Body->Temperature) * substepDeltaTime;

            // Environmentは無限熱容量Reservoirなので平衡温度はAmbientTemperatureです。
            // Substep上限へ達した場合にも環境温度を飛び越えないようClampします。
            const float heatToAmbient = heatCapacity * (contact.AmbientTemperature - contact.Body->Temperature);
            if (heatToAmbient >= 0.0f)
            {
                transferredHeat = std::min(transferredHeat, heatToAmbient);
            }
            else
            {
                transferredHeat = std::max(transferredHeat, heatToAmbient);
            }
            heatDeltas[static_cast<std::size_t>(bodyIterator - m_Bodies.begin())] += transferredHeat;
        }

        for (std::size_t bodyIndex = 0; bodyIndex < m_Bodies.size(); ++bodyIndex)
        {
            ThermalBody* body = m_Bodies[bodyIndex];
            if (body == nullptr)
            {
                continue;
            }
            const float heatCapacity = body->GetHeatCapacity();
            if (heatCapacity <= MinimumThermalValue)
            {
                continue;
            }
            body->Temperature += heatDeltas[bodyIndex] / heatCapacity;
            body->Temperature = std::max(body->Temperature, 0.0f);
        }
    }
}

bool ThermalWorld::ContainsBody(const ThermalBody& body) const
{
    return std::find(m_Bodies.begin(), m_Bodies.end(), &body) != m_Bodies.end();
}

}
