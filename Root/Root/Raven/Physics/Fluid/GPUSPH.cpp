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
constexpr std::size_t DynamicsWordsPerParticle = 5u;
struct ParticleWord
{
    std::array<float, 4> m_PositionMass;
    std::array<float, 4> m_Velocity;
};
static_assert(sizeof(ParticleWord) == 32u);
static_assert(math::Epsilon == 1.0e-6f);

constexpr float MaximumAbsoluteCellCoordinate = 1.0e6f;

struct Parameters
{
    std::array<int32_t, 4> m_Origin;
    std::array<uint32_t, 4> m_Dimensions;
    std::array<float, 4> m_Kernel;
    std::array<float, 4> m_Dynamics;
    std::array<float, 4> m_GravityViscosity;
    std::array<float, 4> m_BoundaryMin;
    std::array<float, 4> m_BoundaryMax;
};
static_assert(sizeof(Parameters) == 112u);

uint32_t GroupCount(uint32_t count)
{
    return count / GroupSize + (count % GroupSize != 0u ? 1u : 0u);
}
}

GPUSPH::PrepareResult GPUSPH::Prepare(const Ref<RHIComputeCommandEncoder>& encoder,
    const RHIComputeCapabilities& capabilities, std::span<const FluidParticle> particles,
    float smoothingRadius, const std::string& shaderSource)
{
    SPHSettings settings;
    settings.SmoothingRadius = smoothingRadius;
    return PrepareInternal(encoder, capabilities, particles, settings, 0.0f, shaderSource);
}

GPUSPH::PrepareResult GPUSPH::PrepareFixedStep(const Ref<RHIComputeCommandEncoder>& encoder,
    const RHIComputeCapabilities& capabilities, std::span<const FluidParticle> particles,
    const SPHSettings& settings, float deltaTime, const std::string& shaderSource)
{
    if (std::isfinite(deltaTime) == false || deltaTime <= 0.0f)
    {
        m_IsReady = false;
        m_HasDensity = false;
        m_HasStep = false;
        return PrepareResult::Failed;
    }
    return PrepareInternal(encoder, capabilities, particles, settings, deltaTime, shaderSource);
}

GPUSPH::PrepareResult GPUSPH::PrepareInternal(const Ref<RHIComputeCommandEncoder>& encoder,
    const RHIComputeCapabilities& capabilities, std::span<const FluidParticle> particles,
    const SPHSettings& settings, float deltaTime, const std::string& shaderSource)
{
    const float smoothingRadius = settings.SmoothingRadius;
    m_CanAdvance = deltaTime > 0.0f;
    m_HasStep = false;
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

    if (m_CanAdvance && particles.size() > (std::numeric_limits<uint32_t>::max() - 1u) / DynamicsWordsPerParticle)
    {
        return PrepareResult::CapacityExceeded;
    }
    const auto resultWords = (m_CanAdvance ? DynamicsWordsPerParticle : 1u) * particles.size() + 1u;
    if (resultWords * sizeof(uint32_t) > capabilities.MaxStorageBufferBytes
        || particles.size() * sizeof(ParticleWord) > capabilities.MaxStorageBufferBytes
        || GroupCount(static_cast<uint32_t>(particles.size())) > capabilities.MaxGroups[0])
    {
        return PrepareResult::CapacityExceeded;
    }

    std::array<int32_t, 3> minimum;
    std::array<int32_t, 3> maximum;
    minimum.fill(std::numeric_limits<int32_t>::max());
    maximum.fill(std::numeric_limits<int32_t>::min());
    const auto includePosition = [&](const math::Vec3& position)
    {
        const std::array coordinates{position.x, position.y, position.z};
        for (std::size_t axis = 0; axis < coordinates.size(); ++axis)
        {
            const float coordinate = std::floor(coordinates[axis] * inverseCellSize);
            if ((std::abs(coordinate) <= MaximumAbsoluteCellCoordinate) == false)
            {
                return false;
            }
            const auto cell = static_cast<int32_t>(coordinate);
            minimum[axis] = std::min(minimum[axis], cell);
            maximum[axis] = std::max(maximum[axis], cell);
        }
        return true;
    };
    for (const FluidParticle& particle : particles)
    {
        if (std::isfinite(particle.Mass) == false)
        {
            return PrepareResult::Failed;
        }
        if (includePosition(particle.Position) == false)
        {
            return PrepareResult::CapacityExceeded;
        }
    }

    Parameters parameters{};
    if (m_CanAdvance == true)
    {
        const float spiky = -45.0f / (math::Pi * (h3 * h3));
        parameters.m_Dynamics = {std::max(0.0f, settings.RestDensity),
            std::max(0.0f, settings.PressureStiffness), deltaTime, spiky};
        parameters.m_GravityViscosity = {settings.Gravity.x, settings.Gravity.y, settings.Gravity.z,
            std::max(0.0f, settings.Viscosity)};
        if (settings.BoundaryEnabled == true)
        {
            const auto low = settings.BoundaryMinimum;
            const auto high = settings.BoundaryMaximum;
            const float radius = std::max(0.0f, settings.BoundaryParticleRadius);
            const auto axisBounds = [radius](float a, float b)
            {
                const float minimum = std::min(a, b) + radius;
                const float maximum = std::max(a, b) - radius;
                const float center = (a + b) * 0.5f;
                return minimum > maximum ? std::array{center, center} : std::array{minimum, maximum};
            };
            const auto x = axisBounds(low.x, high.x);
            const auto y = axisBounds(low.y, high.y);
            const auto z = axisBounds(low.z, high.z);
            parameters.m_BoundaryMin = {x[0], y[0], z[0], 1.0f};
            parameters.m_BoundaryMax = {x[1], y[1], z[1], std::clamp(settings.BoundaryRestitution, 0.0f, 1.0f)};
            if (includePosition({x[0], y[0], z[0]}) == false || includePosition({x[1], y[1], z[1]}) == false)
            {
                return PrepareResult::CapacityExceeded;
            }
        }
    }
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
    parameters.m_Kernel = {inverseCellSize, hSquared, poly6, smoothingRadius};

    using Type = RHIComputeBindingType;
    using Access = RHIComputeAccess;
    const RHIComputeBindingLayout parameterLayout{0u, Type::StorageBuffer, Access::Read};
    const RHIComputeBindingLayout particleLayout{1u, Type::StorageBuffer, Access::Read};
    const RHIComputeBindingLayout gridRead{2u, Type::StorageBuffer, Access::Read};
    const RHIComputeBindingLayout gridWrite{2u, Type::StorageBuffer, Access::ReadWrite};
    const RHIComputeBindingLayout resultWrite{3u, Type::StorageBuffer, Access::ReadWrite};
    const std::array<std::vector<RHIComputeBindingLayout>, 5> layouts{{
        {parameterLayout, gridWrite},
        {parameterLayout, particleLayout, gridWrite, resultWrite},
        {parameterLayout, particleLayout, gridRead, resultWrite},
        {parameterLayout, particleLayout, gridRead, resultWrite},
        {parameterLayout, {1u, Type::StorageBuffer, Access::ReadWrite}, resultWrite}
    }};
    constexpr std::array passes{"#define RAVEN_SPH_CLEAR\n", "#define RAVEN_SPH_BUILD\n", "#define RAVEN_SPH_DENSITY\n",
        "#define RAVEN_SPH_FORCE\n", "#define RAVEN_SPH_INTEGRATE\n"};
    const auto versionEnd = shaderSource.find('\n');
    if (versionEnd == std::string::npos)
    {
        return PrepareResult::Failed;
    }
    for (std::size_t index = 0; index < (m_CanAdvance ? passes.size() : 3u); ++index)
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
        words.push_back({{particle.Position.x, particle.Position.y, particle.Position.z, particle.Mass},
            {particle.Velocity.x, particle.Velocity.y, particle.Velocity.z, 0.0f}});
    }
    // 状態ワードを準備時にだけ初期化し、複数Substepのうち一度でも起きたGPUエラーを残します。
    std::vector<uint32_t> result(resultWords, 0u);
    m_Resources = {encoder->CreateStorageBuffer(sizeof(parameters), &parameters),
        encoder->CreateStorageBuffer(words.size() * sizeof(ParticleWord), words.data()),
        encoder->CreateStorageBuffer(gridBytes, nullptr),
        encoder->CreateStorageBuffer(result.size() * sizeof(uint32_t), result.data())};
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
    return Dispatch(false, timestamp);
}

bool GPUSPH::DispatchFixedStep(Ref<RHIComputeTimestamp>& timestamp)
{
    return Dispatch(true, timestamp);
}

bool GPUSPH::Dispatch(bool advancesParticles, Ref<RHIComputeTimestamp>& timestamp)
{
    timestamp.reset();
    m_HasStep = false;
    m_HasDensity = false;
    if (m_IsReady == false || (advancesParticles && m_CanAdvance == false))
    {
        return false;
    }
    if (m_ParticleCount == 0u)
    {
        return true;
    }
    m_HasDensity = m_Encoder->MeasureGpuWork([this, advancesParticles](RHIComputeCommandEncoder& encoder)
    {
        const std::array<RHIComputeBinding, 2> clear{{{0u, m_Resources[0]}, {2u, m_Resources[2]}}};
        const std::array<RHIComputeBinding, 4> particle{{{0u, m_Resources[0]}, {1u, m_Resources[1]},
            {2u, m_Resources[2]}, {3u, m_Resources[3]}}};
        const RHIComputeExtent particleGroups{GroupCount(m_ParticleCount), 1u, 1u};
        const bool hasDensity = encoder.Dispatch(m_Pipelines[0], clear, {GroupCount(m_CellCount), 1u, 1u})
            && encoder.Barrier(RHIComputeConsumer::Storage)
            && encoder.Dispatch(m_Pipelines[1], particle, particleGroups)
            && encoder.Barrier(RHIComputeConsumer::Storage)
            && encoder.Dispatch(m_Pipelines[2], particle, particleGroups)
            && encoder.Barrier(RHIComputeConsumer::Storage);
        if (hasDensity == false || advancesParticles == false)
        {
            return hasDensity;
        }
        // Force参照中の位置・速度を保つため、全Forceを書き終えてから積分します。
        const std::array<RHIComputeBinding, 3> integrate{{{0u, m_Resources[0]},
            {1u, m_Resources[1]}, {3u, m_Resources[3]}}};
        return encoder.Dispatch(m_Pipelines[3], particle, particleGroups)
            && encoder.Barrier(RHIComputeConsumer::Storage)
            && encoder.Dispatch(m_Pipelines[4], integrate, particleGroups)
            && encoder.Barrier(RHIComputeConsumer::Storage);
    }, timestamp);
    m_HasStep = m_HasDensity && advancesParticles;
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

GPUSPH::StepReadback GPUSPH::RequestStepReadback()
{
    if (m_HasStep == false || m_Encoder->Barrier(RHIComputeConsumer::BufferReadback) == false)
    {
        return {};
    }
    const auto count = static_cast<std::size_t>(m_ParticleCount);
    return {m_Encoder->RequestBufferReadback(m_Resources[1], 0u, count * sizeof(ParticleWord)),
        m_Encoder->RequestBufferReadback(m_Resources[3], 0u,
            (count * DynamicsWordsPerParticle + 1u) * sizeof(uint32_t))};
}

bool GPUSPH::DecodeStep(std::span<const std::byte> particleBytes, std::span<const std::byte> dynamicsBytes,
    std::span<FluidParticle> particles)
{
    const auto count = particles.size();
    if (particleBytes.size() != count * sizeof(ParticleWord)
        || dynamicsBytes.size() != (count * DynamicsWordsPerParticle + 1u) * sizeof(uint32_t))
    {
        return false;
    }
    uint32_t status = 0u;
    std::memcpy(&status, dynamicsBytes.data(), sizeof(status));
    if (status != 0u)
    {
        return false;
    }
    const std::byte* pForcePressure = dynamicsBytes.data() + (count + 1u) * sizeof(uint32_t);
    for (std::size_t index = 0u; index < count; ++index)
    {
        ParticleWord state{};
        std::array<float, 4> forcePressure{};
        std::memcpy(&state, particleBytes.data() + index * sizeof(state), sizeof(state));
        std::memcpy(&forcePressure, pForcePressure + index * sizeof(forcePressure), sizeof(forcePressure));
        auto& particle = particles[index];
        particle.Position = {state.m_PositionMass[0], state.m_PositionMass[1], state.m_PositionMass[2]};
        particle.Mass = state.m_PositionMass[3];
        particle.Velocity = {state.m_Velocity[0], state.m_Velocity[1], state.m_Velocity[2]};
        particle.Force = {forcePressure[0], forcePressure[1], forcePressure[2]};
        particle.Pressure = forcePressure[3];
        std::memcpy(&particle.Density, dynamicsBytes.data() + (index + 1u) * sizeof(uint32_t), sizeof(float));
    }
    return true;
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
