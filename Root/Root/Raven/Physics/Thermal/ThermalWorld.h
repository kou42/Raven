#pragma once

#include <cstddef>
#include <vector>

#include "Raven/Physics/Thermal/ThermalBody.h"

namespace Raven::ph
{
struct ThermalContact
{
    ThermalBody* BodyA = nullptr;
    ThermalBody* BodyB = nullptr;
    float ThermalConductance = 0.0f;

    // Legacy/diagnostic geometry. SolverのStep()はこれらを直接参照しません。
    float ContactArea = 0.0f;
    float ConductionDistance = 0.0f;
    float ConductivityScale = 1.0f;
};

// ============================================================================
// ThermalEnvironmentContact
// ============================================================================
// 無限熱容量の環境Reservoirとの熱交換境界です。
// Newtonの冷却則 Qdot = h*A*(Tambient - Tbody) を G=h*A としてSolverへ渡します。
// Environment側の温度は熱交換で変化しないため、Body間Contactとは異なり熱量を返しません。
struct ThermalEnvironmentContact
{
    ThermalBody* Body = nullptr;
    float AmbientTemperature = 293.15f;
    float HeatTransferCoefficient = 10.0f; // h [W/(m^2*K)]
    float SurfaceArea = 1.0f;              // A [m^2]
    float ThermalConductance = 0.0f;       // G=h*A [W/K]
};

class ThermalWorld
{
public:
    bool RegisterBody(ThermalBody& body);
    bool UnregisterBody(ThermalBody& body);

    bool RegisterContact(const ThermalContact& contact);
    bool RegisterEnvironmentContact(const ThermalEnvironmentContact& contact);
    void ClearContacts();
    void Clear();

    void Step(float fixedDeltaTime);

    void SetSubstepSafetyFactor(float safetyFactor);
    float GetSubstepSafetyFactor() const { return m_SubstepSafetyFactor; }

    void SetMaximumSubsteps(std::size_t maximumSubsteps);
    std::size_t GetMaximumSubsteps() const { return m_MaximumSubsteps; }
    std::size_t GetLastSubstepCount() const { return m_LastSubstepCount; }

    static float CalculateConductance(
        const ThermalBody& bodyA,
        const ThermalBody& bodyB,
        float contactArea,
        float conductionDistance,
        float conductivityScale = 1.0f);

    static float CalculateConvectionConductance(
        float heatTransferCoefficient,
        float surfaceArea);

    bool ContainsBody(const ThermalBody& body) const;
    std::size_t GetRegisteredBodyCount() const { return m_Bodies.size(); }
    std::size_t GetContactCount() const { return m_Contacts.size(); }
    std::size_t GetEnvironmentContactCount() const { return m_EnvironmentContacts.size(); }

    const std::vector<ThermalBody*>& GetRegisteredBodies() const { return m_Bodies; }
    const std::vector<ThermalContact>& GetContacts() const { return m_Contacts; }
    const std::vector<ThermalEnvironmentContact>& GetEnvironmentContacts() const { return m_EnvironmentContacts; }

private:
    std::vector<ThermalBody*> m_Bodies;
    std::vector<ThermalContact> m_Contacts;
    std::vector<ThermalEnvironmentContact> m_EnvironmentContacts;

    float m_SubstepSafetyFactor = 0.5f;
    std::size_t m_MaximumSubsteps = 64u;
    std::size_t m_LastSubstepCount = 0u;
};

}
