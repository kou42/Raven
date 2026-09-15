#pragma once

#include <cstddef>
#include <vector>

#include "Raven/Math/MathVector.h"
#include "Raven/Physics/Thermal/TemperatureField.h"
#include "Raven/Physics/Thermal/ThermalBody.h"

namespace Raven::ph
{
// ============================================================================
// ThermalContact
// ============================================================================
// 2つの集中熱容量Body間の熱伝導リンクです。
// Solver内部では形状そのものではなく ThermalConductance G [W/K] を共通入力として扱います。
// これにより、Collider形状や将来の接触熱抵抗モデルをSolverから分離できます。
// ContactArea / ConductionDistance / ConductivityScaleはGを構築するための入力兼診断値で、
// ThermalConductanceを直接指定した場合はSolverがこれらを再解釈しません。
struct ThermalContact
{
    ThermalBody* BodyA = nullptr;
    ThermalBody* BodyB = nullptr;
    float ThermalConductance = 0.0f; // G [W/K]
    float ContactArea = 0.0f;        // A [m^2]
    float ConductionDistance = 0.0f; // d [m]
    float ConductivityScale = 1.0f;  // 無次元補正係数
};

// ============================================================================
// ThermalEnvironmentContact
// ============================================================================
// Newtonの冷却則 Qdot = h*A*(Tenv - Tbody) を表す線形な環境境界です。
// Environmentは一定温度の無限Reservoirとして扱うため、Bodyから出入りした熱で
// AmbientTemperature自身は変化しません。h*Aは登録時にG [W/K]へ正規化します。
// TemperatureFieldを指定した場合はBodyPositionでFieldを評価し、AmbientTemperatureより優先します。
struct ThermalEnvironmentContact
{
    ThermalBody* Body = nullptr;
    float AmbientTemperature = 293.15f;       // Tenv [K]
    float HeatTransferCoefficient = 10.0f;    // h [W/(m^2*K)]
    float SurfaceArea = 1.0f;                 // A [m^2]
    float ThermalConductance = 0.0f;          // G=h*A [W/K]
    const TemperatureField* AmbientTemperatureField = nullptr; // 非所有。登録中は呼び出し側が寿命を保証します。
    math::Vec3 BodyPosition{};                 // Fieldを評価するworld-space位置

    float EvaluateAmbientTemperature() const
    {
        if (AmbientTemperatureField != nullptr)
        {
            return AmbientTemperatureField->Evaluate(BodyPosition);
        }
        return AmbientTemperature;
    }
};

// ============================================================================
// ThermalRadiationContact
// ============================================================================
// Stefan-Boltzmann則による非線形なEnvironment境界です。
// 放射は温度差に対して線形ではないため固定Gへ置き換えず、各substepの現在温度から
// Qdotを再評価します。接線Conductanceは実熱流ではなく、explicit Solverの安定性を
// 見積もる用途だけに使用します。Environmentは一定温度、View Factor=1の近似です。
struct ThermalRadiationContact
{
    ThermalBody* Body = nullptr;
    float EnvironmentTemperature = 293.15f; // [K]
    float Emissivity = 0.9f;                 // epsilon [-]
    float SurfaceArea = 1.0f;                // A [m^2]
};

// ============================================================================
// ThermalWorld
// ============================================================================
// Ravenの集中熱容量Thermal Domainを進める非所有Runtime Worldです。
// ThermalBodyの所有権はECS側に残し、ここではFixed Step中に有効なpointerだけを登録します。
// 伝導・対流・放射はすべて「substep中に移動した熱量Q [J]」へ変換して一括反映することで、
// Contactの列挙順による温度更新順序依存を抑えています。
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

    // Explicit熱Solverの安定性を保つため、各Bodyの熱時定数 tau=C/sum(G) から
    // Thermal Domain内部だけのsubstep数を自動決定します。Physics全体のFixed Stepは変更しません。
    void SetSubstepSafetyFactor(float safetyFactor);
    float GetSubstepSafetyFactor() const { return m_SubstepSafetyFactor; }
    void SetMaximumSubsteps(std::size_t maximumSubsteps);
    std::size_t GetMaximumSubsteps() const { return m_MaximumSubsteps; }
    std::size_t GetLastSubstepCount() const { return m_LastSubstepCount; }

    // k_eff*A/d をG [W/K]へ変換します。異材質間では片側の高い熱伝導率だけに
    // 支配されないよう、実装側で両材料の調和平均を使用します。
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
    // 非所有pointerです。ECSのdense storage再配置を跨いで保持しないよう、ThermalSystemが毎Fixed Step再構築します。
    std::vector<ThermalBody*> m_Bodies;
    std::vector<ThermalContact> m_Contacts;
    std::vector<ThermalEnvironmentContact> m_EnvironmentContacts;
    std::vector<ThermalRadiationContact> m_RadiationContacts;

    // 0.5はexplicit Eulerに対して保守的な既定値です。64上限は極端に硬いNetworkで
    // Thermalだけが無制限に計算量を増やすことを防ぐための実行時間側の安全弁です。
    float m_SubstepSafetyFactor = 0.5f;
    std::size_t m_MaximumSubsteps = 64u;
    std::size_t m_LastSubstepCount = 0u;
};

}
