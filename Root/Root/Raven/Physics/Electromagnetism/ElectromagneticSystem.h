#pragma once

#include "Raven/Physics/Electromagnetism/CoulombForce.h"

namespace Raven
{
class Scene;
}

namespace Raven::ph
{

class ElectromagneticSystem
{
public:
    // Scene内の点電荷ペアを直接法 O(n^2) で評価し、RigidBodyComponent::Forceへ蓄積します。
    // 現段階では基礎実装の正しさを優先し、Barnes-Hut等の近似高速化は導入しません。
    void ApplyCoulombForces(Scene& scene) const;

    void SetCoulombForceSettings(const CoulombForceSettings& settings)
    {
        m_CoulombForceSettings = settings;
    }

    const CoulombForceSettings& GetCoulombForceSettings() const
    {
        return m_CoulombForceSettings;
    }

private:
    CoulombForceSettings m_CoulombForceSettings{};
};

} // namespace Raven::ph
