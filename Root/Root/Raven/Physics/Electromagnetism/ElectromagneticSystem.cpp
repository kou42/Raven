#include "Raven/Physics/Electromagnetism/ElectromagneticSystem.h"

#include <algorithm>
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
    // MagneticFieldも非所有参照だけを保持するため、Field本体は破棄しません。
    m_MagneticFields.clear();
}

bool ElectromagneticSystem::ContainsMagneticField(const MagneticField& magneticField) const
{
    return std::find(m_MagneticFields.begin(), m_MagneticFields.end(), &magneticField) != m_MagneticFields.end();
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

        // Bも線形に重ね合わせ可能なので、全Fieldを合成してからLorentz Forceを1度だけ評価します。
        // 磁気力はvに直交するため理想的には仕事をしませんが、Force accumulatorへ通常の外力として渡し、
        // 積分方法はRigidBody側へ一元化します。
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

void ElectromagneticSystem::ApplyCoulombForces(Scene& scene) const
{
    std::vector<ChargedRigidBodyReference> chargedBodies;

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

    // 各ペアを1度だけ評価し、作用反作用を同じ計算結果から同時に加えます。
    // 個別にA->B / B->Aを再計算しないことで、丸め誤差による運動量の非対称を避けます。
    for (std::size_t i = 0; i < chargedBodies.size(); ++i)
    {
        for (std::size_t j = i + 1; j < chargedBodies.size(); ++j)
        {
            ChargedRigidBodyReference& a = chargedBodies[i];
            ChargedRigidBodyReference& b = chargedBodies[j];

            const math::Vec3 forceOnB = ComputeCoulombForce(
                a.Position,
                a.ChargeCoulombs,
                b.Position,
                b.ChargeCoulombs,
                m_CoulombForceSettings);

            if (CanReceiveForce(a.RigidBody))
            {
                a.RigidBody->Force -= forceOnB;
                if (forceOnB.LengthSq() > 0.0f)
                {
                    WakeRigidBody(*a.RigidBody);
                }
            }

            if (CanReceiveForce(b.RigidBody))
            {
                b.RigidBody->Force += forceOnB;
                if (forceOnB.LengthSq() > 0.0f)
                {
                    WakeRigidBody(*b.RigidBody);
                }
            }
        }
    }
}

} // namespace Raven::ph
