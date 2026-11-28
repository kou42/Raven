#include "Raven/Physics/Fluid/GPUSPH.h"

#include <algorithm>
#include <bit>
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
constexpr uint32_t MaximumAdaptiveSubsteps = 64u;
struct TimeControl
{
    float m_RemainingTime;
    float m_SelectedDt;
    float m_MinimumDt;
    uint32_t m_Substeps;
    uint32_t m_LimitReached;
    uint32_t m_MaxSpeedBits;
    uint32_t m_MaxAccelerationBits;
    uint32_t m_Reserved;
};
static_assert(sizeof(TimeControl) == 32u);
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
    std::array<float, 4> m_Cfl;
};
static_assert(sizeof(Parameters) == 128u);

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

GPUSPH::PrepareResult GPUSPH::PrepareAdaptiveFrame(const Ref<RHIComputeCommandEncoder>& encoder,
    const RHIComputeCapabilities& capabilities, std::span<const FluidParticle> particles,
    const SPHSettings& settings, float frameDeltaTime, const std::string& shaderSource)
{
    if (settings.StableTimeStepEnabled == false || std::isfinite(frameDeltaTime) == false || frameDeltaTime <= 0.0f
        || settings.MaximumSubsteps > MaximumAdaptiveSubsteps)
    {
        m_IsReady = false;
        m_HasDensity = false;
        m_HasStep = false;
        return PrepareResult::Failed;
    }
    return PrepareInternal(encoder, capabilities, particles, settings, frameDeltaTime, shaderSource,
        std::max(1u, settings.MaximumSubsteps));
}

GPUSPH::PrepareResult GPUSPH::PrepareInternal(const Ref<RHIComputeCommandEncoder>& encoder,
    const RHIComputeCapabilities& capabilities, std::span<const FluidParticle> particles,
    const SPHSettings& settings, float deltaTime, const std::string& shaderSource,
    uint32_t adaptiveSubsteps)
{
    const float smoothingRadius = settings.SmoothingRadius;
    m_AdaptiveSubsteps = adaptiveSubsteps;
    m_CanAdvance = deltaTime > 0.0f;
    m_HasStep = false;
    m_IsReady = false;
    m_HasDensity = false;
    m_Pipelines = {};
    m_Resources = {};
    m_Encoder = encoder;
    m_ParticleCount = 0u;
    m_CellCount = 0u;
    if (encoder == nullptr || capabilities.Supported == false || capabilities.MaxStorageBufferSlots < (adaptiveSubsteps > 0u ? 5u : 4u)
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
    parameters.m_Origin[3] = static_cast<int32_t>(adaptiveSubsteps);
    parameters.m_Cfl = {std::max(0.0f, settings.CFLFactor), std::max(0.0f, settings.AccelerationTimeStepFactor),
        std::max(0.0f, settings.MinimumTimeStep), std::sqrt(std::max(0.0f, settings.PressureStiffness))
            * std::max(0.0f, settings.SpeedOfSoundScale)};
    parameters.m_Dimensions[3] = particleCount;
    parameters.m_Kernel = {inverseCellSize, hSquared, poly6, smoothingRadius};

    using Type = RHIComputeBindingType;
    using Access = RHIComputeAccess;
    const RHIComputeBindingLayout parameterLayout{0u, Type::StorageBuffer, Access::Read};
    const RHIComputeBindingLayout particleLayout{1u, Type::StorageBuffer, Access::Read};
    const RHIComputeBindingLayout gridRead{2u, Type::StorageBuffer, Access::Read};
    const RHIComputeBindingLayout gridWrite{2u, Type::StorageBuffer, Access::ReadWrite};
    const RHIComputeBindingLayout resultWrite{3u, Type::StorageBuffer, Access::ReadWrite};
    const RHIComputeBindingLayout controlWrite{4u, Type::StorageBuffer, Access::ReadWrite};
    const std::array<std::vector<RHIComputeBindingLayout>, 8> layouts{{
        {parameterLayout, gridWrite},
        {parameterLayout, particleLayout, gridWrite, resultWrite},
        {parameterLayout, particleLayout, gridRead, resultWrite},
        {parameterLayout, particleLayout, gridRead, resultWrite},
        {parameterLayout, {1u, Type::StorageBuffer, Access::ReadWrite}, resultWrite},
        {parameterLayout, controlWrite},
        {parameterLayout, particleLayout, resultWrite, controlWrite},
        {parameterLayout, controlWrite}
    }};
    constexpr std::array passes{"#define RAVEN_SPH_CLEAR\n", "#define RAVEN_SPH_BUILD\n", "#define RAVEN_SPH_DENSITY\n",
        "#define RAVEN_SPH_FORCE\n", "#define RAVEN_SPH_INTEGRATE\n", "#define RAVEN_SPH_CFL_BEGIN\n",
        "#define RAVEN_SPH_CFL_REDUCE\n", "#define RAVEN_SPH_CFL_CHOOSE\n"};
    const auto versionEnd = shaderSource.find('\n');
    if (versionEnd == std::string::npos)
    {
        return PrepareResult::Failed;
    }
    for (std::size_t index = 0; index < (adaptiveSubsteps > 0u ? passes.size() : (m_CanAdvance ? 5u : 3u)); ++index)
    {
        RHIComputePipelineSpecification spec;
        spec.OpenGLSource = shaderSource;
        spec.OpenGLSource.insert(versionEnd + 1u, passes[index]);
        spec.LocalSize = {GroupSize, 1u, 1u};
        spec.Layout = layouts[index];
        if (adaptiveSubsteps > 0u)
        {
            spec.OpenGLSource.insert(versionEnd + 1u, "#define RAVEN_SPH_ADAPTIVE\n");
            if (index < 5u)
            {
                spec.Layout.push_back({4u, Type::StorageBuffer, Access::Read});
            }
        }
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
    if (adaptiveSubsteps > 0u)
    {
        // 最初のCFLはCPU版と同じく、呼び出し側が渡したForceとVelocityから始めます。
        for (std::size_t index = 0u; index < particles.size(); ++index)
        {
            const std::size_t offset = 1u + particles.size() + 4u * index;
            result[offset] = std::bit_cast<uint32_t>(particles[index].Force.x);
            result[offset + 1u] = std::bit_cast<uint32_t>(particles[index].Force.y);
            result[offset + 2u] = std::bit_cast<uint32_t>(particles[index].Force.z);
            result[offset + 3u] = std::bit_cast<uint32_t>(particles[index].Pressure);
            result[index + 1u] = std::bit_cast<uint32_t>(particles[index].Density);
        }
    }
    m_Resources = {encoder->CreateStorageBuffer(sizeof(parameters), &parameters),
        encoder->CreateStorageBuffer(words.size() * sizeof(ParticleWord), words.data()),
        encoder->CreateStorageBuffer(gridBytes, nullptr),
        encoder->CreateStorageBuffer(result.size() * sizeof(uint32_t), result.data()),
        adaptiveSubsteps > 0u ? encoder->CreateStorageBuffer(sizeof(TimeControl), nullptr) : nullptr};
    m_IsReady = std::all_of(m_Resources.begin(), m_Resources.begin() + (adaptiveSubsteps > 0u ? 5u : 4u), [](const auto& buffer)
    {
        return buffer != nullptr;
    });
    m_CellCount = cellCount;
    m_ParticleCount = particleCount;
    return m_IsReady ? PrepareResult::Ready : PrepareResult::Failed;
}

bool GPUSPH::DispatchDensity(Ref<RHIComputeTimestamp>& timestamp)
{
    return Dispatch(false, &timestamp);
}

bool GPUSPH::DispatchFixedStep()
{
    return Dispatch(true, nullptr);
}

bool GPUSPH::DispatchFixedStep(Ref<RHIComputeTimestamp>& timestamp)
{
    return Dispatch(true, &timestamp);
}

bool GPUSPH::Dispatch(bool advancesParticles, Ref<RHIComputeTimestamp>* pTimestamp)
{
    if (pTimestamp != nullptr)
    {
        pTimestamp->reset();
    }
    m_HasStep = false;
    m_HasDensity = false;
    if (m_IsReady == false || m_AdaptiveSubsteps > 0u || (advancesParticles && m_CanAdvance == false))
    {
        return false;
    }
    if (m_ParticleCount == 0u)
    {
        return true;
    }
    const auto record = [this, advancesParticles](RHIComputeCommandEncoder& encoder)
    {
        return RecordParticlePasses(encoder, advancesParticles);
    };
    m_HasDensity = pTimestamp != nullptr ? m_Encoder->MeasureGpuWork(record, *pTimestamp) : record(*m_Encoder);
    m_HasStep = m_HasDensity && advancesParticles;
    return m_HasDensity;
}

bool GPUSPH::RecordParticlePasses(RHIComputeCommandEncoder& encoder, bool advancesParticles) const
{
    const bool isAdaptive = m_AdaptiveSubsteps > 0u;
    const RHIComputeBinding control{4u, m_Resources[4]};
    const std::array<RHIComputeBinding, 3> clear{{{0u, m_Resources[0]}, {2u, m_Resources[2]}, control}};
    const std::array<RHIComputeBinding, 5> particle{{{0u, m_Resources[0]}, {1u, m_Resources[1]},
        {2u, m_Resources[2]}, {3u, m_Resources[3]}, control}};
    const RHIComputeExtent particleGroups{GroupCount(m_ParticleCount), 1u, 1u};
    const auto clearBindings = std::span(clear).first(isAdaptive ? 3u : 2u);
    const auto particleBindings = std::span(particle).first(isAdaptive ? 5u : 4u);
    const bool hasDensity = encoder.Dispatch(m_Pipelines[0], clearBindings, {GroupCount(m_CellCount), 1u, 1u})
        && encoder.Barrier(RHIComputeConsumer::Storage)
        && encoder.Dispatch(m_Pipelines[1], particleBindings, particleGroups)
        && encoder.Barrier(RHIComputeConsumer::Storage)
        && encoder.Dispatch(m_Pipelines[2], particleBindings, particleGroups)
        && encoder.Barrier(RHIComputeConsumer::Storage);
    if (hasDensity == false || advancesParticles == false)
    {
        return hasDensity;
    }
    // Force参照中の位置・速度を保つため、全Forceを書き終えてから積分します。
    const std::array<RHIComputeBinding, 4> integrate{{{0u, m_Resources[0]},
        {1u, m_Resources[1]}, {3u, m_Resources[3]}, control}};
    return encoder.Dispatch(m_Pipelines[3], particleBindings, particleGroups)
        && encoder.Barrier(RHIComputeConsumer::Storage)
        && encoder.Dispatch(m_Pipelines[4], std::span(integrate).first(isAdaptive ? 4u : 3u), particleGroups)
        && encoder.Barrier(RHIComputeConsumer::Storage);
}

bool GPUSPH::DispatchAdaptiveFrame(Ref<RHIComputeTimestamp>& timestamp)
{
    timestamp.reset();
    m_HasStep = false;
    m_HasDensity = false;
    if (m_IsReady == false || m_AdaptiveSubsteps == 0u)
    {
        return false;
    }
    if (m_ParticleCount == 0u)
    {
        return true;
    }
    m_HasStep = m_Encoder->MeasureGpuWork([this](RHIComputeCommandEncoder& encoder)
    {
        const RHIComputeExtent singleGroup{1u, 1u, 1u};
        const RHIComputeExtent particleGroups{GroupCount(m_ParticleCount), 1u, 1u};
        const RHIComputeBinding control{4u, m_Resources[4]};
        const std::array<RHIComputeBinding, 2> timeOnly{{{0u, m_Resources[0]}, control}};
        const std::array<RHIComputeBinding, 4> motion{{{0u, m_Resources[0]}, {1u, m_Resources[1]},
            {3u, m_Resources[3]}, control}};
        if (encoder.Dispatch(m_Pipelines[5], timeOnly, singleGroup) == false
            || encoder.Barrier(RHIComputeConsumer::Storage) == false)
        {
            return false;
        }
        // CPUは上限回数を発行し、残り時間がないスロットはGPU側で何も更新しません。
        for (uint32_t slot = 0u; slot < m_AdaptiveSubsteps; ++slot)
        {
            if (encoder.Dispatch(m_Pipelines[6], motion, particleGroups) == false
                || encoder.Barrier(RHIComputeConsumer::Storage) == false
                || encoder.Dispatch(m_Pipelines[7], timeOnly, singleGroup) == false
                || encoder.Barrier(RHIComputeConsumer::Storage) == false
                || RecordParticlePasses(encoder, true) == false)
            {
                return false;
            }
        }
        return true;
    }, timestamp);
    m_HasDensity = m_HasStep;
    return m_HasStep;
}

Ref<RHIComputeReadback> GPUSPH::RequestAdaptiveStatistics()
{
    if (m_HasStep == false || m_AdaptiveSubsteps == 0u
        || m_Encoder->Barrier(RHIComputeConsumer::BufferReadback) == false)
    {
        return {};
    }
    return m_Encoder->RequestBufferReadback(m_Resources[4], 0u, sizeof(TimeControl));
}

bool GPUSPH::DecodeAdaptiveStatistics(std::span<const std::byte> bytes, AdaptiveStatistics& statistics)
{
    if (bytes.size() != sizeof(TimeControl))
    {
        return false;
    }
    TimeControl control{};
    std::memcpy(&control, bytes.data(), sizeof(control));
    statistics = {control.m_Substeps, control.m_MinimumDt, control.m_LimitReached != 0u};
    return true;
}

Ref<RHIComputeResource> GPUSPH::GetParticleBuffer() const
{
    return m_IsReady ? m_Resources[1] : nullptr;
}

uint32_t GPUSPH::GetParticleCount() const
{
    return m_IsReady ? m_ParticleCount : 0u;
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
