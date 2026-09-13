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

    // 2材料が直列に熱抵抗を持つ接触として扱うため、調和平均を使用します。
    // 単純な算術平均より低熱伝導側の影響を正しく強く反映できます。
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

    // Body参照を外した後もContactにdangling pointerを残さないよう、
    // 関連Contactも同時にRegistryから除去します。
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

    if (contact.ContactArea <= 0.0f
        || contact.ConductionDistance <= MinimumThermalValue
        || contact.ConductivityScale < 0.0f)
    {
        return false;
    }

    m_Contacts.push_back(contact);
    return true;
}

void ThermalWorld::ClearContacts()
{
    m_Contacts.clear();
}

void ThermalWorld::Clear()
{
    // ThermalWorldはBodyを所有しないため、参照だけを解除します。
    m_Contacts.clear();
    m_Bodies.clear();
}

void ThermalWorld::Step(float fixedDeltaTime)
{
    if (fixedDeltaTime <= 0.0f || m_Contacts.empty() == true)
    {
        return;
    }

    // Contactを順番に直接Temperatureへ反映すると、同じStep内で後続Contactが
    // 更新済み温度を参照し、Contact列挙順によって結果が変わります。
    // そこで全ContactをStep開始時温度から評価し、Bodyごとの熱量[J]を蓄積してから一括反映します。
    std::vector<float> heatDeltas(m_Bodies.size(), 0.0f);

    for (const ThermalContact& contact : m_Contacts)
    {
        if (contact.BodyA == nullptr || contact.BodyB == nullptr)
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

        const float effectiveConductivity = CalculateEffectiveConductivity(*contact.BodyA, *contact.BodyB);
        if (effectiveConductivity <= 0.0f || contact.ConductivityScale <= 0.0f)
        {
            continue;
        }

        const float thermalConductance =
            effectiveConductivity
            * contact.ConductivityScale
            * contact.ContactArea
            / contact.ConductionDistance;

        const float temperatureDifference = contact.BodyB->Temperature - contact.BodyA->Temperature;
        float transferredHeat = thermalConductance * temperatureDifference * fixedDeltaTime;

        // Explicit Eulerで1 Step中に平衡温度を飛び越えると、温度が振動・発散する可能性があります。
        // 2 Bodyだけが熱交換した場合の平衡温度から求めた最大移動熱量でClampし、
        // 少なくとも単一Contactについては1 Stepで熱流方向が反転しないようにします。
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

        // Kelvinは負値を取らないため、入力異常や将来の外部熱源実装から負温度が入っても
        // Thermal Domain内部では絶対零度を下回らないようにします。
        body->Temperature = std::max(body->Temperature, 0.0f);
    }
}

bool ThermalWorld::ContainsBody(const ThermalBody& body) const
{
    return std::find(m_Bodies.begin(), m_Bodies.end(), &body) != m_Bodies.end();
}

}
