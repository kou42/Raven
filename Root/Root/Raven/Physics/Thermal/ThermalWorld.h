#pragma once

#include <cstddef>
#include <vector>

#include "Raven/Physics/Thermal/ThermalBody.h"

namespace Raven::ph
{
// ============================================================================
// ThermalContact
// ============================================================================
// 2つの集中熱容量間で熱を伝える接続を表します。
// Solverの正規入力は熱コンダクタンス G[W/K] です。
// ContactArea等は既存呼び出しとの互換性と診断表示用に残し、RegisterContact時に
// ThermalConductanceが未指定の場合だけ G = k_eff*A/d へ正規化します。
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

class ThermalWorld
{
public:
    bool RegisterBody(ThermalBody& body);
    bool UnregisterBody(ThermalBody& body);

    bool RegisterContact(const ThermalContact& contact);
    void ClearContacts();
    void Clear();

    void Step(float fixedDeltaTime);

    // 材料熱伝導率と簡易形状パラメータから G[W/K] を構築します。
    // 将来、接触熱抵抗を直接モデル化する境界はこの関数を使わずGを直接設定できます。
    static float CalculateConductance(
        const ThermalBody& bodyA,
        const ThermalBody& bodyB,
        float contactArea,
        float conductionDistance,
        float conductivityScale = 1.0f);

    bool ContainsBody(const ThermalBody& body) const;
    std::size_t GetRegisteredBodyCount() const { return m_Bodies.size(); }
    std::size_t GetContactCount() const { return m_Contacts.size(); }

    const std::vector<ThermalBody*>& GetRegisteredBodies() const { return m_Bodies; }
    const std::vector<ThermalContact>& GetContacts() const { return m_Contacts; }

private:
    std::vector<ThermalBody*> m_Bodies;
    std::vector<ThermalContact> m_Contacts;
};

}
