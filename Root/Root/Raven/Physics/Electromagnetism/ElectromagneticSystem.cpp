#include "Raven/Physics/Electromagnetism/ElectromagneticSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Raven/Physics/Electromagnetism/ElectricCharge.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
struct ChargedRigidBodyReference
{
    math::Vec3 Position{};
    RigidBodyComponent* RigidBody = nullptr;
    double ChargeCoulombs = 0.0;
};

bool CanReceiveForce(const RigidBodyComponent* rigidBody)
{
    return rigidBody != nullptr
        && rigidBody->Type == BodyType::Dynamic
        && rigidBody->InverseMass > 0.0f;
}

void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}
}

bool ElectromagneticSystem::RegisterElectricField(const ElectricField& electricField)
{
    if (ContainsElectricField(electricField) == true)
    {
        return false;
    }

    m_ElectricFields.push_back(&electricField);
    return true;
}

bool ElectromagneticSystem::UnregisterElectricField(const ElectricField& electricField)
{
    const auto iterator = std::find(m_ElectricFields.begin(), m_ElectricFields.end(), &electricField);
    if (iterator == m_ElectricFields.end())
    {
        return false;
    }

    m_ElectricFields.erase(iterator);
    return true;
}

void ElectromagneticSystem::ClearElectricFields()
{
    // ElectricFieldの所有権は呼び出し側に残るため、Registryの非所有参照だけを解除します。
    m_ElectricFields.clear();
}

bool ElectromagneticSystem::ContainsElectricField(const ElectricField& electricField) const
{
    return std::find(m_ElectricFields.begin(), m_ElectricFields.end(), &electricField) != m_ElectricFields.end();
}

bool ElectromagneticSystem::RegisterMagneticField(const MagneticField& magneticField)
{
    if (ContainsMagneticField(magneticField) == true)
    {
        return false;
    }

    m_MagneticFields.push_back(&magneticField);
    return true;
}

bool ElectromagneticSystem::UnregisterMagneticField(const MagneticField& magneticField)
{
    const auto iterator = std::find(m_MagneticFields.begin(), m_MagneticFields.end(), &magneticField);
    if (iterator == m_MagneticFields.end())
    {
        return false;
    }

    m_MagneticFields.erase(iterator);
    return true;
}

void ElectromagneticSystem::ClearMagneticFields()
{
    // MagneticFieldの所有権は呼び出し側に残るため、Registryの非所有参照だけを解除します。
    m_MagneticFields.clear();
}

bool ElectromagneticSystem::ContainsMagneticField(const MagneticField& magneticField) const
{
    return std::find(m_MagneticFields.begin(), m_MagneticFields.end(), &magneticField)
        != m_MagneticFields.end();
}

void ElectromagneticSystem::ApplyElectricFieldForces(Scene& scene) const
{
    if (m_ElectricFields.empty() == true)
    {
        return;
    }

    for (auto [entity, transform, rigidBody, collider, electricCharge]
        : scene.View<TransformComponent, RigidBodyComponent, ColliderComponent, ElectricChargeComponent>())
    {
        static_cast<void>(entity);
        static_cast<void>(collider);

        if (electricCharge.IsEnabled == false
            || electricCharge.ChargeCoulombs == 0.0
            || CanReceiveForce(&rigidBody) == false)
        {
            continue;
        }

        // Maxwell方程式が線形である範囲では電場は重ね合わせ可能です。
        // 先に全FieldのEを合成してからF=qEを1度だけ評価し、Force経路を単純に保ちます。
        math::Vec3 combinedElectricField{};
        for (const ElectricField* electricField : m_ElectricFields)
        {
            if (electricField == nullptr)
            {
                continue;
            }
            combinedElectricField += electricField->Evaluate(transform.Position);
        }

        const math::Vec3 force = ComputeElectricForce(electricCharge.ChargeCoulombs, combinedElectricField);
        rigidBody.Force += force;
        if (force.LengthSq() > 0.0f)
        {
            WakeRigidBody(rigidBody);
        }
    }
}

void ElectromagneticSystem::ApplyMagneticFieldForces(Scene& scene) const
{
    if (m_MagneticFields.empty() == true)
    {
        return;
    }

    for (auto [entity, transform, rigidBody, collider, electricCharge]
        : scene.View<TransformComponent, RigidBodyComponent, ColliderComponent, ElectricChargeComponent>())
    {
        static_cast<void>(entity);
        static_cast<void>(collider);

        if (electricCharge.IsEnabled == false
            || electricCharge.ChargeCoulombs == 0.0
            || CanReceiveForce(&rigidBody) == false)
        {
            continue;
        }

        // 磁場も線形に重ね合わせてからローレンツ力へ変換します。
        // Field評価には現在位置、外積にはfixed-step開始時点のLinearVelocityを使用します。
        math::Vec3 combinedMagneticField{};
        for (const MagneticField* magneticField : m_MagneticFields)
        {
            if (magneticField == nullptr)
            {
                continue;
            }
            combinedMagneticField += magneticField->Evaluate(transform.Position);
        }

        const math::Vec3 force = ComputeMagneticForce(
            electricCharge.ChargeCoulombs,
            rigidBody.LinearVelocity,
            combinedMagneticField);
        rigidBody.Force += force;
        if (force.LengthSq() > 0.0f)
        {
            WakeRigidBody(rigidBody);
        }
    }
}

void ElectromagneticSystem::ApplyCoulombForces(Scene& scene)
{
    m_Statistics.Clear();
    std::vector<ChargedRigidBodyReference> chargedBodies;

    using Clock = std::chrono::steady_clock;
    const auto collectionBegin = Clock::now();
    for (auto [entity, transform, rigidBody, collider, electricCharge]
        : scene.View<TransformComponent, RigidBodyComponent, ColliderComponent, ElectricChargeComponent>())
    {
        static_cast<void>(entity);
        static_cast<void>(collider);

        if (electricCharge.IsEnabled == false || electricCharge.ChargeCoulombs == 0.0)
        {
            continue;
        }

        ChargedRigidBodyReference reference{};
        reference.Position = transform.Position;
        reference.RigidBody = &rigidBody;
        reference.ChargeCoulombs = electricCharge.ChargeCoulombs;
        chargedBodies.push_back(reference);
    }
    const auto collectionEnd = Clock::now();
    m_Statistics.ActiveChargedBodyCount = static_cast<std::uint64_t>(chargedBodies.size());
    m_Statistics.CoulombStateCollectionTimeMs =
        std::chrono::duration<double, std::milli>(collectionEnd - collectionBegin).count();

    const bool useBarnesHut = ShouldUseBarnesHut(chargedBodies.size());
    std::vector<math::Vec3> forces(chargedBodies.size(), math::Vec3{});

    const auto solveBegin = Clock::now();
    if (useBarnesHut == true)
    {
        std::vector<CoulombOctreeBody> solverBodies;
        solverBodies.reserve(chargedBodies.size());
        for (const ChargedRigidBodyReference& chargedBody : chargedBodies)
        {
            CoulombOctreeBody solverBody{};
            solverBody.Position = {
                static_cast<double>(chargedBody.Position.x),
                static_cast<double>(chargedBody.Position.y),
                static_cast<double>(chargedBody.Position.z)
            };
            solverBody.ChargeCoulombs = chargedBody.ChargeCoulombs;
            solverBodies.push_back(solverBody);
        }

        m_BarnesHutCoulombSolver.SetTheta(m_CoulombSolverSelectionSettings.BarnesHutTheta);
        CoulombBarnesHutStatistics solverStatistics{};
        m_BarnesHutCoulombSolver.ComputeForces(
            solverBodies,
            m_CoulombForceSettings,
            forces,
            &solverStatistics);
        m_Statistics.CoulombSolver = CoulombSolverKind::BarnesHut;
        m_Statistics.CoulombPairCandidateCount = solverStatistics.PairCandidateCount;
        m_Statistics.CoulombForceEvaluationCount = solverStatistics.ForceEvaluationCount;
        m_Statistics.CoulombVisitedNodeCount = solverStatistics.VisitedNodeCount;
        m_Statistics.CoulombAcceptedAggregateNodeCount =
            solverStatistics.AcceptedAggregateNodeCount;
        m_Statistics.CoulombTreeBuildTimeMs = solverStatistics.TreeBuildTimeMs;
    }
    else
    {
        m_Statistics.CoulombSolver = CoulombSolverKind::Direct;
        // Direct法は各pairを1度だけ評価し、作用反作用を同じ計算結果から同時に加えます。
        // Barnes-Hutとの比較Referenceでもあるため、この対称な経路は維持します。
        for (std::size_t i = 0u; i < chargedBodies.size(); ++i)
        {
            for (std::size_t j = i + 1; j < chargedBodies.size(); ++j)
            {
                const ChargedRigidBodyReference& a = chargedBodies[i];
                const ChargedRigidBodyReference& b = chargedBodies[j];
                ++m_Statistics.CoulombPairCandidateCount;

                const math::Vec3 forceOnB = ComputeCoulombForce(
                    a.Position,
                    a.ChargeCoulombs,
                    b.Position,
                    b.ChargeCoulombs,
                    m_CoulombForceSettings);
                forces[i] -= forceOnB;
                forces[j] += forceOnB;
                if (forceOnB.LengthSq() > 0.0f)
                {
                    ++m_Statistics.CoulombForceEvaluationCount;
                }
            }
        }
    }
    const auto solveEnd = Clock::now();
    m_Statistics.CoulombSolveTimeMs =
        std::chrono::duration<double, std::milli>(solveEnd - solveBegin).count();

    const auto feedbackBegin = Clock::now();
    for (std::size_t i = 0u; i < chargedBodies.size(); ++i)
    {
        RigidBodyComponent* rigidBody = chargedBodies[i].RigidBody;
        if (CanReceiveForce(rigidBody) == false)
        {
            continue;
        }

        const math::Vec3& force = forces[i];
        if (std::isfinite(force.x) == false
            || std::isfinite(force.y) == false
            || std::isfinite(force.z) == false)
        {
            continue;
        }

        rigidBody->Force += force;
        if (force.LengthSq() > 0.0f)
        {
            WakeRigidBody(*rigidBody);
        }
    }
    const auto feedbackEnd = Clock::now();
    m_Statistics.CoulombForceFeedbackTimeMs =
        std::chrono::duration<double, std::milli>(feedbackEnd - feedbackBegin).count();
}

void ElectromagneticSystem::SetCoulombSolverSelectionSettings(
    const CoulombSolverSelectionSettings& settings)
{
    m_CoulombSolverSelectionSettings = settings;
    if (std::isfinite(m_CoulombSolverSelectionSettings.BarnesHutTheta) == false
        || m_CoulombSolverSelectionSettings.BarnesHutTheta <= 0.0)
    {
        m_CoulombSolverSelectionSettings.BarnesHutTheta = 0.25;
    }
    if (m_CoulombSolverSelectionSettings.DirectBodyThreshold
        > m_CoulombSolverSelectionSettings.BarnesHutBodyThreshold)
    {
        m_CoulombSolverSelectionSettings.DirectBodyThreshold =
            m_CoulombSolverSelectionSettings.BarnesHutBodyThreshold;
    }

    m_AutomaticUsingBarnesHut = false;
}

bool ElectromagneticSystem::ShouldUseBarnesHut(std::size_t bodyCount)
{
    if (m_CoulombSolverSelectionSettings.Mode == CoulombSolverMode::BarnesHut)
    {
        return true;
    }
    if (m_CoulombSolverSelectionSettings.Mode == CoulombSolverMode::Direct)
    {
        return false;
    }

    if (m_AutomaticUsingBarnesHut == true)
    {
        m_AutomaticUsingBarnesHut =
            bodyCount >= m_CoulombSolverSelectionSettings.DirectBodyThreshold;
    }
    else
    {
        m_AutomaticUsingBarnesHut =
            bodyCount >= m_CoulombSolverSelectionSettings.BarnesHutBodyThreshold;
    }
    return m_AutomaticUsingBarnesHut;
}

} // namespace Raven::ph
