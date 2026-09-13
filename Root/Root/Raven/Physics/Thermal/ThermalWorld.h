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
// Solver内部では形状や距離を直接解釈せず、熱コンダクタンス G[W/K] だけを使用します。
// これによりRigid Contact、明示Link、将来のSoftBody Contactなど異なる接触モデルを
// 同じ熱伝導Solverへ接続できます。
struct ThermalContact
{
    ThermalBody* BodyA = nullptr;
    ThermalBody* BodyB = nullptr;
    float ThermalConductance = 0.0f;
};

// ============================================================================
// ThermalWorld
// ============================================================================
// Ravenの熱Domainを担当する最小Worldです。
// Body/Contactは所有せず、登録された参照をFixed Stepで更新します。
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
    // k*A/d は境界側の近似に閉じ込め、Solver本体はConductanceだけを扱います。
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
