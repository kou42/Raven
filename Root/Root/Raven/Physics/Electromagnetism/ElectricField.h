#pragma once

#include "Raven/Physics/Electromagnetism/CoulombForce.h"
#include "Raven/Physics/Field/PhysicsField.h"

namespace Raven::ph
{

// Electric field abstraction evaluated in world space.
// The value returned by Evaluate is an electric-field vector, not a force.
class ElectricField : public VectorField
{
public:
    ~ElectricField() override = default;
};

class UniformElectricField final : public ElectricField
{
public:
    UniformElectricField() = default;
    explicit UniformElectricField(const math::Vec3& field) : m_Field(field) {}

    math::Vec3 Evaluate(const math::Vec3& worldPosition) const override;

    void SetField(const math::Vec3& field) { m_Field = field; }
    const math::Vec3& GetField() const { return m_Field; }

private:
    math::Vec3 m_Field{};
};

class PointChargeElectricField final : public ElectricField
{
public:
    PointChargeElectricField() = default;
    PointChargeElectricField(
        const math::Vec3& sourcePosition,
        double sourceChargeCoulombs,
        const CoulombForceSettings& settings = CoulombForceSettings{});

    math::Vec3 Evaluate(const math::Vec3& worldPosition) const override;

    void SetSourcePosition(const math::Vec3& position) { m_SourcePosition = position; }
    const math::Vec3& GetSourcePosition() const { return m_SourcePosition; }

    void SetSourceChargeCoulombs(double chargeCoulombs) { m_SourceChargeCoulombs = chargeCoulombs; }
    double GetSourceChargeCoulombs() const { return m_SourceChargeCoulombs; }

    void SetSettings(const CoulombForceSettings& settings) { m_Settings = settings; }
    const CoulombForceSettings& GetSettings() const { return m_Settings; }

private:
    math::Vec3 m_SourcePosition{};
    double m_SourceChargeCoulombs = 0.0;
    CoulombForceSettings m_Settings{};
};

math::Vec3 ComputeElectricForce(double chargeCoulombs, const math::Vec3& electricField);

} // namespace Raven::ph
