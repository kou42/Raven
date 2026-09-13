#include "Raven/Physics/Thermal/ThermalWorld.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{
namespace
{
constexpr float MinimumThermalValue = 1.0e-8f;
constexpr float StefanBoltzmannConstant = 5.670374419e-8f;

float CalculateEffectiveConductivity(const ThermalBody& bodyA, const ThermalBody& bodyB)
{
    const float conductivityA = bodyA.Material.ThermalConductivity;
    const float conductivityB = bodyB.Material.ThermalConductivity;
    if (conductivityA <= MinimumThermalValue || conductivityB <= MinimumThermalValue) { return 0.0f; }
    return (2.0f * conductivityA * conductivityB) / (conductivityA + conductivityB);
}

float ClampHeatTowardTemperature(float heat, float heatCapacity, float currentTemperature, float targetTemperature)
{
    const float heatToTarget = heatCapacity * (targetTemperature - currentTemperature);
    if (heatToTarget >= 0.0f) { return std::min(heat, heatToTarget); }
    return std::max(heat, heatToTarget);
}
}

bool ThermalWorld::RegisterBody(ThermalBody& body)
{
    if (ContainsBody(body) == true) { return false; }
    m_Bodies.push_back(&body); return true;
}

bool ThermalWorld::UnregisterBody(ThermalBody& body)
{
    const auto iterator = std::find(m_Bodies.begin(), m_Bodies.end(), &body);
    if (iterator == m_Bodies.end()) { return false; }
    m_Bodies.erase(iterator);
    m_Contacts.erase(std::remove_if(m_Contacts.begin(), m_Contacts.end(), [&body](const ThermalContact& c) { return c.BodyA == &body || c.BodyB == &body; }), m_Contacts.end());
    m_EnvironmentContacts.erase(std::remove_if(m_EnvironmentContacts.begin(), m_EnvironmentContacts.end(), [&body](const ThermalEnvironmentContact& c) { return c.Body == &body; }), m_EnvironmentContacts.end());
    m_RadiationContacts.erase(std::remove_if(m_RadiationContacts.begin(), m_RadiationContacts.end(), [&body](const ThermalRadiationContact& c) { return c.Body == &body; }), m_RadiationContacts.end());
    return true;
}

bool ThermalWorld::RegisterContact(const ThermalContact& contact)
{
    if (contact.BodyA == nullptr || contact.BodyB == nullptr || contact.BodyA == contact.BodyB) { return false; }
    if (ContainsBody(*contact.BodyA) == false || ContainsBody(*contact.BodyB) == false) { return false; }
    ThermalContact normalizedContact = contact;
    if (normalizedContact.ThermalConductance <= 0.0f)
    {
        normalizedContact.ThermalConductance = CalculateConductance(*normalizedContact.BodyA, *normalizedContact.BodyB,
            normalizedContact.ContactArea, normalizedContact.ConductionDistance, normalizedContact.ConductivityScale);
    }
    if (normalizedContact.ThermalConductance <= 0.0f) { return false; }
    m_Contacts.push_back(normalizedContact); return true;
}

bool ThermalWorld::RegisterEnvironmentContact(const ThermalEnvironmentContact& contact)
{
    if (contact.Body == nullptr || ContainsBody(*contact.Body) == false || contact.AmbientTemperature < 0.0f) { return false; }
    ThermalEnvironmentContact normalizedContact = contact;
    if (normalizedContact.ThermalConductance <= 0.0f)
    {
        normalizedContact.ThermalConductance = CalculateConvectionConductance(normalizedContact.HeatTransferCoefficient, normalizedContact.SurfaceArea);
    }
    if (normalizedContact.ThermalConductance <= 0.0f) { return false; }
    m_EnvironmentContacts.push_back(normalizedContact); return true;
}

bool ThermalWorld::RegisterRadiationContact(const ThermalRadiationContact& contact)
{
    if (contact.Body == nullptr || ContainsBody(*contact.Body) == false || contact.EnvironmentTemperature < 0.0f
        || contact.Emissivity < 0.0f || contact.Emissivity > 1.0f || contact.SurfaceArea <= 0.0f) { return false; }
    if (contact.Emissivity <= MinimumThermalValue) { return false; }
    m_RadiationContacts.push_back(contact); return true;
}

void ThermalWorld::ClearContacts() { m_Contacts.clear(); m_EnvironmentContacts.clear(); m_RadiationContacts.clear(); }
void ThermalWorld::Clear() { ClearContacts(); m_Bodies.clear(); m_LastSubstepCount = 0u; }
void ThermalWorld::SetSubstepSafetyFactor(float value) { if (value > MinimumThermalValue) { m_SubstepSafetyFactor = value; } }
void ThermalWorld::SetMaximumSubsteps(std::size_t value) { if (value > 0u) { m_MaximumSubsteps = value; } }

float ThermalWorld::CalculateConductance(const ThermalBody& bodyA, const ThermalBody& bodyB, float area, float distance, float scale)
{
    if (area <= 0.0f || distance <= MinimumThermalValue || scale <= 0.0f) { return 0.0f; }
    const float k = CalculateEffectiveConductivity(bodyA, bodyB); if (k <= 0.0f) { return 0.0f; }
    return k * scale * area / distance;
}
float ThermalWorld::CalculateConvectionConductance(float h, float area) { return (h > 0.0f && area > 0.0f) ? h * area : 0.0f; }

float ThermalWorld::CalculateRadiationHeatFlow(float bodyT, float envT, float emissivity, float area)
{
    if (bodyT < 0.0f || envT < 0.0f || emissivity <= 0.0f || emissivity > 1.0f || area <= 0.0f) { return 0.0f; }
    const float b2 = bodyT * bodyT; const float e2 = envT * envT;
    return emissivity * StefanBoltzmannConstant * area * (e2 * e2 - b2 * b2);
}

float ThermalWorld::CalculateRadiationTangentConductance(float bodyT, float emissivity, float area)
{
    if (bodyT < 0.0f || emissivity <= 0.0f || emissivity > 1.0f || area <= 0.0f) { return 0.0f; }
    return 4.0f * emissivity * StefanBoltzmannConstant * area * bodyT * bodyT * bodyT;
}

void ThermalWorld::Step(float dt)
{
    m_LastSubstepCount = 0u;
    if (dt <= 0.0f || m_Bodies.empty() == true || (m_Contacts.empty() == true && m_EnvironmentContacts.empty() == true && m_RadiationContacts.empty() == true)) { return; }
    std::vector<float> sums(m_Bodies.size(), 0.0f);
    for (const ThermalContact& c : m_Contacts)
    {
        const auto a = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyA); const auto b = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyB);
        if (c.BodyA != nullptr && c.BodyB != nullptr && c.ThermalConductance > 0.0f && a != m_Bodies.end() && b != m_Bodies.end())
        { sums[static_cast<std::size_t>(a - m_Bodies.begin())] += c.ThermalConductance; sums[static_cast<std::size_t>(b - m_Bodies.begin())] += c.ThermalConductance; }
    }
    for (const ThermalEnvironmentContact& c : m_EnvironmentContacts)
    {
        const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body);
        if (c.Body != nullptr && c.ThermalConductance > 0.0f && it != m_Bodies.end()) { sums[static_cast<std::size_t>(it - m_Bodies.begin())] += c.ThermalConductance; }
    }
    for (const ThermalRadiationContact& c : m_RadiationContacts)
    {
        const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body);
        if (c.Body != nullptr && it != m_Bodies.end())
        { sums[static_cast<std::size_t>(it - m_Bodies.begin())] += CalculateRadiationTangentConductance(c.Body->Temperature, c.Emissivity, c.SurfaceArea); }
    }
    float stableDt = dt;
    for (std::size_t i = 0; i < m_Bodies.size(); ++i)
    {
        if (m_Bodies[i] != nullptr && sums[i] > MinimumThermalValue && m_Bodies[i]->GetHeatCapacity() > MinimumThermalValue)
        { stableDt = std::min(stableDt, m_SubstepSafetyFactor * m_Bodies[i]->GetHeatCapacity() / sums[i]); }
    }
    std::size_t count = 1u;
    if (stableDt > MinimumThermalValue && stableDt < dt) { count = std::min(static_cast<std::size_t>(std::ceil(dt / stableDt)), m_MaximumSubsteps); }
    m_LastSubstepCount = count;
    const float subDt = dt / static_cast<float>(count); std::vector<float> deltas(m_Bodies.size(), 0.0f);
    for (std::size_t step = 0; step < count; ++step)
    {
        std::fill(deltas.begin(), deltas.end(), 0.0f);
        for (const ThermalContact& c : m_Contacts)
        {
            const auto a = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyA); const auto b = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyB);
            if (c.BodyA == nullptr || c.BodyB == nullptr || c.ThermalConductance <= 0.0f || a == m_Bodies.end() || b == m_Bodies.end()) { continue; }
            const float ca = c.BodyA->GetHeatCapacity(); const float cb = c.BodyB->GetHeatCapacity(); if (ca <= MinimumThermalValue || cb <= MinimumThermalValue) { continue; }
            float q = c.ThermalConductance * (c.BodyB->Temperature - c.BodyA->Temperature) * subDt;
            const float eq = (ca * c.BodyA->Temperature + cb * c.BodyB->Temperature) / (ca + cb);
            q = ClampHeatTowardTemperature(q, ca, c.BodyA->Temperature, eq);
            deltas[static_cast<std::size_t>(a - m_Bodies.begin())] += q; deltas[static_cast<std::size_t>(b - m_Bodies.begin())] -= q;
        }
        for (const ThermalEnvironmentContact& c : m_EnvironmentContacts)
        {
            const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body); if (c.Body == nullptr || it == m_Bodies.end()) { continue; }
            const float cap = c.Body->GetHeatCapacity(); if (cap <= MinimumThermalValue) { continue; }
            float q = c.ThermalConductance * (c.AmbientTemperature - c.Body->Temperature) * subDt;
            q = ClampHeatTowardTemperature(q, cap, c.Body->Temperature, c.AmbientTemperature);
            deltas[static_cast<std::size_t>(it - m_Bodies.begin())] += q;
        }
        for (const ThermalRadiationContact& c : m_RadiationContacts)
        {
            const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body); if (c.Body == nullptr || it == m_Bodies.end()) { continue; }
            const float cap = c.Body->GetHeatCapacity(); if (cap <= MinimumThermalValue) { continue; }
            float q = CalculateRadiationHeatFlow(c.Body->Temperature, c.EnvironmentTemperature, c.Emissivity, c.SurfaceArea) * subDt;
            q = ClampHeatTowardTemperature(q, cap, c.Body->Temperature, c.EnvironmentTemperature);
            deltas[static_cast<std::size_t>(it - m_Bodies.begin())] += q;
        }
        for (std::size_t i = 0; i < m_Bodies.size(); ++i)
        {
            ThermalBody* body = m_Bodies[i]; if (body == nullptr || body->GetHeatCapacity() <= MinimumThermalValue) { continue; }
            body->Temperature += deltas[i] / body->GetHeatCapacity(); body->Temperature = std::max(body->Temperature, 0.0f);
        }
    }
}

bool ThermalWorld::ContainsBody(const ThermalBody& body) const { return std::find(m_Bodies.begin(), m_Bodies.end(), &body) != m_Bodies.end(); }

}
