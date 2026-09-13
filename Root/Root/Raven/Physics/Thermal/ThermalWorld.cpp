#include "Raven/Physics/Thermal/ThermalWorld.h"

#include <algorithm>
#include <cmath>

namespace Raven::ph
{
namespace
{
constexpr float MinimumThermalValue = 1.0e-8f;
constexpr float StefanBoltzmannConstant = 5.670374419e-8f;

// 異材質境界では熱流が低い熱伝導率側にも制限されるため、単純平均ではなく調和平均を使います。
// これにより一方のkが非常に大きい場合でも、もう一方の抵抗を無視した過大なGになりにくくします。
float CalculateEffectiveConductivity(const ThermalBody& bodyA, const ThermalBody& bodyB)
{
    const float a = bodyA.Material.ThermalConductivity;
    const float b = bodyB.Material.ThermalConductivity;
    if (a <= MinimumThermalValue || b <= MinimumThermalValue)
    {
        return 0.0f;
    }
    return (2.0f * a * b) / (a + b);
}

// Explicit更新が大きなdtや強いGで平衡温度を飛び越えないよう、1境界が1substepで
// 移動できる熱量を「現在温度から目標温度までに必要な熱量」へ制限します。
// MaximumSubstepsへ到達した場合にも破綻を抑えるための局所的な安全網です。
float ClampHeat(float heat, float capacity, float current, float target)
{
    const float limit = capacity * (target - current);
    if (limit >= 0.0f)
    {
        return std::min(heat, limit);
    }
    return std::max(heat, limit);
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
    const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), &body);
    if (it == m_Bodies.end())
    {
        return false;
    }
    m_Bodies.erase(it);

    // 非所有Bodyを外すときは、それを参照する全境界も同時に破棄してdangling pointerを残しません。
    m_Contacts.erase(std::remove_if(m_Contacts.begin(), m_Contacts.end(), [&body](const ThermalContact& c) { return c.BodyA == &body || c.BodyB == &body; }), m_Contacts.end());
    m_EnvironmentContacts.erase(std::remove_if(m_EnvironmentContacts.begin(), m_EnvironmentContacts.end(), [&body](const ThermalEnvironmentContact& c) { return c.Body == &body; }), m_EnvironmentContacts.end());
    m_RadiationContacts.erase(std::remove_if(m_RadiationContacts.begin(), m_RadiationContacts.end(), [&body](const ThermalRadiationContact& c) { return c.Body == &body; }), m_RadiationContacts.end());
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

    // Solverのcanonical inputはG [W/K]です。旧来/診断用の形状値だけが渡された場合も
    // 登録境界で一度だけGへ正規化し、Step中に形状モデルを再評価しません。
    ThermalContact c = contact;
    if (c.ThermalConductance <= 0.0f)
    {
        c.ThermalConductance = CalculateConductance(*c.BodyA, *c.BodyB, c.ContactArea, c.ConductionDistance, c.ConductivityScale);
    }
    if (c.ThermalConductance <= 0.0f)
    {
        return false;
    }
    m_Contacts.push_back(c);
    return true;
}

bool ThermalWorld::RegisterEnvironmentContact(const ThermalEnvironmentContact& contact)
{
    if (contact.Body == nullptr || ContainsBody(*contact.Body) == false || contact.AmbientTemperature < 0.0f)
    {
        return false;
    }
    ThermalEnvironmentContact c = contact;
    if (c.ThermalConductance <= 0.0f)
    {
        c.ThermalConductance = CalculateConvectionConductance(c.HeatTransferCoefficient, c.SurfaceArea);
    }
    if (c.ThermalConductance <= 0.0f)
    {
        return false;
    }
    m_EnvironmentContacts.push_back(c);
    return true;
}

bool ThermalWorld::RegisterRadiationContact(const ThermalRadiationContact& contact)
{
    if (contact.Body == nullptr || ContainsBody(*contact.Body) == false || contact.EnvironmentTemperature < 0.0f
        || contact.Emissivity <= MinimumThermalValue || contact.Emissivity > 1.0f || contact.SurfaceArea <= 0.0f)
    {
        return false;
    }
    m_RadiationContacts.push_back(contact);
    return true;
}

void ThermalWorld::ClearContacts()
{
    m_Contacts.clear();
    m_EnvironmentContacts.clear();
    m_RadiationContacts.clear();
}

void ThermalWorld::Clear()
{
    ClearContacts();
    m_Bodies.clear();
    m_LastSubstepCount = 0u;
}

void ThermalWorld::SetSubstepSafetyFactor(float value)
{
    if (value > MinimumThermalValue)
    {
        m_SubstepSafetyFactor = value;
    }
}

void ThermalWorld::SetMaximumSubsteps(std::size_t value)
{
    if (value > 0u)
    {
        m_MaximumSubsteps = value;
    }
}

float ThermalWorld::CalculateConductance(const ThermalBody& a, const ThermalBody& b, float area, float distance, float scale)
{
    if (area <= 0.0f || distance <= MinimumThermalValue || scale <= 0.0f)
    {
        return 0.0f;
    }
    const float k = CalculateEffectiveConductivity(a, b);
    if (k <= 0.0f)
    {
        return 0.0f;
    }
    return k * scale * area / distance;
}

float ThermalWorld::CalculateConvectionConductance(float h, float area)
{
    if (h <= 0.0f || area <= 0.0f)
    {
        return 0.0f;
    }
    return h * area;
}

float ThermalWorld::CalculateRadiationHeatFlow(float t, float e, float emissivity, float area)
{
    if (t < 0.0f || e < 0.0f || emissivity <= 0.0f || emissivity > 1.0f || area <= 0.0f)
    {
        return 0.0f;
    }
    const float t2 = t * t;
    const float e2 = e * e;
    return emissivity * StefanBoltzmannConstant * area * (e2 * e2 - t2 * t2);
}

float ThermalWorld::CalculateRadiationTangentConductance(float t, float emissivity, float area)
{
    if (t < 0.0f || emissivity <= 0.0f || emissivity > 1.0f || area <= 0.0f)
    {
        return 0.0f;
    }

    // Stefan-Boltzmann則自体は非線形のまま熱流計算へ使います。
    // ここで求める |dQdot/dT|=4*epsilon*sigma*A*T^3 は、時定数評価用の局所的なGだけです。
    return 4.0f * emissivity * StefanBoltzmannConstant * area * t * t * t;
}

void ThermalWorld::Step(float dt)
{
    m_LastSubstepCount = 0u;
    if (dt <= 0.0f || m_Bodies.empty() == true
        || (m_Contacts.empty() == true && m_EnvironmentContacts.empty() == true && m_RadiationContacts.empty() == true))
    {
        return;
    }

    // 各Bodyへ接続する総Conductance sum(G) を集計します。
    // 集中熱容量C=m*cに対する熱時定数 tau=C/sum(G) から、explicit更新の安定な刻み幅を決めます。
    std::vector<float> sums(m_Bodies.size(), 0.0f);
    for (const ThermalContact& c : m_Contacts)
    {
        if (c.BodyA == nullptr || c.BodyB == nullptr || c.ThermalConductance <= 0.0f)
        {
            continue;
        }
        const auto a = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyA);
        const auto b = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyB);
        if (a == m_Bodies.end() || b == m_Bodies.end())
        {
            continue;
        }
        sums[static_cast<std::size_t>(a - m_Bodies.begin())] += c.ThermalConductance;
        sums[static_cast<std::size_t>(b - m_Bodies.begin())] += c.ThermalConductance;
    }
    for (const ThermalEnvironmentContact& c : m_EnvironmentContacts)
    {
        if (c.Body == nullptr || c.ThermalConductance <= 0.0f)
        {
            continue;
        }
        const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body);
        if (it != m_Bodies.end())
        {
            sums[static_cast<std::size_t>(it - m_Bodies.begin())] += c.ThermalConductance;
        }
    }
    for (const ThermalRadiationContact& c : m_RadiationContacts)
    {
        if (c.Body == nullptr)
        {
            continue;
        }
        const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body);
        if (it == m_Bodies.end())
        {
            continue;
        }

        // 放射は固定Gではないため、Step開始温度における接線Gだけを安定性見積もりへ加えます。
        sums[static_cast<std::size_t>(it - m_Bodies.begin())] += CalculateRadiationTangentConductance(c.Body->Temperature, c.Emissivity, c.SurfaceArea);
    }

    // SafetyFactor=0.5を掛けた最も短い時定数を採用します。
    // PhysicsSimulationWorldのFixed Step自体は細分化せず、Thermal Domain内部だけをsubstep化します。
    float stable = dt;
    for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
    {
        if (m_Bodies[i] == nullptr || sums[i] <= MinimumThermalValue)
        {
            continue;
        }
        const float cap = m_Bodies[i]->GetHeatCapacity();
        if (cap <= MinimumThermalValue)
        {
            continue;
        }
        stable = std::min(stable, m_SubstepSafetyFactor * cap / sums[i]);
    }

    std::size_t count = 1u;
    if (stable > MinimumThermalValue && stable < dt)
    {
        count = std::min(static_cast<std::size_t>(std::ceil(dt / stable)), m_MaximumSubsteps);
    }
    m_LastSubstepCount = count;
    const float subDt = dt / static_cast<float>(count);
    std::vector<float> heatDeltas(m_Bodies.size(), 0.0f);

    for (std::size_t step = 0u; step < count; ++step)
    {
        // 全境界を同じsubstep開始温度から評価するため、温度をその場で変更せずQ [J]だけ蓄積します。
        // これによりContactのvector順が温度結果へ直接影響するGauss-Seidel型更新を避けます。
        std::fill(heatDeltas.begin(), heatDeltas.end(), 0.0f);
        for (const ThermalContact& c : m_Contacts)
        {
            if (c.BodyA == nullptr || c.BodyB == nullptr || c.ThermalConductance <= 0.0f)
            {
                continue;
            }
            const auto a = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyA);
            const auto b = std::find(m_Bodies.begin(), m_Bodies.end(), c.BodyB);
            if (a == m_Bodies.end() || b == m_Bodies.end())
            {
                continue;
            }
            const float ca = c.BodyA->GetHeatCapacity();
            const float cb = c.BodyB->GetHeatCapacity();
            if (ca <= MinimumThermalValue || cb <= MinimumThermalValue)
            {
                continue;
            }

            // Fourier型の集中モデル Q=G*(Tb-Ta)*dt です。
            // Aへ+Q、Bへ-Qを対称に加えることで、Body間伝導では総熱エネルギーを保存します。
            float heat = c.ThermalConductance * (c.BodyB->Temperature - c.BodyA->Temperature) * subDt;
            const float equilibrium = (ca * c.BodyA->Temperature + cb * c.BodyB->Temperature) / (ca + cb);
            heat = ClampHeat(heat, ca, c.BodyA->Temperature, equilibrium);
            heatDeltas[static_cast<std::size_t>(a - m_Bodies.begin())] += heat;
            heatDeltas[static_cast<std::size_t>(b - m_Bodies.begin())] -= heat;
        }
        for (const ThermalEnvironmentContact& c : m_EnvironmentContacts)
        {
            if (c.Body == nullptr || c.ThermalConductance <= 0.0f)
            {
                continue;
            }
            const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body);
            if (it == m_Bodies.end())
            {
                continue;
            }
            const float cap = c.Body->GetHeatCapacity();
            if (cap <= MinimumThermalValue)
            {
                continue;
            }

            // Environmentは無限ReservoirなのでBody側にだけQを加えます。したがってBody集合だけを見た
            // エネルギーは保存されませんが、これは外部環境との熱交換を表す意図した挙動です。
            float heat = c.ThermalConductance * (c.AmbientTemperature - c.Body->Temperature) * subDt;
            heat = ClampHeat(heat, cap, c.Body->Temperature, c.AmbientTemperature);
            heatDeltas[static_cast<std::size_t>(it - m_Bodies.begin())] += heat;
        }
        for (const ThermalRadiationContact& c : m_RadiationContacts)
        {
            if (c.Body == nullptr)
            {
                continue;
            }
            const auto it = std::find(m_Bodies.begin(), m_Bodies.end(), c.Body);
            if (it == m_Bodies.end())
            {
                continue;
            }
            const float cap = c.Body->GetHeatCapacity();
            if (cap <= MinimumThermalValue)
            {
                continue;
            }

            // 放射熱流は各substepの現在温度でT^4式を再評価し、線形化誤差を実熱流へ持ち込みません。
            float heat = CalculateRadiationHeatFlow(c.Body->Temperature, c.EnvironmentTemperature, c.Emissivity, c.SurfaceArea) * subDt;
            heat = ClampHeat(heat, cap, c.Body->Temperature, c.EnvironmentTemperature);
            heatDeltas[static_cast<std::size_t>(it - m_Bodies.begin())] += heat;
        }

        // 熱量の蓄積までは全境界を同じsubstep開始状態から評価し、最後に各Bodyへ一括適用します。
        // ApplyHeatが顕熱と潜熱の配分を担当するため、相転移中もSolver側は熱量ベースのまま保てます。
        for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
        {
            ThermalBody* body = m_Bodies[i];
            if (body == nullptr)
            {
                continue;
            }
            if (body->GetHeatCapacity() <= MinimumThermalValue)
            {
                continue;
            }
            body->ApplyHeat(heatDeltas[i]);
        }
    }
}

bool ThermalWorld::ContainsBody(const ThermalBody& body) const
{
    return std::find(m_Bodies.begin(), m_Bodies.end(), &body) != m_Bodies.end();
}

}
