#pragma once

#include "Raven/Physics/Fluid/FluidCouplingBinding.h"

namespace Raven::ph
{

// ============================================================================
// Fluid Coupling Debug Preset
// ============================================================================
// Fluid/RigidBody Couplingの各効果を比較しやすくするためのDebug Demo向けPresetです。
// Solver本体へ「水」「高粘性流体」といった表現上の概念を持ち込まず、Coupling係数の
// 組み合わせだけをDebug層で管理します。
enum class FluidCouplingPreset
{
    Water,
    HeavyFluid,
    HighDrag,
    CouplingOff
};

struct FluidCouplingPresetSettings
{
    float ParticleRadius = 0.12f;
    float Restitution = 0.05f;
    float DragCoefficient = 0.15f;
    float PressureReactionCoefficient = 1.0f;
    float BuoyancyCoefficient = 1.0f;
    bool StaticColliderCouplingEnabled = true;
    bool RigidBodyCouplingEnabled = true;
};

inline FluidCouplingPresetSettings GetFluidCouplingPresetSettings(FluidCouplingPreset preset)
{
    FluidCouplingPresetSettings settings{};

    switch (preset)
    {
        case FluidCouplingPreset::Water:
        {
            // 現在のSPH Demo既定値です。Preset導入前と同じ挙動を基準ケースとして維持します。
            break;
        }
        case FluidCouplingPreset::HeavyFluid:
        {
            // 圧力反作用と浮力を強め、同じRigidBodyに対するCoupling差を観察しやすくします。
            settings.DragCoefficient = 0.30f;
            settings.PressureReactionCoefficient = 2.0f;
            settings.BuoyancyCoefficient = 2.0f;
            break;
        }
        case FluidCouplingPreset::HighDrag:
        {
            // 浮力・圧力はWaterと揃え、接線方向の速度交換だけを強調します。
            settings.DragCoefficient = 1.50f;
            break;
        }
        case FluidCouplingPreset::CouplingOff:
        {
            // Particle Simulation自体は継続し、Scene Collider/RigidBodyとのCouplingだけを停止します。
            settings.StaticColliderCouplingEnabled = false;
            settings.RigidBodyCouplingEnabled = false;
            break;
        }
    }

    return settings;
}

inline void ApplyFluidCouplingPreset(FluidCouplingBinding& binding, FluidCouplingPreset preset)
{
    const FluidCouplingPresetSettings settings = GetFluidCouplingPresetSettings(preset);

    binding.StaticColliderCouplingEnabled = settings.StaticColliderCouplingEnabled;
    binding.RigidBodyCouplingEnabled = settings.RigidBodyCouplingEnabled;

    // Static/Dynamic Couplingは同じParticle表面と反発特性を扱うため、Preset適用時は同期します。
    binding.StaticColliderSettings.ParticleRadius = settings.ParticleRadius;
    binding.StaticColliderSettings.Restitution = settings.Restitution;

    binding.RigidBodySettings.ParticleRadius = settings.ParticleRadius;
    binding.RigidBodySettings.Restitution = settings.Restitution;
    binding.RigidBodySettings.DragCoefficient = settings.DragCoefficient;
    binding.RigidBodySettings.PressureReactionCoefficient = settings.PressureReactionCoefficient;
    binding.RigidBodySettings.BuoyancyCoefficient = settings.BuoyancyCoefficient;
}

} // namespace Raven::ph
