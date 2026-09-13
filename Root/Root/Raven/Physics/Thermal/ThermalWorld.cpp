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
    const float a = bodyA.Material.ThermalConductivity;
    const float b = bodyB.Material.ThermalConductivity;
    if (a <= MinimumThermalValue || b <= MinimumThermalValue) { return 0.0f; }
    return (2.0f * a * b) / (a + b);
}

float ClampHeat(float heat, float capacity, float current, float target)
{
    const float limit = capacity * (target - current);
    if (limit >= 0.0f) { return std::min(heat, limit); }
    return std::max(heat, limit);
}
}

bool ThermalWorld::RegisterBody(ThermalBody& body)
{
    if (ContainsBody(body) == true) { return false; }
    m_Bodies.push_back(&body); return true;
}

bool ThermalWorld::UnregisterBody(ThermalBody& body)
{
    const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), &body);
    if (it == m_Bodies.end()) { return false; }
    m_Bodies.erase(it);
    m_Contacts.erase(std::remove_if(m_Contacts.begin(), m_Contacts.end(), [&body](const ThermalContact& c) { return c.BodyA == &body || c.BodyB == &body; }), m_Contacts.end());
    m_EnvironmentContacts.erase(std::remove_if(m_EnvironmentContacts.begin(), m_EnvironmentContacts.end(), [&body](const ThermalEnvironmentContact& c) { return c.Body == &body; }), m_EnvironmentContacts.end());
    m_RadiationContacts.erase(std::remove_if(m_RadiationContacts.begin(), m_RadiationContacts.end(), [&body](const ThermalRadiationContact& c) { return c.Body == &body; }), m_RadiationContacts.end());
    return true;
}

bool ThermalWorld::RegisterContact(const ThermalContact& contact)
{
    if (contact.BodyA == nullptr || contact.BodyB == nullptr || contact.BodyA == contact.BodyB) { return false; }
    if (ContainsBody(*contact.BodyA) == false || ContainsBody(*contact.BodyB) == false) { return false; }
    ThermalContact c = contact;
    if (c.ThermalConductance <= 0.0f) { c.ThermalConductance = CalculateConductance(*c.BodyA, *c.BodyB, c.ContactArea, c.ConductionDistance, c.ConductivityScale); }
    if (c.ThermalConductance <= 0.0f) { return false; }
    m_Contacts.push_back(c); return true;
}

bool ThermalWorld::RegisterEnvironmentContact(const ThermalEnvironmentContact& contact)
{
    if (contact.Body == nullptr || ContainsBody(*contact.Body) == false || contact.AmbientTemperature < 0.0f) { return false; }
    ThermalEnvironmentContact c = contact;
    if (c.ThermalConductance <= 0.0f) { c.ThermalConductance = CalculateConvectionConductance(c.HeatTransferCoefficient, c.SurfaceArea); }
    if (c.ThermalConductance <= 0.0f) { return false; }
    m_EnvironmentContacts.push_back(c); return true;
}

bool ThermalWorld::RegisterRadiationContact(const ThermalRadiationContact& contact)
{
    if (contact.Body == nullptr || ContainsBody(*contact.Body) == false || contact.EnvironmentTemperature < 0.0f
        || contact.Emissivity <= MinimumThermalValue || contact.Emissivity > 1.0f || contact.SurfaceArea <= 0.0f) { return false; }
    m_RadiationContacts.push_back(contact); return true;
}

void ThermalWorld::ClearContacts() { m_Contacts.clear(); m_EnvironmentContacts.clear(); m_RadiationContacts.clear(); }
void ThermalWorld::Clear() { ClearContacts(); m_Bodies.clear(); m_LastSubstepCount = 0u; }
void ThermalWorld::SetSubstepSafetyFactor(float v) { if (v > MinimumThermalValue) { m_SubstepSafetyFactor = v; } }
void ThermalWorld::SetMaximumSubsteps(std::size_t v) { if (v > 0u) { m_MaximumSubsteps = v; } }

float ThermalWorld::CalculateConductance(const ThermalBody& a, const ThermalBody& b, float area, float distance, float scale)
{
    if (area <= 0.0f || distance <= MinimumThermalValue || scale <= 0.0f) { return 0.0f; }
    const float k = CalculateEffectiveConductivity(a, b); if (k <= 0.0f) { return 0.0f; }
    return k * scale * area / distance;
}
float ThermalWorld::CalculateConvectionConductance(float h, float area) { if (h <= 0.0f || area <= 0.0f) { return 0.0f; } return h * area; }
float ThermalWorld::CalculateRadiationHeatFlow(float t, float e, float emissivity, float area)
{
    if (t < 0.0f || e < 0.0f || emissivity <= 0.0f || emissivity > 1.0f || area <= 0.0f) { return 0.0f; }
    const float t2 = t * t; const float e2 = e * e; return emissivity * StefanBoltzmannConstant * area * (e2 * e2 - t2 * t2);
}
float ThermalWorld::CalculateRadiationTangentConductance(float t, float emissivity, float area)
{
    if (t < 0.0f || emissivity <= 0.0f || emissivity > 1.0f || area <= 0.0f) { return 0.0f; }
    return 4.0f * emissivity * StefanBoltzmannConstant * area * t * t * t;
}

void ThermalWorld::Step(float dt)
{
    m_LastSubstepCount = 0u;
    if (dt <= 0.0f || m_Bodies.empty() == true || (m_Contacts.empty() == true && m_EnvironmentContacts.empty() == true && m_RadiationContacts.empty() == true)) { return; }
    std::vector<float> sums(m_Bodies.size(), 0.0f);
    for (const ThermalContact& c : m_Contacts)
    {
        if (c.BodyA == nullptr || c.BodyB == nullptr || c.ThermalConductance <= 0.0f) { continue; }
        const auto a = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyA); const auto b = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyB);
        if (a == m_Bodies.end() || b == m_Bodies.end()) { continue; }
        sums[static_cast<std::size_t>(a - m_Bodies.begin())] += c.ThermalConductance; sums[static_cast<std::size_t>(b - m_Bodies.begin())] += c.ThermalConductance;
    }
    for (const ThermalEnvironmentContact& c : m_EnvironmentContacts)
    {
        if (c.Body == nullptr || c.ThermalConductance <= 0.0f) { continue; }
        const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body); if (it != m_Bodies.end()) { sums[static_cast<std::size_t>(it - m_Bodies.begin())] += c.ThermalConductance; }
    }
    for (const ThermalRadiationContact& c : m_RadiationContacts)
    {
        if (c.Body == nullptr) { continue; }
        const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body); if (it == m_Bodies.end()) { continue; }
        sums[static_cast<std::size_t>(it - m_Bodies.begin())] += CalculateRadiationTangentConductance(c.Body->Temperature, c.Emissivity, c.SurfaceArea);
    }
    float stable = dt;
    for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
    {
        if (m_Bodies[i] == nullptr || sums[i] <= MinimumThermalValue) { continue; }
        const float cap = m_Bodies[i]->GetHeatCapacity(); if (cap <= MinimumThermalValue) { continue; }
        stable = std::min(stable, m_SubstepSafetyFactor * cap / sums[i]);
    }
    std::size_t count = 1u; if (stable > MinimumThermalValue && stable < dt) { count = std::min(static_cast<std::size_t>(std::ceil(dt / stable)), m_MaximumSubsteps); }
    m_LastSubstepCount = count; const float subDt = dt / static_cast<float>(count); std::vector<float> q(m_Bodies.size(), 0.0f);
    for (std::size_t step = 0u; step < count; ++step)
    {
        std::fill(q.begin(), q.end(), 0.0f);
        for (const ThermalContact& c : m_Contacts)
        {
            if (c.BodyA == nullptr || c.BodyB == nullptr || c.ThermalConductance <= 0.0f) { continue; }
            const auto a = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyA); const auto b = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyB); if (a == m_Bodies.end() || b == m_Bodies.end()) { continue; }
            const float ca = c.BodyA->GetHeatCapacity(); const float cb = c.BodyB->GetHeatCapacity(); if (ca <= MinimumThermalValue || cb <= MinimumThermalValue) { continue; }
            float heat = c.ThermalConductance * (c.BodyB->Temperature - c.BodyA->Temperature) * subDt; const float eq = (ca * c.BodyA->Temperature + cb * c.BodyB->Temperature) / (ca + cb);
            heat = ClampHeat(heat, ca, c.BodyA->Temperature, eq); q[static_cast<std::size_t>(a - m_Bodies.begin())] += heat; q[static_cast<std::size_t>(b - m_Bodies.begin())] -= heat;
        }
        for (const ThermalEnvironmentContact& c : m_EnvironmentContacts)
        {
            if (c.Body == nullptr || c.ThermalConductance <= 0.0f) { continue; }
            const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body); if (it == m_Bodies.end()) { continue; }
            const float cap = c.Body->GetHeatCapacity(); if (cap <= MinimumThermalValue) { continue; }
            float heat = c.ThermalConductance * (c.AmbientTemperature - c.Body->Temperature) * subDt; heat = ClampHeat(heat, cap, c.Body->Temperature, c.AmbientTemperature); q[static_cast<std::size_t>(it - m_Bodies.begin())] += heat;
        }
        for (const ThermalRadiationContact& c : m_RadiationContacts)
        {
            if (c.Body == nullptr) { continue; }
            const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body); if (it == m_Bodies.end()) { continue; }
            const float cap = c.Body->GetHeatCapacity(); if (cap <= MinimumThermalValue) { continue; }
            float heat = CalculateRadiationHeatFlow(c.Body->Temperature, c.EnvironmentTemperature, c.Emissivity, c.SurfaceArea) * subDt; heat = ClampHeat(heat, cap, c.Body->Temperature, c.EnvironmentTemperature); q[static_cast<std::size_t>(it - m_Bodies.begin())] += heat;
        }
        for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
        {
            ThermalBody* body = m_Bodies[i]; if (body == nullptr) { continue; } const float cap = body->GetHeatCapacity(); if (cap <= MinimumThermalValue) { continue; }
            body->Temperature += q[i] / cap; body->Temperature = std::max(body->Temperature, 0.0f);
        }
    }
}

bool ThermalWorld::ContainsBody(const ThermalBody& body) const { return std::find(m_Bodies.begin(), m_Bodies.end(), &body) != m_Bodies.end(); }

}
