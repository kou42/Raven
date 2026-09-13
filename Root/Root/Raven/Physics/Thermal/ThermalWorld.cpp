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

    // 2材料が同じ有効距離を分担する簡易直列熱抵抗モデルとして調和平均を使用します。
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
        std::remove_if(
            m_Contacts.begin(),
            m_Contacts.end(),
            [&body](const ThermalContact& contact)
            {
                return contact.BodyA == &body || contact.BodyB == &body;
            }),
        m_Contacts.end());

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
        // 旧来のA/d入力も登録境界で一度だけGへ変換します。
        // Step()は正規化済みThermalConductanceだけを見るため、Solverを形状モデルから分離できます。
        normalizedContact.ThermalConductance = CalculateConductance(
            *normalizedContact.BodyA,
            *normalizedContact.BodyB,
            normalizedContact.ContactArea,
            normalizedContact.ConductionDistance,
            normalizedContact.ConductivityScale);
    }

    if (normalizedContact.ThermalConductance <= 0.0f)
    {
        return false;
    }

    m_Contacts.push_back(normalizedContact);
    return true;
}

void ThermalWorld::ClearContacts()
{
    m_Contacts.clear();
}

void ThermalWorld::Clear()
{
    m_Contacts.clear();
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
    const ThermalBody& bodyA,
    const ThermalBody& bodyB,
    float contactArea,
    float conductionDistance,
    float conductivityScale)
{
    if (contactArea <= 0.0f
        || conductionDistance <= MinimumThermalValue
        || conductivityScale <= 0.0f)
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

void ThermalWorld::Step(float fixedDeltaTime)
{
    m_LastSubstepCount = 0u;
    if (fixedDeltaTime <= 0.0f || m_Contacts.empty() == true || m_Bodies.empty() == true)
    {
        return;
    }

    // 各Bodyへ接続されたGの総和を求めます。集中熱容量C[J/K]に対して
    // tau = C / sum(G) [s] が、そのBodyの温度が変化する代表時定数になります。
    // Explicit Eulerをこの時定数より十分細かく刻むことで、多接触でも一括更新の
    // Contact順序非依存性を保ったまま振動・発散を抑えます。
    std::vector<float> conductanceSums(m_Bodies.size(), 0.0f);

    for (const ThermalContact& contact : m_Contacts)
    {
        if (contact.BodyA == nullptr
            || contact.BodyB == nullptr
            || contact.ThermalConductance <= 0.0f)
        {
            continue;
        }

        const auto bodyAIterator = std::find(m_Bodies.begin(), m_Bodies.end(), contact.BodyA);
        const auto bodyBIterator = std::find(m_Bodies.begin(), m_Bodies.end(), contact.BodyB);
        if (bodyAIterator == m_Bodies.end() || bodyBIterator == m_Bodies.end())
        {
            continue;
        }

        const std::size_t bodyAIndex = static_cast<std::size_t>(bodyAIterator - m_Bodies.begin());
        const std::size_t bodyBIndex = static_cast<std::size_t>(bodyBIterator - m_Bodies.begin());
        conductanceSums[bodyAIndex] += contact.ThermalConductance;
        conductanceSums[bodyBIndex] += contact.ThermalConductance;
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

        const float thermalTimeConstant = heatCapacity / conductanceSums[bodyIndex];
        stableSubstepTime = std::min(
            stableSubstepTime,
            m_SubstepSafetyFactor * thermalTimeConstant);
    }

    std::size_t substepCount = 1u;
    if (stableSubstepTime > MinimumThermalValue && stableSubstepTime < fixedDeltaTime)
    {
        substepCount = static_cast<std::size_t>(
            std::ceil(fixedDeltaTime / stableSubstepTime));
        substepCount = std::min(substepCount, m_MaximumSubsteps);
    }

    m_LastSubstepCount = substepCount;
    const float substepDeltaTime = fixedDeltaTime / static_cast<float>(substepCount);
    std::vector<float> heatDeltas(m_Bodies.size(), 0.0f);

    for (std::size_t substepIndex = 0; substepIndex < substepCount; ++substepIndex)
    {
        std::fill(heatDeltas.begin(), heatDeltas.end(), 0.0f);

        // 各substepでも全Contactを開始時温度から評価して最後に一括反映します。
        // したがってContactの登録順を変えても同じ温度状態から同じ熱量を計算できます。
        for (const ThermalContact& contact : m_Contacts)
        {
            if (contact.BodyA == nullptr
                || contact.BodyB == nullptr
                || contact.ThermalConductance <= 0.0f)
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

            const float temperatureDifference = contact.BodyB->Temperature - contact.BodyA->Temperature;
            float transferredHeat = contact.ThermalConductance * temperatureDifference * substepDeltaTime;

            // SafetyFactorでNetwork全体を安定化していますが、MaximumSubsteps上限へ到達した場合にも
            // 単一Edgeが平衡点を大きく飛び越えないよう、局所Clampを安全網として残します。
            const float equilibriumTemperature =
                (heatCapacityA * contact.BodyA->Temperature + heatCapacityB * contact.BodyB->Temperature)
                / (heatCapacityA + heatCapacityB);
            const float heatToEquilibrium =
                heatCapacityA * (equilibriumTemperature - contact.BodyA->Temperature);

            if (heatToEquilibrium >= 0.0f)
            {
                transferredHeat = std::min(transferredHeat, heatToEquilibrium);
            }
            else
            {
                transferredHeat = std::max(transferredHeat, heatToEquilibrium);
            }

            const std::size_t bodyAIndex = static_cast<std::size_t>(bodyAIterator - m_Bodies.begin());
            const std::size_t bodyBIndex = static_cast<std::size_t>(bodyBIterator - m_Bodies.begin());
            heatDeltas[bodyAIndex] += transferredHeat;
            heatDeltas[bodyBIndex] -= transferredHeat;
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
