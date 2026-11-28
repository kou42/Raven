#pragma once

#include "Raven/Physics/Fluid/FluidParticle.h"
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
    bool DispatchDensity(Ref<RHIComputeTimestamp>& timestamp);
    Ref<RHIComputeReadback> RequestDensityReadback();
    static bool DecodeDensity(std::span<const std::byte> bytes, std::span<float> densities);

private:
    Ref<RHIComputeCommandEncoder> m_Encoder;
    std::array<Ref<RHIComputePipeline>, 3> m_Pipelines;
    std::array<Ref<RHIComputeResource>, 4> m_Resources;
    uint32_t m_ParticleCount = 0u;
    uint32_t m_CellCount = 0u;
    bool m_IsReady = false;
    bool m_HasDensity = false;
};
} // namespace Raven::ph
