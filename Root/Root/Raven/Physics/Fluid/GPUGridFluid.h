#pragma once

#include "Raven/Physics/Field/DenseSDF.h"
#include "Raven/Physics/Fluid/FluidSimulationParticipant.h"

namespace Raven::ph
{

struct GPUGridFluidSettings
{
    float FixedStep = 1.0f / 60.0f;
    float Viscosity = 0.0f;
    float ParticleRadius = 0.02f;
    math::Vec3 InitialVelocity{};
    math::Vec3 Acceleration{};
};

// 2 vec4のレイアウトをCompute / 描画 / 診断で共有します。FluidParticleとは独立です。
struct GPUGridFluidParticle
{
    std::array<float, 4> PositionAndContacts{};
    std::array<float, 4> VelocityAndFlags{};
};

class GPUGridFluid final : public FluidSimulationParticipant
{
public:
    bool Prepare(Ref<RHIComputeCommandEncoder> encoder, RHIComputePipelineSpecification gridShader,
        RHIComputePipelineSpecification particleShader, const DenseSDFGenerator& signedBoundary,
        const GPUGridFluidSettings& settings,
        std::span<const math::Vec3> particlePositions);
    void SimulateFluid(float fixedDeltaTime) override;

    Ref<RHIComputeReadback> RequestParticleReadback();
    // 同じOpenGL Contextで呼ぶ。Pending中は前回の完了データを保持し、待機しない。
    bool UpdateVelocityReadback();
    const DenseSDFGrid& GetVelocityGrid() const;
    std::span<const math::Vec3> GetCompletedVelocities() const;
    std::uint64_t GetCompletedVelocityGeneration() const;
    std::uint64_t GetVelocityReadbackCount() const;

    const Ref<RHIComputeResource>& GetParticleBuffer() const;
    std::uint64_t GetStepGeneration() const;
    std::uint64_t GetReadbackCount() const;
    bool IsReady() const;

private:
    Ref<RHIComputeCommandEncoder> m_Encoder;
    Ref<RHIComputePipeline> m_GridPipeline;
    Ref<RHIComputePipeline> m_ParticlePipeline;
    Ref<RHIComputeResource> m_Parameters;
    Ref<RHIComputeResource> m_Boundary;
    std::array<Ref<RHIComputeResource>, 2> m_Velocity;
    Ref<RHIComputeResource> m_Particles;
    std::uint32_t m_GridGroups = 0;
    std::uint32_t m_ParticleGroups = 0;
    std::size_t m_ParticleCount = 0;
    std::uint32_t m_CurrentVelocity = 0;
    float m_FixedStep = 0.0f;
    std::uint64_t m_StepGeneration = 0;
    std::uint64_t m_ReadbackCount = 0;
    DenseSDFGrid m_VelocityGrid{};
    Ref<RHIComputeReadback> m_VelocityTicket;
    std::vector<math::Vec3> m_CompletedVelocities;
    std::uint64_t m_TicketGeneration = 0;
    std::uint64_t m_CompletedVelocityGeneration = 0;
    std::uint64_t m_VelocityReadbackCount = 0;
    bool m_Ready = false;
};
} // namespace Raven::ph
