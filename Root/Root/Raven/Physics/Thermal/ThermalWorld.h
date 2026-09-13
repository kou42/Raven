#pragma once

#include <cstddef>
#include <vector>

#include "Raven/Physics/Thermal/ThermalBody.h"

namespace Raven::ph
{
// ============================================================================
// ThermalContact
// ============================================================================
// 2つの集中熱容量間で熱を伝える接触を表します。
// ContactArea[m^2]とConductionDistance[m]からFourier則の離散形を評価します。
// ConductivityScaleは接触抵抗などを後から表現できるようにする無次元係数です。
struct ThermalContact
{
    ThermalBody* BodyA = nullptr;
    ThermalBody* BodyB = nullptr;
    float ContactArea = 1.0f;
    float ConductionDistance = 1.0f;
    float ConductivityScale = 1.0f;
};

// ============================================================================
// ThermalWorld
// ============================================================================
// Ravenの熱Domainを担当する最小Worldです。
// Body/Contactは所有せず、登録された参照をFixed Stepで更新します。
//
// 初期段階では熱伝導だけを扱い、対流・放射・相転移・Physics Contactとの自動Couplingは
// この基礎モデルのエネルギー保存を確認した後に追加します。
class ThermalWorld
{
public:
    bool RegisterBody(ThermalBody& body);
    bool UnregisterBody(ThermalBody& body);

    bool RegisterContact(const ThermalContact& contact);
    void ClearContacts();
    void Clear();

    void Step(float fixedDeltaTime);

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
