#include "Raven/Physics/Fluid/GPUSPH.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "Raven/Math/Math.h"

namespace Raven::ph
{
namespace
{
constexpr uint32_t GroupSize = 64u;
constexpr uint32_t MaximumCells = 1024u * 1024u;
constexpr float MaximumAbsoluteCellCoordinate = 1.0e6f;

struct Parameters
{
    std::array<int32_t, 4> m_Origin;
    std::array<uint32_t, 4> m_Dimensions;
    std::array<float, 4> m_Kernel;
};
static_assert(sizeof(Parameters) == 48u);

uint32_t GroupCount(uint32_t count)
{
    return count / GroupSize + (count % GroupSize != 0u ? 1u : 0u);
}
}

GPUSPH::PrepareResult GPUSPH::Prepare(const Ref<RHIComputeCommandEncoder>& encoder,
    const RHIComputeCapabilities& capabilities, std::span<const FluidParticle> particles,
    float smoothingRadius, const std::string& shaderSource)
{
    m_IsReady = false;
    m_HasDensity = false;
    m_Pipelines = {};
    m_Resources = {};
    m_Encoder = encoder;
    m_ParticleCount = 0u;
    m_CellCount = 0u;
    if (encoder == nullptr || capabilities.Supported == false || capabilities.MaxStorageBufferSlots < 4u
        || capabilities.MaxLocalSize[0] < GroupSize || capabilities.MaxInvocations < GroupSize)
    {
        return PrepareResult::Unsupported;
    }
    if (particles.empty() == true)
    {
        m_IsReady = true;
        return PrepareResult::Ready;
    }
    if (particles.size() > std::numeric_limits<int32_t>::max()
        || shaderSource.starts_with("#version") == false)
    {
        return PrepareResult::Failed;
    }

    const float inverseCellSize = 0.5f / smoothingRadius;
    const float hSquared = smoothingRadius * smoothingRadius;
    const float h3 = hSquared * smoothingRadius;
    const float poly6 = 315.0f / (64.0f * math::Pi * (h3 * h3 * h3));
    if (smoothingRadius < 1.0e-4f || std::isfinite(inverseCellSize) == false
        || std::isfinite(poly6) == false || poly6 <= 0.0f)
    {
        return PrepareResult::Failed;
    }

    using ParticleWord = std::array<float, 4>;
    if (particles.size() * sizeof(ParticleWord) > capabilities.MaxStorageBufferBytes
        || GroupCount(static_cast<uint32_t>(particles.size())) > capabilities.MaxGroups[0])
    {
        return PrepareResult::CapacityExceeded;
    }

    std::array<int32_t, 3> minimum;
    std::array<int32_t, 3> maximum;
    minimum.fill(std::numeric_limits<int32_t>::max());
    maximum.fill(std::numeric_limits<int32_t>::min());
    for (const FluidParticle& particle : particles)
    {
        const std::array coordinates{particle.Position.x, particle.Position.y, particle.Position.z};
        if (std::isfinite(particle.Mass) == false)
        {
            return PrepareResult::Failed;
        }
        for (std::size_t axis = 0; axis < coordinates.size(); ++axis)
        {
            const float coordinate = std::floor(coordinates[axis] * inverseCellSize);
            if ((std::abs(coordinate) <= MaximumAbsoluteCellCoordinate) == false)
            {
                return PrepareResult::CapacityExceeded;
            }
            const auto cell = static_cast<int32_t>(coordinate);
            minimum[axis] = std::min(minimum[axis], cell);
            maximum[axis] = std::max(maximum[axis], cell);
        }
    }

    Parameters parameters{};
    uint32_t cellCount = 1u;
    for (std::size_t axis = 0; axis < minimum.size(); ++axis)
    {
        // 境界の乗算がCPUとGPUで丸め分かれしても収まるよう両端を1セル広げます。
        parameters.m_Origin[axis] = minimum[axis] - 1;
        const auto dimension = static_cast<uint32_t>(maximum[axis] - minimum[axis] + 3);
        parameters.m_Dimensions[axis] = dimension;
        if (dimension > MaximumCells / cellCount)
        {
            return PrepareResult::CapacityExceeded;
        }
        cellCount *= dimension;
    }
    const auto particleCount = static_cast<uint32_t>(particles.size());
    const auto gridBytes = (static_cast<std::size_t>(cellCount) + particleCount) * sizeof(int32_t);
    if (gridBytes > capabilities.MaxStorageBufferBytes || GroupCount(cellCount) > capabilities.MaxGroups[0])
    {
        return PrepareResult::CapacityExceeded;
    }
    parameters.m_Dimensions[3] = particleCount;
    parameters.m_Kernel = {inverseCellSize, hSquared, poly6, 0.0f};

    using Type = RHIComputeBindingType;
    using Access = RHIComputeAccess;
    const RHIComputeBindingLayout parameterLayout{0u, Type::StorageBuffer, Access::Read};
    const RHIComputeBindingLayout particleLayout{1u, Type::StorageBuffer, Access::Read};
    const RHIComputeBindingLayout gridRead{2u, Type::StorageBuffer, Access::Read};
    const RHIComputeBindingLayout gridWrite{2u, Type::StorageBuffer, Access::ReadWrite};
    const RHIComputeBindingLayout resultWrite{3u, Type::StorageBuffer, Access::ReadWrite};
    const std::array<std::vector<RHIComputeBindingLayout>, 3> layouts{{
        {parameterLayout, gridWrite, resultWrite},
        {parameterLayout, particleLayout, gridWrite, resultWrite},
        {parameterLayout, particleLayout, gridRead, resultWrite}
    }};
    constexpr std::array passes{"#define RAVEN_SPH_CLEAR\n", "#define RAVEN_SPH_BUILD\n", "#define RAVEN_SPH_DENSITY\n"};
    const auto versionEnd = shaderSource.find('\n');
    if (versionEnd == std::string::npos)
    {
        return PrepareResult::Failed;
    }
    for (std::size_t index = 0; index < passes.size(); ++index)
    {
        RHIComputePipelineSpecification spec;
        spec.OpenGLSource = shaderSource;
        spec.OpenGLSource.insert(versionEnd + 1u, passes[index]);
        spec.LocalSize = {GroupSize, 1u, 1u};
        spec.Layout = layouts[index];
        m_Pipelines[index] = encoder->CreatePipeline(spec);
        if (m_Pipelines[index] == nullptr)
        {
            return PrepareResult::Failed;
        }
    }

    std::vector<ParticleWord> words;
    words.reserve(particles.size());
    for (const FluidParticle& particle : particles)
    {
        words.push_back({particle.Position.x, particle.Position.y, particle.Position.z, particle.Mass});
    }
    m_Resources = {encoder->CreateStorageBuffer(sizeof(parameters), &parameters),
        encoder->CreateStorageBuffer(words.size() * sizeof(ParticleWord), words.data()),
        encoder->CreateStorageBuffer(gridBytes, nullptr),
        encoder->CreateStorageBuffer((particles.size() + 1u) * sizeof(uint32_t), nullptr)};
    m_IsReady = std::all_of(m_Resources.begin(), m_Resources.end(), [](const auto& buffer)
    {
        return buffer != nullptr;
    });
    m_CellCount = cellCount;
    m_ParticleCount = particleCount;
    return m_IsReady ? PrepareResult::Ready : PrepareResult::Failed;
}

bool GPUSPH::DispatchDensity(Ref<RHIComputeTimestamp>& timestamp)
{
    timestamp.reset();
    m_HasDensity = false;
    if (m_IsReady == false)
    {
        return false;
    }
    if (m_ParticleCount == 0u)
    {
        return true;
    }
    m_HasDensity = m_Encoder->MeasureGpuWork([this](RHIComputeCommandEncoder& encoder)
    {
        const std::array<RHIComputeBinding, 3> clear{{{0u, m_Resources[0]}, {2u, m_Resources[2]}, {3u, m_Resources[3]}}};
        const std::array<RHIComputeBinding, 4> particle{{{0u, m_Resources[0]}, {1u, m_Resources[1]},
            {2u, m_Resources[2]}, {3u, m_Resources[3]}}};
        const RHIComputeExtent particleGroups{GroupCount(m_ParticleCount), 1u, 1u};
        return encoder.Dispatch(m_Pipelines[0], clear, {GroupCount(m_CellCount), 1u, 1u})
            && encoder.Barrier(RHIComputeConsumer::Storage)
            && encoder.Dispatch(m_Pipelines[1], particle, particleGroups)
            && encoder.Barrier(RHIComputeConsumer::Storage)
            && encoder.Dispatch(m_Pipelines[2], particle, particleGroups)
            && encoder.Barrier(RHIComputeConsumer::Storage);
    }, timestamp);
    return m_HasDensity;
}

Ref<RHIComputeReadback> GPUSPH::RequestDensityReadback()
{
    if (m_HasDensity == false || m_Encoder->Barrier(RHIComputeConsumer::BufferReadback) == false)
    {
        return {};
    }
    return m_Encoder->RequestBufferReadback(m_Resources[3], 0u,
        (static_cast<std::size_t>(m_ParticleCount) + 1u) * sizeof(uint32_t));
}

bool GPUSPH::DecodeDensity(std::span<const std::byte> bytes, std::span<float> densities)
{
    static_assert(sizeof(float) == sizeof(uint32_t));
    if (bytes.size() != (densities.size() + 1u) * sizeof(uint32_t))
    {
        return false;
    }
    uint32_t status = 0u;
    std::memcpy(&status, bytes.data(), sizeof(status));
    if (status != 0u)
    {
        return false;
    }
    if (densities.empty() == false)
    {
        std::memcpy(densities.data(), bytes.data() + sizeof(status), densities.size_bytes());
    }
    return true;
}
} // namespace Raven::ph
