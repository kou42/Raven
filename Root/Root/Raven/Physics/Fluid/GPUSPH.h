#pragma once

#include "Raven/Physics/Fluid/FluidParticle.h"
#include "Raven/Physics/Fluid/SPHSettings.h"
#include "Raven/Renderer/RHI/RHICompute.h"

namespace Raven::ph
{
class GPUSPH
{
public:
    enum class PrepareResult { Ready, Unsupported, CapacityExceeded, Failed };

    // EncoderとGPUリソースはOpenGL Contextの破棄より前に解放します。
    PrepareResult Prepare(const Ref<RHIComputeCommandEncoder>& encoder, const RHIComputeCapabilities& capabilities,
        std::span<const FluidParticle> particles, float smoothingRadius, const std::string& shaderSource);
    PrepareResult PrepareFixedStep(const Ref<RHIComputeCommandEncoder>& encoder,
        const RHIComputeCapabilities& capabilities, std::span<const FluidParticle> particles,
        const SPHSettings& settings, float deltaTime, const std::string& shaderSource);
    bool DispatchFixedStep();
    bool DispatchFixedStep(Ref<RHIComputeTimestamp>& timestamp);
    // 描画用SSBOは粒子ごとにvec4(位置・Mass)とvec4(速度)の順で並びます。
    Ref<RHIComputeResource> GetParticleBuffer() const;
    uint32_t GetParticleCount() const;

    struct StepReadback
    {
        Ref<RHIComputeReadback> m_Particles;
        Ref<RHIComputeReadback> m_Dynamics;
    };
    StepReadback RequestStepReadback();
    static bool DecodeStep(std::span<const std::byte> particleBytes, std::span<const std::byte> dynamicsBytes,
        std::span<FluidParticle> particles);

    bool DispatchDensity(Ref<RHIComputeTimestamp>& timestamp);
    Ref<RHIComputeReadback> RequestDensityReadback();
    static bool DecodeDensity(std::span<const std::byte> bytes, std::span<float> densities);

private:
    PrepareResult PrepareInternal(const Ref<RHIComputeCommandEncoder>& encoder,
        const RHIComputeCapabilities& capabilities, std::span<const FluidParticle> particles,
        const SPHSettings& settings, float deltaTime, const std::string& shaderSource);
    bool Dispatch(bool advancesParticles, Ref<RHIComputeTimestamp>* pTimestamp);

    Ref<RHIComputeCommandEncoder> m_Encoder;
    std::array<Ref<RHIComputePipeline>, 5> m_Pipelines;
    std::array<Ref<RHIComputeResource>, 4> m_Resources;
    uint32_t m_ParticleCount = 0u;
    uint32_t m_CellCount = 0u;
    bool m_IsReady = false;
    bool m_HasDensity = false;
    bool m_HasStep = false;
    bool m_CanAdvance = false;
};
} // namespace Raven::ph
