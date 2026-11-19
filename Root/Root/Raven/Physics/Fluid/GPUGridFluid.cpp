#include "Raven/Physics/Fluid/GPUGridFluid.h"

#include <cmath>

namespace Raven::ph
{
namespace
{
constexpr std::uint32_t WorkgroupWidth = 64;
constexpr std::uint32_t MaxGroupCount = 65535;
using Word = std::array<float, 4>;

bool IsFinite(math::Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Word Pack(math::Vec3 value, float w = 0.0f) { return {value.x, value.y, value.z, w}; }

std::uint32_t GroupCount(std::size_t count)
{
    return static_cast<std::uint32_t>((count - 1) / WorkgroupWidth + 1);
}
} // namespace

bool GPUGridFluid::Prepare(Ref<RHIComputeCommandEncoder> encoder, RHIComputePipelineSpecification gridShader,
    RHIComputePipelineSpecification particleShader, const DenseSDFGenerator& signedBoundary,
    const GPUGridFluidSettings& settings,
    std::span<const math::Vec3> particlePositions)
{
    const auto& grid = signedBoundary.GetGrid();
    const auto voxelCount = grid.GetVoxelCount();
    if (encoder == nullptr || signedBoundary.IsSigned() == false || signedBoundary.GetTexture() == nullptr ||
        voxelCount == 0 || particlePositions.empty() ||
        voxelCount > WorkgroupWidth * MaxGroupCount || particlePositions.size() > WorkgroupWidth * MaxGroupCount ||
        grid.Size[0] < 4 || grid.Size[1] < 4 || grid.Size[2] < 4 ||
        std::isfinite(settings.FixedStep) == false || settings.FixedStep <= 0.0f ||
        std::isfinite(settings.Viscosity) == false || settings.Viscosity < 0.0f ||
        std::isfinite(settings.ParticleRadius) == false || settings.ParticleRadius < 0.0f ||
        IsFinite(settings.InitialVelocity) == false || IsFinite(settings.Acceleration) == false)
    {
        return false;
    }
    const float diffusion = settings.FixedStep * settings.Viscosity;
    const math::Vec3 weights{diffusion / (grid.Spacing.x * grid.Spacing.x),
        diffusion / (grid.Spacing.y * grid.Spacing.y), diffusion / (grid.Spacing.z * grid.Spacing.z)};
    // 拡散を凸結合に保ち、固定Step幅での数値発散を避けます。
    if (IsFinite(weights) == false || weights.x + weights.y + weights.z > 0.5f)
    {
        return false;
    }
    const auto first = grid.GetCellCenter(1, 1, 1);
    const auto last = grid.GetCellCenter(grid.Size[0] - 2, grid.Size[1] - 2, grid.Size[2] - 2);
    std::vector<GPUGridFluidParticle> initial;
    initial.reserve(particlePositions.size());
    for (const auto position : particlePositions)
    {
        if (IsFinite(position) == false || position.x < first.x || position.y < first.y || position.z < first.z ||
            position.x > last.x || position.y > last.y || position.z > last.z)
        {
            return false;
        }
        initial.push_back({Pack(position), {}});
    }
    const Word parameters[] = {Pack(grid.Origin, settings.FixedStep), Pack(grid.Spacing, settings.ParticleRadius),
        Pack(settings.Acceleration), Pack(weights)};
    using Type = RHIComputeBindingType;
    using Access = RHIComputeAccess;
    gridShader.LocalSize = {WorkgroupWidth, 1, 1};
    gridShader.Layout = {{0, Type::StorageBuffer, Access::Read}, {1, Type::StorageBuffer, Access::Read},
        {2, Type::StorageBuffer, Access::Write}, {3, Type::StorageImage3D, Access::Read}};
    particleShader.LocalSize = gridShader.LocalSize;
    particleShader.Layout = gridShader.Layout;
    particleShader.Layout[2].Access = Access::ReadWrite;

    auto gridPipeline = encoder->CreatePipeline(gridShader);
    auto particlePipeline = encoder->CreatePipeline(particleShader);
    auto parameterBuffer = encoder->CreateStorageBuffer(sizeof(parameters), parameters);
    const std::vector<Word> velocities(voxelCount, Pack(settings.InitialVelocity));
    auto firstVelocity = encoder->CreateStorageBuffer(voxelCount * sizeof(Word), velocities.data());
    auto secondVelocity = encoder->CreateStorageBuffer(voxelCount * sizeof(Word), nullptr);
    auto particles = encoder->CreateStorageBuffer(initial.size() * sizeof(GPUGridFluidParticle), initial.data());
    if (gridPipeline == nullptr || particlePipeline == nullptr || parameterBuffer == nullptr ||
        firstVelocity == nullptr || secondVelocity == nullptr || particles == nullptr)
    {
        return false;
    }
    m_Encoder = std::move(encoder);
    m_GridPipeline = std::move(gridPipeline);
    m_ParticlePipeline = std::move(particlePipeline);
    m_Parameters = std::move(parameterBuffer);
    m_Boundary = signedBoundary.GetTexture();
    m_Velocity = {std::move(firstVelocity), std::move(secondVelocity)};
    m_Particles = std::move(particles);
    m_GridGroups = GroupCount(voxelCount);
    m_ParticleGroups = GroupCount(initial.size());
    m_ParticleCount = initial.size();
    m_FixedStep = settings.FixedStep;
    m_CurrentVelocity = 0;
    m_StepGeneration = 0;
    m_ReadbackCount = 0;
    m_Ready = true;
    return true;
}

void GPUGridFluid::SimulateFluid(float fixedDeltaTime)
{
    if (m_Ready == false || fixedDeltaTime != m_FixedStep)
    {
        return;
    }
    const auto next = 1u - m_CurrentVelocity;
    const RHIComputeBinding grid[] = {{0, m_Parameters}, {1, m_Velocity[m_CurrentVelocity]},
        {2, m_Velocity[next]}, {3, m_Boundary}};
    const RHIComputeBinding particles[] = {{0, m_Parameters}, {1, m_Velocity[next]},
        {2, m_Particles}, {3, m_Boundary}};
    // 前StepとSDF生成の書き込みを可視化してから、Grid更新と粒子更新を順に記録します。
    m_Ready = m_Encoder->Barrier(RHIComputeConsumer::Storage) &&
        m_Encoder->Dispatch(m_GridPipeline, grid, {m_GridGroups, 1, 1}) &&
        m_Encoder->Barrier(RHIComputeConsumer::Storage) &&
        m_Encoder->Dispatch(m_ParticlePipeline, particles, {m_ParticleGroups, 1, 1}) &&
        m_Encoder->Barrier(RHIComputeConsumer::Storage);
    if (m_Ready == true)
    {
        m_CurrentVelocity = next;
        ++m_StepGeneration;
    }
}

Ref<RHIComputeReadback> GPUGridFluid::RequestParticleReadback()
{
    if (m_Ready == false || m_Encoder->Barrier(RHIComputeConsumer::BufferReadback) == false)
    {
        return nullptr;
    }
    auto result = m_Encoder->RequestBufferReadback(m_Particles, 0, m_ParticleCount * sizeof(GPUGridFluidParticle));
    if (result != nullptr)
    {
        ++m_ReadbackCount;
    }
    return result;
}

const Ref<RHIComputeResource>& GPUGridFluid::GetParticleBuffer() const { return m_Particles; }
std::uint64_t GPUGridFluid::GetStepGeneration() const { return m_StepGeneration; }
std::uint64_t GPUGridFluid::GetReadbackCount() const { return m_ReadbackCount; }
bool GPUGridFluid::IsReady() const { return m_Ready; }
} // namespace Raven::ph
