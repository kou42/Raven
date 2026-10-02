#pragma once

#include <cstddef>
#include <vector>

#include "Raven/Physics/Electromagnetism/BarnesHutCoulombSolver.h"
#include "Raven/Physics/Electromagnetism/CoulombForce.h"
#include "Raven/Physics/Electromagnetism/ElectromagneticStatistics.h"
#include "Raven/Physics/Electromagnetism/ElectricField.h"
#include "Raven/Physics/Electromagnetism/MagneticField.h"

namespace Raven
{
class Scene;
}

namespace Raven::ph
{

enum class CoulombSolverMode
{
    Automatic,
    Direct,
    BarnesHut
};

struct CoulombSolverSelectionSettings
{
    CoulombSolverMode Mode = CoulombSolverMode::Automatic;
    std::size_t BarnesHutBodyThreshold = 1000u;
    std::size_t DirectBodyThreshold = 800u;
    double BarnesHutTheta = 0.5;
};

class ElectromagneticSystem
{
public:
    // 外部ElectricFieldはSystemが所有しません。Scene/Demo等でFieldの寿命を管理し、
    // 登録解除はField破棄前に行います。同一Fieldの二重登録は拒否します。
    bool RegisterElectricField(const ElectricField& electricField);
    bool UnregisterElectricField(const ElectricField& electricField);
    void ClearElectricFields();

    bool ContainsElectricField(const ElectricField& electricField) const;
    std::size_t GetRegisteredElectricFieldCount() const { return m_ElectricFields.size(); }
    const std::vector<const ElectricField*>& GetRegisteredElectricFields() const { return m_ElectricFields; }

    // MagneticFieldもElectricFieldと同じ非所有Lifetime契約で保持します。
    bool RegisterMagneticField(const MagneticField& magneticField);
    bool UnregisterMagneticField(const MagneticField& magneticField);
    void ClearMagneticFields();

    bool ContainsMagneticField(const MagneticField& magneticField) const;
    std::size_t GetRegisteredMagneticFieldCount() const { return m_MagneticFields.size(); }
    const std::vector<const MagneticField*>& GetRegisteredMagneticFields() const
    {
        return m_MagneticFields;
    }

    // 登録済みElectric Fieldを重ね合わせ、Scene内の荷電RigidBodyへ F=qE として蓄積します。
    // FieldごとにForceを別々に適用せずEを先に合成することで、電場の重ね合わせ原理を明示します。
    void ApplyElectricFieldForces(Scene& scene) const;

    // 登録済み磁場を重ね合わせ、荷電RigidBodyへF=q(v x B)として蓄積します。
    // 現段階では重心のLinearVelocityを点電荷速度として扱い、角速度や有限電荷分布は考慮しません。
    void ApplyMagneticFieldForces(Scene& scene) const;

    // Scene内の点電荷相互作用を選択中のCoulomb Solverで評価し、RigidBodyComponent::Forceへ蓄積します。
    // Automaticではbody数にhysteresisを持たせ、閾値付近でDirect/Barnes-Hutが毎step反転することを防ぎます。
    void ApplyCoulombForces(Scene& scene);

    void SetCoulombSolverSelectionSettings(const CoulombSolverSelectionSettings& settings);
    const CoulombSolverSelectionSettings& GetCoulombSolverSelectionSettings() const
    {
        return m_CoulombSolverSelectionSettings;
    }

    const ElectromagneticStatistics& GetStatistics() const { return m_Statistics; }

    void SetCoulombForceSettings(const CoulombForceSettings& settings)
    {
        m_CoulombForceSettings = settings;
    }

    const CoulombForceSettings& GetCoulombForceSettings() const
    {
        return m_CoulombForceSettings;
    }

private:
    bool ShouldUseBarnesHut(std::size_t bodyCount);

    std::vector<const ElectricField*> m_ElectricFields;
    std::vector<const MagneticField*> m_MagneticFields;
    CoulombForceSettings m_CoulombForceSettings{};
    CoulombSolverSelectionSettings m_CoulombSolverSelectionSettings{};
    BarnesHutCoulombSolver m_BarnesHutCoulombSolver{};
    bool m_AutomaticUsingBarnesHut = false;
    ElectromagneticStatistics m_Statistics{};
};

} // namespace Raven::ph
