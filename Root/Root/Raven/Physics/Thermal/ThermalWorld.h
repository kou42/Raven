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
    float ContactArea = 0.0f;
    float ConductionDistance = 0.0f;
    float ConductivityScale = 1.0f;
};

struct ThermalEnvironmentContact
{
    ThermalBody* Body = nullptr;
    float AmbientTemperature = 293.15f;
    float HeatTransferCoefficient = 10.0f;
    float SurfaceArea = 1.0f;
    float ThermalConductance = 0.0f;
};

// ============================================================================
// ThermalRadiationContact
// ============================================================================
// Stefan-Boltzmann則による非線形なEnvironment境界です。
// 放射は温度差に対して線形ではないため固定Gを保持せず、各substepの現在温度から
// Qdotと接線Conductanceを再評価します。Environmentは一定温度の無限Reservoirです。
struct ThermalRadiationContact
{
    ThermalBody* Body = nullptr;
    float EnvironmentTemperature = 293.15f; // [K]
    float Emissivity = 0.9f;                 // [-]
    float SurfaceArea = 1.0f;                // [m^2]
};

class ThermalWorld
{
public:
    bool RegisterBody(ThermalBody& body);
    bool UnregisterBody(ThermalBody& body);

    bool RegisterContact(const ThermalContact& contact);
    bool RegisterEnvironmentContact(const ThermalEnvironmentContact& contact);
    bool RegisterRadiationContact(const ThermalRadiationContact& contact);
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

    static float CalculateConvectionConductance(float heatTransferCoefficient, float surfaceArea);
    static float CalculateRadiationHeatFlow(
        float bodyTemperature,
        float environmentTemperature,
        float emissivity,
        float surfaceArea);
    static float CalculateRadiationTangentConductance(
        float bodyTemperature,
        float emissivity,
        float surfaceArea);

    bool ContainsBody(const ThermalBody& body) const;
    std::size_t GetRegisteredBodyCount() const { return m_Bodies.size(); }
    std::size_t GetContactCount() const { return m_Contacts.size(); }
    std::size_t GetEnvironmentContactCount() const { return m_EnvironmentContacts.size(); }
    std::size_t GetRadiationContactCount() const { return m_RadiationContacts.size(); }

    const std::vector<ThermalBody*>& GetRegisteredBodies() const { return m_Bodies; }
    const std::vector<ThermalContact>& GetContacts() const { return m_Contacts; }
    const std::vector<ThermalEnvironmentContact>& GetEnvironmentContacts() const { return m_EnvironmentContacts; }
    const std::vector<ThermalRadiationContact>& GetRadiationContacts() const { return m_RadiationContacts; }

private:
    std::vector<ThermalBody*> m_Bodies;
    std::vector<ThermalContact> m_Contacts;
    std::vector<ThermalEnvironmentContact> m_EnvironmentContacts;
    std::vector<ThermalRadiationContact> m_RadiationContacts;

    float m_SubstepSafetyFactor = 0.5f;
    std::size_t m_MaximumSubsteps = 64u;
    std::size_t m_LastSubstepCount = 0u;
};

}
