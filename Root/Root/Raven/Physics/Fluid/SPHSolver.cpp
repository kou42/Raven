#include "Raven/Physics/Fluid/SPHSolver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "Raven/Core/CPUProfiler.h"
#include "Raven/Core/Jobs/JobSystem.h"
#include "Raven/Diagnostics/DiagnosticMeasurement.h"
#include "Raven/Math/Math.h"

namespace Raven
{
namespace ph
{
namespace
{
constexpr float MinimumSmoothingRadius = 1.0e-4f;
constexpr float MinimumSpatialHashCellSizeScale = 0.01f;
constexpr std::size_t InitialNeighborCapacity = 1024u;
// 512粒子では投入コストが勝つため、1,024粒子から、1範囲を256粒子まで小さくできるようにします。
constexpr std::size_t MinimumParticlesForForceJobs = 1024u;
constexpr std::size_t MinimumParticlesPerForceRange = 256u;
constexpr std::size_t MinimumParticlesForWeightedForceRanges = 2048u;
constexpr std::size_t MinimumParticlesPerWeightedForceRange = 128u;
constexpr uint64_t ForceParticleWork = 16u;
constexpr uint64_t ForceAcceptedNeighborWork = 4u;
constexpr double ForceWorkSkewRatio = 1.5;
constexpr std::size_t MinimumParticlesPerDensityRange = 512u;

using Clock = std::chrono::steady_clock;

void RecordSPHCounter(const char* name, double value)
{
    CPUProfiler::Get().AddCounter(name, value);
    Diagnostics::RecordCounter(name, value, Diagnostics::DiagnosticCategory::Physics);
}

void ResolveBoundaryAxis(float& position, float& velocity, float minimum, float maximum, float restitution)
{
    if (position < minimum)
    {
        position = minimum;
        if (velocity < 0.0f)
        {
            velocity = -velocity * restitution;
        }
        return;
    }

    if (position > maximum)
    {
        position = maximum;
        if (velocity > 0.0f)
        {
            velocity = -velocity * restitution;
        }
    }
}

double LengthSquaredForStatistics(const math::Vec3& value)
{
    const float squared = value.LengthSq();
    if (std::isfinite(squared) && squared >= std::numeric_limits<float>::min())
    {
        return squared;
    }

    // 通常値では従来のfloatの積和を使い、float二乗の範囲外だけdoubleへ広げます。
    const double x = value.x;
    const double y = value.y;
    const double z = value.z;
    return x * x + y * y + z * z;
}

float NarrowMagnitude(double value)
{
    return value > std::numeric_limits<float>::max()
        ? std::numeric_limits<float>::infinity()
        : static_cast<float>(value);
}

struct BoundaryBounds
{
    math::Vec3 m_Minimum;
    math::Vec3 m_Maximum;
};

BoundaryBounds ComputeBoundaryBounds(const SPHSettings& settings)
{
    const float radius = settings.BoundaryParticleRadius;
    BoundaryBounds bounds{settings.BoundaryMinimum + math::Vec3(radius),
        settings.BoundaryMaximum - math::Vec3(radius)};
    const auto collapseInvertedAxis = [](float& minimum, float& maximum, float lower, float upper)
    {
        if (minimum > maximum)
        {
            const float center = (lower + upper) * 0.5f;
            minimum = center;
            maximum = center;
        }
    };
    collapseInvertedAxis(bounds.m_Minimum.x, bounds.m_Maximum.x,
        settings.BoundaryMinimum.x, settings.BoundaryMaximum.x);
    collapseInvertedAxis(bounds.m_Minimum.y, bounds.m_Maximum.y,
        settings.BoundaryMinimum.y, settings.BoundaryMaximum.y);
    collapseInvertedAxis(bounds.m_Minimum.z, bounds.m_Maximum.z,
        settings.BoundaryMinimum.z, settings.BoundaryMaximum.z);
    return bounds;
}

void ResolveParticleBoundary(FluidParticle& particle, const BoundaryBounds& bounds, float restitution)
{
    ResolveBoundaryAxis(particle.Position.x, particle.Velocity.x, bounds.m_Minimum.x, bounds.m_Maximum.x, restitution);
    ResolveBoundaryAxis(particle.Position.y, particle.Velocity.y, bounds.m_Minimum.y, bounds.m_Maximum.y, restitution);
    ResolveBoundaryAxis(particle.Position.z, particle.Velocity.z, bounds.m_Minimum.z, bounds.m_Maximum.z, restitution);
}

void IntegrateParticle(FluidParticle& particle, float mass, float deltaTime)
{
    if (mass > math::Epsilon)
    {
        const math::Vec3 acceleration = particle.Force / mass;
        particle.Velocity += acceleration * deltaTime;
        particle.Position += particle.Velocity * deltaTime;
    }
}

}

SPHSolver::SPHSolver(const SPHSettings& settings)
    : m_SpatialHash(settings.SmoothingRadius)
{
    SetSettings(settings);
}

void SPHSolver::SetSettings(const SPHSettings& settings)
{
    if (settings.MaximumCachedNeighbors < m_Settings.MaximumCachedNeighbors)
    {
        // 設定を下げた後も旧予算のバッファを保持し続けないようにします。
        for (DensityJobBuffer& buffer : m_DensityBuffers)
        {
            buffer = {};
        }
    }
    m_Settings = settings;
    m_Settings.SmoothingRadius = std::max(settings.SmoothingRadius, MinimumSmoothingRadius);
    m_Kernel = SPHKernel(m_Settings.SmoothingRadius);
    m_Settings.SpatialHashCellSizeScale = std::max(
        settings.SpatialHashCellSizeScale,
        MinimumSpatialHashCellSizeScale);
    m_Settings.VerletSkinScale = std::isfinite(settings.VerletSkinScale)
        ? std::clamp(settings.VerletSkinScale, 0.0f, 1.0f)
        : 0.0f;
    if (m_Settings.VerletSkinScale == 0.0f || m_Settings.MaximumCachedNeighbors == 0u)
    {
        std::vector<math::Vec3>().swap(m_VerletReferencePositions);
    }
    m_Settings.RestDensity = std::max(0.0f, settings.RestDensity);
    m_Settings.PressureStiffness = std::max(0.0f, settings.PressureStiffness);
    m_Settings.Viscosity = std::max(0.0f, settings.Viscosity);
    m_Settings.CFLFactor = std::max(0.0f, settings.CFLFactor);
    m_Settings.SpeedOfSoundScale = std::max(0.0f, settings.SpeedOfSoundScale);
    m_Settings.AccelerationTimeStepFactor = std::max(0.0f, settings.AccelerationTimeStepFactor);
    m_Settings.MinimumTimeStep = std::max(0.0f, settings.MinimumTimeStep);
    m_Settings.MaximumSubsteps = std::max(1u, settings.MaximumSubsteps);
    m_Settings.BoundaryParticleRadius = std::max(0.0f, settings.BoundaryParticleRadius);
    m_Settings.BoundaryRestitution = std::clamp(settings.BoundaryRestitution, 0.0f, 1.0f);
    m_Settings.BoundaryMinimum = {
        std::min(settings.BoundaryMinimum.x, settings.BoundaryMaximum.x),
        std::min(settings.BoundaryMinimum.y, settings.BoundaryMaximum.y),
        std::min(settings.BoundaryMinimum.z, settings.BoundaryMaximum.z) };
    m_Settings.BoundaryMaximum = {
        std::max(settings.BoundaryMinimum.x, settings.BoundaryMaximum.x),
        std::max(settings.BoundaryMinimum.y, settings.BoundaryMaximum.y),
        std::max(settings.BoundaryMinimum.z, settings.BoundaryMaximum.z) };

    // Kernel support radius hとSpatial Hash CellSizeは役割が異なります。
    // hはDensity / Forceの物理結果を決め、CellSizeは候補探索の効率だけを決めます。
    // Neighbor Queryはhから走査Cell数を求めるため、CellSizeを変更してもh内のParticleを取りこぼしません。
    const float spatialHashCellSize =
        m_Settings.SmoothingRadius * m_Settings.SpatialHashCellSizeScale;
    m_SpatialHash.SetCellSize(spatialHashCellSize);
}

void SPHSolver::ComputeDensity(std::vector<FluidParticle>& particles)
{
    ComputeDensity(particles, false, nullptr);
}

std::span<const SPHSolver::DensityJobBuffer> SPHSolver::ComputeDensity(
    std::vector<FluidParticle>& particles, bool isSubstep, JobSystem* pJobSystem, VerletStepState* pVerlet)
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Density", Diagnostics::DiagnosticCategory::Physics);
    const float h = m_Settings.SmoothingRadius;
    NeighborSearch search = NeighborSearch::Grid;
    float searchRadius = h;
    if (pVerlet != nullptr && pVerlet->m_SearchRadius > h)
    {
        search = CanReuseVerletList(particles, *pVerlet) ? NeighborSearch::ReuseVerlet : NeighborSearch::BuildVerlet;
        searchRadius = pVerlet->m_SearchRadius;
    }
    if (search != NeighborSearch::ReuseVerlet)
    {
        m_SpatialHash.Build(particles);
        if (isSubstep == true)
        {
            // 診断TraceはGrid側で発行し、ここではCPUProfiler用の区間だけを追加します。
            CPUProfileScope bucketProfile("Physics.Fluid.SPH.PrepareNeighborBuckets");
            m_SpatialHash.PrepareNeighborBuckets(searchRadius);
        }
    }

    std::span<NeighborForceTerms> forceTerms;
    if (isSubstep == true)
    {
        // Jobは自身の粒子の項だけを書きます。再確保はJob投入前に完了させます。
        m_NeighborForceTerms.resize(particles.size());
        forceTerms = m_NeighborForceTerms;
    }

    std::size_t rangeCount = 1u;
    if (pJobSystem != nullptr && particles.size() >= 2u * MinimumParticlesPerDensityRange)
    {
        rangeCount += std::min({MaximumWorkerJobs, pJobSystem->GetWorkerCount(),
            particles.size() / MinimumParticlesPerDensityRange - 1u});
    }

    std::span<uint64_t> forceWork;
    if (isSubstep == true)
    {
        if (rangeCount > 1u && particles.size() >= MinimumParticlesForWeightedForceRanges)
        {
            m_ForceWork.resize(particles.size());
            forceWork = m_ForceWork;
        }
        else
        {
            m_ForceWork.clear();
        }
    }

    DensityJobBuffer standaloneBuffer;
    std::span<DensityJobBuffer> ranges = isSubstep
        ? std::span<DensityJobBuffer>(m_DensityBuffers.data(), rangeCount)
        : std::span<DensityJobBuffer>(&standaloneBuffer, 1u);
    const std::size_t rangeSize = particles.size() / rangeCount;
    const std::size_t cacheLimit = isSubstep ? m_Settings.MaximumCachedNeighbors : 0u;
    for (std::size_t index = 0u; search != NeighborSearch::ReuseVerlet && index < rangeCount; ++index)
    {
        DensityJobBuffer& buffer = ranges[index];
        buffer.m_RangeBegin = index * rangeSize;
        buffer.m_RangeEnd = index + 1u == rangeCount ? particles.size() : (index + 1u) * rangeSize;
        buffer.m_CacheLimit = cacheLimit / rangeCount + (index < cacheLimit % rangeCount ? 1u : 0u);

        // 並列度を切り替えた後も、以前の大きな確保を足し合わせて総予算を超えないようにします。
        if (buffer.m_Neighbors.capacity() > buffer.m_CacheLimit)
        {
            std::vector<CachedNeighbor>().swap(buffer.m_Neighbors);
        }
        if (cacheLimit == 0u)
        {
            std::vector<std::size_t>().swap(buffer.m_Offsets);
        }
        else
        {
            const std::size_t offsetCount = buffer.m_RangeEnd - buffer.m_RangeBegin + 1u;
            if (buffer.m_Offsets.capacity() > offsetCount)
            {
                std::vector<std::size_t>().swap(buffer.m_Offsets);
            }
            buffer.m_Offsets.reserve(offsetCount);
            buffer.m_Offsets.resize(offsetCount);
            buffer.m_Offsets[0] = 0u;
        }
    }
    if (isSubstep == true)
    {
        for (std::size_t index = rangeCount; index < m_DensityBuffers.size(); ++index)
        {
            m_DensityBuffers[index] = {};
        }
    }

    std::size_t jobCount = 0u;
    {
        Diagnostics::ProfiledTraceScope loopTrace("Physics.Fluid.SPH.DensityNeighborLoop",
            Diagnostics::DiagnosticCategory::Physics);
        std::array<std::future<void>, MaximumWorkerJobs> jobs;
        for (; jobCount + 1u < rangeCount; ++jobCount)
        {
            try
            {
                // 位置・Mass・Gridは全Jobで読み取り専用です。各Jobは別々のDensityとBufferを書き込みます。
                DensityJobBuffer& buffer = ranges[jobCount];
                jobs[jobCount] = pJobSystem->Submit([this, &particles, &buffer, forceTerms, search, searchRadius, forceWork]()
                {
                    ComputeDensityRange(particles, buffer, forceTerms, search, searchRadius, forceWork);
                });
            }
            catch (...)
            {
                break;
            }
            if (jobs[jobCount].valid() == false)
            {
                break;
            }
        }

        try
        {
            // 投入に失敗した場合も、まだJobに渡していない範囲を呼び出し元で処理します。
            for (std::size_t index = jobCount; index < rangeCount; ++index)
            {
                ComputeDensityRange(particles, ranges[index], forceTerms, search, searchRadius, forceWork);
            }
            for (std::size_t index = 0u; index < jobCount; ++index)
            {
                jobs[index].get();
            }
        }
        catch (...)
        {
            // 近傍バッファの確保に失敗しても、借用中のJobが残ったまま呼び出し元に戻りません。
            for (auto& job : jobs)
            {
                if (job.valid() == true)
                {
                    job.wait();
                }
            }
            throw;
        }
    }

    uint64_t candidateVisitCount = 0u;
    uint64_t acceptedNeighborCount = 0u;
    std::size_t cachedNeighborCount = 0u;
    std::size_t cachedRowCount = 0u;
    std::size_t overflowedRangeCount = 0u;
    std::size_t capacityBytes = 0u;
    for (const DensityJobBuffer& buffer : ranges)
    {
        candidateVisitCount += buffer.m_Candidates;
        acceptedNeighborCount += buffer.m_Accepted;
        cachedNeighborCount += buffer.m_Neighbors.size();
        cachedRowCount += buffer.m_CachedRowCount;
        overflowedRangeCount += buffer.m_HasOverflowed ? 1u : 0u;
        capacityBytes += buffer.m_Neighbors.capacity() * sizeof(CachedNeighbor)
            + buffer.m_Offsets.capacity() * sizeof(std::size_t);
    }
    if (forceWork.empty() == false)
    {
        // 均一な範囲では粒子数分割を維持し、累積値の作成と再分割を省きます。
        double minimumWorkPerParticle = std::numeric_limits<double>::max();
        double maximumWorkPerParticle = 0.0;
        for (const DensityJobBuffer& buffer : ranges)
        {
            const auto work = buffer.m_Candidates + ForceAcceptedNeighborWork * buffer.m_Accepted;
            const double average = static_cast<double>(work) / (buffer.m_RangeEnd - buffer.m_RangeBegin);
            minimumWorkPerParticle = std::min(minimumWorkPerParticle, average);
            maximumWorkPerParticle = std::max(maximumWorkPerParticle, average);
        }
        if (maximumWorkPerParticle < minimumWorkPerParticle * ForceWorkSkewRatio + ForceParticleWork)
        {
            m_ForceWork.clear();
        }
    }
    if (search != NeighborSearch::Grid)
    {
        const bool hasOverflowed = overflowedRangeCount > 0u;
        const bool isRebuilding = search == NeighborSearch::BuildVerlet;
        if (hasOverflowed == true)
        {
            // 拡張リストに欠けた行があれば、現位置のGridへ戻します。同じStep内では再試行しません。
            pVerlet->m_HasValidList = false;
            pVerlet->m_SearchRadius = 0.0f;
            cachedRowCount = 0u;
            CPUProfileScope bucketProfile("Physics.Fluid.SPH.PrepareNeighborBuckets");
            m_SpatialHash.PrepareNeighborBuckets(h);
        }
        else if (isRebuilding == true)
        {
            m_VerletReferencePositions.resize(particles.size());
            for (std::size_t index = 0u; index < particles.size(); ++index)
            {
                m_VerletReferencePositions[index] = particles[index].Position;
            }
            pVerlet->m_HasValidList = true;
        }

        RecordSPHCounter("Physics.Fluid.SPH.VerletRebuildCount", isRebuilding ? 1.0 : 0.0);
        RecordSPHCounter("Physics.Fluid.SPH.VerletReuseCount", isRebuilding ? 0.0 : 1.0);
        RecordSPHCounter("Physics.Fluid.SPH.VerletOverflowCount", hasOverflowed ? 1.0 : 0.0);
        if (hasOverflowed == false && acceptedNeighborCount > 0u)
        {
            const double ratio = static_cast<double>(cachedNeighborCount) / static_cast<double>(acceptedNeighborCount);
            RecordSPHCounter("Physics.Fluid.SPH.VerletCandidateExpansionRatio", ratio);
        }
    }
    if (pVerlet != nullptr)
    {
        RecordSPHCounter("Physics.Fluid.SPH.VerletReferenceCapacityBytes",
            static_cast<double>(m_VerletReferencePositions.capacity() * sizeof(math::Vec3)));
    }
    if (overflowedRangeCount > 0u && m_Settings.ReuseCompletedNeighborRowsOnOverflow == false)
    {
        cachedRowCount = 0u;
    }
    if (isSubstep == true)
    {
        RecordSPHCounter("Physics.Fluid.SPH.DensityJobCount", static_cast<double>(jobCount));
        RecordSPHCounter("Physics.Fluid.SPH.NeighborCacheCount", static_cast<double>(cachedNeighborCount));
        RecordSPHCounter("Physics.Fluid.SPH.NeighborCacheOverflow", overflowedRangeCount > 0u ? 1.0 : 0.0);
        RecordSPHCounter("Physics.Fluid.SPH.NeighborCacheOverflowedRangeCount",
            static_cast<double>(overflowedRangeCount));
        // 使用中の件数と区別して、再利用のため保持している行オフセットも含めます。
        RecordSPHCounter("Physics.Fluid.SPH.NeighborCacheCapacityBytes", static_cast<double>(capacityBytes));
        RecordSPHCounter("Physics.Fluid.SPH.NeighborCacheCompletedRowCount", static_cast<double>(cachedRowCount));
    }

    CPUProfiler& profiler = CPUProfiler::Get();
    profiler.AddCounter(
        "Physics.Fluid.SPH.DensityNeighborCandidateCount",
        static_cast<double>(candidateVisitCount));
    profiler.AddCounter(
        "Physics.Fluid.SPH.DensityNeighborAcceptedCount",
        static_cast<double>(acceptedNeighborCount));
    Diagnostics::RecordCounter("Physics.Fluid.SPH.DensityNeighborCandidateCount",
        static_cast<double>(candidateVisitCount), Diagnostics::DiagnosticCategory::Physics);
    Diagnostics::RecordCounter("Physics.Fluid.SPH.DensityNeighborAcceptedCount",
        static_cast<double>(acceptedNeighborCount), Diagnostics::DiagnosticCategory::Physics);

    const double densityAcceptanceRatio = candidateVisitCount > 0u
        ? static_cast<double>(acceptedNeighborCount) / static_cast<double>(candidateVisitCount)
        : 0.0;
    const double averageDensityNeighbors = particles.empty() == false
        ? static_cast<double>(acceptedNeighborCount) / static_cast<double>(particles.size())
        : 0.0;

    profiler.AddCounter(
        "Physics.Fluid.SPH.DensityNeighborAcceptanceRatio",
        densityAcceptanceRatio);
    profiler.AddCounter(
        "Physics.Fluid.SPH.DensityAverageAcceptedNeighborsPerParticle",
        averageDensityNeighbors);
    if (cachedRowCount == 0u)
    {
        return {};
    }
    return ranges;
}

void SPHSolver::ComputeDensityRange(std::vector<FluidParticle>& particles, DensityJobBuffer& buffer,
    std::span<NeighborForceTerms> forceTerms, NeighborSearch search, float searchRadius,
    std::span<uint64_t> forceWork) const
{
    const float h = m_Settings.SmoothingRadius;
    const float hSq = h * h;
    const float searchRadiusSq = searchRadius * searchRadius;
    const float nearZeroDistanceSq = math::Epsilon * math::Epsilon;
    const std::size_t limit = buffer.m_CacheLimit;
    bool isCaching = buffer.m_Offsets.empty() == false;
    const bool hasViscosity = m_Settings.Viscosity > 0.0f;
    const bool isReusing = search == NeighborSearch::ReuseVerlet;
    if (isReusing == false)
    {
        buffer.m_Neighbors.clear();
        buffer.m_CachedRowCount = 0u;
        buffer.m_HasOverflowed = false;
        buffer.m_IsVerlet = search == NeighborSearch::BuildVerlet;
    }
    // 総候補と半径内の候補は担当範囲内で集計し、同期後にProfilerへ登録します。
    uint64_t candidateVisitCount = 0u;
    uint64_t acceptedNeighborCount = 0u;

    const auto appendNeighbor = [&](uint32_t index, float distanceSquared)
    {
        if (buffer.m_Neighbors.size() == limit)
        {
            // 通常キャッシュの部分利用では途中の行を破棄します。Verletは全行が揃ったときだけ使います。
            isCaching = false;
            buffer.m_HasOverflowed = true;
            if (buffer.m_IsVerlet == false && m_Settings.ReuseCompletedNeighborRowsOnOverflow == true)
            {
                buffer.m_Neighbors.resize(buffer.m_Offsets[buffer.m_CachedRowCount]);
            }
            else
            {
                buffer.m_CachedRowCount = 0u;
            }
            return;
        }
        if (buffer.m_Neighbors.size() == buffer.m_Neighbors.capacity())
        {
            const std::size_t capacity = buffer.m_Neighbors.capacity();
            const std::size_t growth = std::max(InitialNeighborCapacity, capacity);
            buffer.m_Neighbors.reserve(capacity + std::min(growth, limit - capacity));
        }
        buffer.m_Neighbors.push_back({index, distanceSquared});
    };

    const auto computeRange = [&]<NeighborSearch Search>()
    {
        for (std::size_t i = buffer.m_RangeBegin; i < buffer.m_RangeEnd; ++i)
        {
            const uint64_t previousCandidates = candidateVisitCount;
            const uint64_t previousAccepted = acceptedNeighborCount;
            FluidParticle& particle = particles[i];
            float density = 0.0f;
            const auto accumulateDensity = [&](const FluidParticle& neighbor, float rSq)
            {
                // rho_i = sum_j m_j W(|x_i-x_j|, h)。self contributionも含めます。
                if (rSq <= hSq)
                {
                    ++acceptedNeighborCount;
                    density += std::max(0.0f, neighbor.Mass) * m_Kernel.EvaluatePoly6Density(rSq);
                }
            };
            if constexpr (Search == NeighborSearch::ReuseVerlet)
            {
                const std::size_t row = i - buffer.m_RangeBegin;
                const std::size_t begin = buffer.m_Offsets[row];
                const std::size_t end = buffer.m_Offsets[row + 1u];
                candidateVisitCount += end - begin;
                for (std::size_t index = begin; index < end; ++index)
                {
                    CachedNeighbor& cached = buffer.m_Neighbors[index];
                    const FluidParticle& neighbor = particles[cached.ParticleIndex];
                    cached.DistanceSquared = (particle.Position - neighbor.Position).LengthSq();
                    accumulateDensity(neighbor, cached.DistanceSquared);
                }
            }
            else
            {
                m_SpatialHash.ForEachPreparedNeighborParticle(particle.Position, searchRadius, [&](uint32_t j)
                {
                    ++candidateVisitCount;
                    if (j >= particles.size())
                    {
                        return;
                    }

                    const FluidParticle& neighbor = particles[j];
                    const float rSq = (particle.Position - neighbor.Position).LengthSq();
                    accumulateDensity(neighbor, rSq);
                    if constexpr (Search == NeighborSearch::BuildVerlet)
                    {
                        // 後のSubstepでh内へ入る候補と、Densityに必要なselfを探索順で保持します。
                        if (isCaching == true && rSq <= searchRadiusSq)
                        {
                            appendNeighbor(j, rSq);
                        }
                    }
                    else if (isCaching == true && rSq <= hSq && j != i && rSq > nearZeroDistanceSq)
                    {
                        // 厳密なh判定済みのForce候補だけを保存します。
                        appendNeighbor(j, rSq);
                    }
                });
                if (isCaching == true)
                {
                    ++buffer.m_CachedRowCount;
                    buffer.m_Offsets[buffer.m_CachedRowCount] = buffer.m_Neighbors.size();
                }
            }
            if (forceWork.empty() == false)
            {
                forceWork[i] = candidateVisitCount - previousCandidates
                    + ForceAcceptedNeighborWork * (acceptedNeighborCount - previousAccepted);
            }
            particle.Density = density;
            if (forceTerms.empty() == false)
            {
                particle.Pressure = EvaluatePressure(density);
                forceTerms[i] = EvaluateNeighborForceTerms(particle, hasViscosity);
            }
        }
    };

    switch (search)
    {
    case NeighborSearch::Grid:
        computeRange.template operator()<NeighborSearch::Grid>();
        break;
    case NeighborSearch::BuildVerlet:
        computeRange.template operator()<NeighborSearch::BuildVerlet>();
        break;
    case NeighborSearch::ReuseVerlet:
        computeRange.template operator()<NeighborSearch::ReuseVerlet>();
        break;
    }
    buffer.m_Candidates = candidateVisitCount;
    buffer.m_Accepted = acceptedNeighborCount;
}

void SPHSolver::ComputePressure(std::vector<FluidParticle>& particles) const
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Pressure", Diagnostics::DiagnosticCategory::Physics);
    for (FluidParticle& particle : particles)
    {
        particle.Pressure = EvaluatePressure(particle.Density);
    }
}

void SPHSolver::ComputeDensityAndPressure(std::vector<FluidParticle>& particles)
{
    ComputeDensity(particles);
    ComputePressure(particles);
}

float SPHSolver::EvaluatePressure(float density) const
{
    return m_Settings.PressureStiffness * (density - m_Settings.RestDensity);
}

SPHSolver::NeighborForceTerms SPHSolver::EvaluateNeighborForceTerms(
    const FluidParticle& particle, bool hasViscosity) const
{
    const float mass = std::max(0.0f, particle.Mass);
    if (mass <= math::Epsilon || particle.Density <= math::Epsilon)
    {
        return {};
    }

    return {mass, particle.Pressure / (particle.Density * particle.Density),
        hasViscosity ? 1.0f / particle.Density : 0.0f};
}

void SPHSolver::ComputeForces(std::vector<FluidParticle>& particles)
{
    m_SpatialHash.Build(particles);
    // 単独APIでは呼び出し側が渡したDensityとPressureから項を作ります。
    {
        Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.NeighborForceTerms",
            Diagnostics::DiagnosticCategory::Physics);
        const bool hasViscosity = m_Settings.Viscosity > 0.0f;
        m_NeighborForceTerms.resize(particles.size());
        for (std::size_t i = 0u; i < particles.size(); ++i)
        {
            m_NeighborForceTerms[i] = EvaluateNeighborForceTerms(particles[i], hasViscosity);
        }
    }
    ComputeForcesUsingCurrentGrid(particles, {}, nullptr);
}

void SPHSolver::PlanForceRanges(std::size_t particleCount, std::span<const DensityJobBuffer> cachedRanges,
    std::span<std::size_t> rangeEnds)
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.ForceRangePlanning",
        Diagnostics::DiagnosticCategory::Physics);
    uint64_t cumulativeWork = 0u;
    std::size_t cacheIndex = 0u;
    for (std::size_t index = 0u; index < particleCount; ++index)
    {
        uint64_t work = m_ForceWork[index];
        if (cachedRanges.empty() == false)
        {
            if (index == cachedRanges[cacheIndex].m_RangeEnd)
            {
                ++cacheIndex;
            }
            const auto& cache = cachedRanges[cacheIndex];
            const std::size_t row = index - cache.m_RangeBegin;
            if (row < cache.m_CachedRowCount)
            {
                // 完成行はGridを引かないため、実際に読む候補数で見積もります。
                work = ForceAcceptedNeighborWork * (cache.m_Offsets[row + 1u] - cache.m_Offsets[row]);
            }
        }
        cumulativeWork += work + ForceParticleWork;
        m_ForceWork[index] = cumulativeWork;
    }

    const uint64_t quotient = cumulativeWork / rangeEnds.size();
    const uint64_t remainder = cumulativeWork % rangeEnds.size();
    std::size_t rangeBegin = 0u;
    for (std::size_t index = 0u; index + 1u < rangeEnds.size(); ++index)
    {
        const std::size_t boundary = index + 1u;
        const uint64_t target = quotient * boundary + std::min<uint64_t>(remainder, boundary);
        const auto found = std::lower_bound(m_ForceWork.begin(), m_ForceWork.end(), target);
        const std::size_t end = static_cast<std::size_t>(found - m_ForceWork.begin()) + 1u;
        rangeEnds[index] = std::clamp(end, rangeBegin + MinimumParticlesPerWeightedForceRange,
            particleCount - (rangeEnds.size() - boundary) * MinimumParticlesPerWeightedForceRange);
        rangeBegin = rangeEnds[index];
    }
    rangeEnds.back() = particleCount;
}

void SPHSolver::ComputeForcesUsingCurrentGrid(
    std::vector<FluidParticle>& particles, std::span<const DensityJobBuffer> cachedRanges, JobSystem* pJobSystem)
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Force", Diagnostics::DiagnosticCategory::Physics);
    std::size_t jobLimit = 0u;
    if (pJobSystem != nullptr && particles.size() >= MinimumParticlesForForceJobs)
    {
        // 呼び出し元も最後の範囲を担当するため、その1範囲を残します。
        jobLimit = std::min({MaximumWorkerJobs, pJobSystem->GetWorkerCount(),
            particles.size() / MinimumParticlesPerForceRange - 1u});
    }

    std::array<std::future<ForceNeighborCounts>, MaximumWorkerJobs> jobs;
    std::size_t jobCount = 0u;
    std::size_t rangeBegin = 0u;
    const std::size_t rangeSize = particles.size() / (jobLimit + 1u);
    std::array<std::size_t, MaximumWorkerJobs + 1u> rangeEnds{};
    const bool isWeighted = jobLimit > 0u && m_ForceWork.size() == particles.size();
    if (isWeighted == true)
    {
        PlanForceRanges(particles.size(), cachedRanges,
            std::span<std::size_t>(rangeEnds.data(), jobLimit + 1u));
    }
    else
    {
        for (std::size_t index = 0u; index < jobLimit; ++index)
        {
            rangeEnds[index] = (index + 1u) * rangeSize;
        }
    }
    RecordSPHCounter("Physics.Fluid.SPH.ForceWeightedRangesUsed", isWeighted ? 1.0 : 0.0);
    const bool shouldMeasureJobs = CPUProfiler::Get().IsEnabled()
        || Diagnostics::IsMeasurementsEnabled();
    if (jobLimit > 0u)
    {
        Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.ForceJobSubmit",
            Diagnostics::DiagnosticCategory::Physics);
        for (; jobCount < jobLimit; ++jobCount)
        {
            try
            {
                // Grid、近傍キャッシュ、前計算済みの項は読取専用です。
                // Jobは担当粒子のForceだけを書き、近傍粒子のForceは読みません。
                const std::size_t rangeEnd = rangeEnds[jobCount];
                jobs[jobCount] = pJobSystem->Submit(
                    [this, &particles, rangeBegin, rangeEnd, cachedRanges, shouldMeasureJobs]()
                    {
                        const auto start = shouldMeasureJobs ? Clock::now() : Clock::time_point{};
                        ForceNeighborCounts result = ComputeForceRange(
                            particles, rangeBegin, rangeEnd, cachedRanges);
                        if (shouldMeasureJobs == true)
                        {
                            result.m_Milliseconds =
                                std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                        }
                        return result;
                    });
            }
            catch (...)
            {
                // 投入済みJobが借用中のデータを保ち、未投入範囲を呼び出し元で処理します。
                break;
            }
            if (jobs[jobCount].valid() == false)
            {
                break;
            }
            rangeBegin = rangeEnds[jobCount];
        }
    }

    ForceNeighborCounts counts = ComputeForceRange(
        particles, rangeBegin, particles.size(), cachedRanges);
    uint64_t minimumRangeCandidates = counts.m_Candidates;
    uint64_t maximumRangeCandidates = counts.m_Candidates;
    double minimumJobMilliseconds = std::numeric_limits<double>::max();
    double maximumJobMilliseconds = 0.0;
    // 積分でPositionとVelocityを書き換える前に、借用中のJobをすべて完了させます。
    if (jobCount > 0u)
    {
        Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.ForceJobWait",
            Diagnostics::DiagnosticCategory::Physics);
        for (std::size_t index = 0u; index < jobCount; ++index)
        {
            const ForceNeighborCounts jobCounts = jobs[index].get();
            minimumRangeCandidates = std::min(minimumRangeCandidates, jobCounts.m_Candidates);
            maximumRangeCandidates = std::max(maximumRangeCandidates, jobCounts.m_Candidates);
            counts.m_Candidates += jobCounts.m_Candidates;
            counts.m_Accepted += jobCounts.m_Accepted;
            minimumJobMilliseconds = std::min(minimumJobMilliseconds, jobCounts.m_Milliseconds);
            maximumJobMilliseconds = std::max(maximumJobMilliseconds, jobCounts.m_Milliseconds);
        }
    }

    // Workerは計測値をfutureで返し、共有Profilerへの登録は同期後に行います。
    if (jobCount > 0u && shouldMeasureJobs == true)
    {
        RecordSPHCounter("Physics.Fluid.SPH.ForceRangeMinCandidateCount", static_cast<double>(minimumRangeCandidates));
        RecordSPHCounter("Physics.Fluid.SPH.ForceRangeMaxCandidateCount", static_cast<double>(maximumRangeCandidates));
        RecordSPHCounter("Physics.Fluid.SPH.ForceJobMinMilliseconds", minimumJobMilliseconds);
        RecordSPHCounter("Physics.Fluid.SPH.ForceJobMaxMilliseconds", maximumJobMilliseconds);
    }

    const uint64_t candidateVisitCount = counts.m_Candidates;
    const uint64_t acceptedNeighborCount = counts.m_Accepted;
    CPUProfiler& profiler = CPUProfiler::Get();
    RecordSPHCounter("Physics.Fluid.SPH.ForceJobCount", static_cast<double>(jobCount));
    profiler.AddCounter(
        "Physics.Fluid.SPH.ForceNeighborCandidateCount",
        static_cast<double>(candidateVisitCount));
    profiler.AddCounter(
        "Physics.Fluid.SPH.ForceNeighborAcceptedCount",
        static_cast<double>(acceptedNeighborCount));
    Diagnostics::RecordCounter("Physics.Fluid.SPH.ForceNeighborCandidateCount",
        static_cast<double>(candidateVisitCount), Diagnostics::DiagnosticCategory::Physics);
    Diagnostics::RecordCounter("Physics.Fluid.SPH.ForceNeighborAcceptedCount",
        static_cast<double>(acceptedNeighborCount), Diagnostics::DiagnosticCategory::Physics);

    const double forceAcceptanceRatio = candidateVisitCount > 0u
        ? static_cast<double>(acceptedNeighborCount) / static_cast<double>(candidateVisitCount)
        : 0.0;
    const double averageForceNeighbors = particles.empty() == false
        ? static_cast<double>(acceptedNeighborCount) / static_cast<double>(particles.size())
        : 0.0;

    profiler.AddCounter(
        "Physics.Fluid.SPH.ForceNeighborAcceptanceRatio",
        forceAcceptanceRatio);
    profiler.AddCounter(
        "Physics.Fluid.SPH.ForceAverageAcceptedNeighborsPerParticle",
        averageForceNeighbors);
}

SPHSolver::ForceNeighborCounts SPHSolver::ComputeForceRange(std::vector<FluidParticle>& particles,
    std::size_t rangeBegin, std::size_t rangeEnd,
    std::span<const DensityJobBuffer> cachedRanges) const noexcept
{
    const float h = m_Settings.SmoothingRadius;
    const float hSq = h * h;

    // Force側はself、無効Mass/Density、support radius外を除外した後の数をAcceptedとします。
    // Density側との差を見ることで、密度計算では有効でも力計算には使われない候補量も追跡できます。
    ForceNeighborCounts counts;

    std::size_t cacheIndex = 0u;
    while (cacheIndex + 1u < cachedRanges.size() && rangeBegin >= cachedRanges[cacheIndex].m_RangeEnd)
    {
        ++cacheIndex;
    }
    const auto computeRange = [&]<bool HasViscosity>()
    {
        for (std::size_t i = rangeBegin; i < rangeEnd; ++i)
        {
            if (cacheIndex + 1u < cachedRanges.size() && i == cachedRanges[cacheIndex].m_RangeEnd)
            {
                ++cacheIndex;
            }
            FluidParticle& particle = particles[i];
            const NeighborForceTerms& selfTerms = m_NeighborForceTerms[i];
            const float massI = std::max(0.0f, particle.Mass);
            math::Vec3 force = m_Settings.Gravity * massI;
            if (selfTerms.Mass <= math::Epsilon)
            {
                particle.Force = force;
                continue;
            }

            const float pressureTermI = selfTerms.PressureOverDensitySquared;
            const float viscosityMassI = massI * m_Settings.Viscosity;
            const auto applyForce = [&](uint32_t neighborIndex, const math::Vec3& displacement, float rSq)
            {
                const NeighborForceTerms& terms = m_NeighborForceTerms[neighborIndex];
                if (terms.Mass <= math::Epsilon)
                {
                    return;
                }

                // ここまで通過したParticleだけがPressure / Viscosityの実際の相互作用に参加します。
                ++counts.m_Accepted;

                const float r = std::sqrt(rSq);
                if (r > h)
                {
                    // rSq <= hSqでも、平方根と境界値の丸めが異なる場合があります。
                    return;
                }
                const float remainingRadius = h - r;
                if (r > math::Epsilon)
                {
                    const float pressureTerm = pressureTermI + terms.PressureOverDensitySquared;
                    force += m_Kernel.EvaluateSpikyGradientInSupport(displacement, r, remainingRadius)
                        * (-massI * terms.Mass * pressureTerm);
                }

                if constexpr (HasViscosity)
                {
                    force += (particles[neighborIndex].Velocity - particle.Velocity)
                        * (viscosityMassI * terms.Mass * terms.InverseDensity
                            * m_Kernel.EvaluateViscosityLaplacianInSupport(remainingRadius));
                }
            };

            const DensityJobBuffer* pCache = cachedRanges.empty() ? nullptr : &cachedRanges[cacheIndex];
            if (pCache != nullptr && i - pCache->m_RangeBegin < pCache->m_CachedRowCount)
            {
                const std::size_t row = i - pCache->m_RangeBegin;
                const std::size_t begin = pCache->m_Offsets[row];
                const std::size_t end = pCache->m_Offsets[row + 1u];
                counts.m_Candidates += end - begin;
                for (std::size_t index = begin; index < end; ++index)
                {
                    const CachedNeighbor& cached = pCache->m_Neighbors[index];
                    if (pCache->m_IsVerlet && (cached.DistanceSquared > hSq
                        || cached.DistanceSquared <= math::Epsilon * math::Epsilon))
                    {
                        continue;
                    }
                    const FluidParticle& neighbor = particles[cached.ParticleIndex];
                    applyForce(cached.ParticleIndex, particle.Position - neighbor.Position, cached.DistanceSquared);
                }
            }
            else
            {
                // 粒子近傍キャッシュの上限を超えても、同じSubstepの密度計算が準備したCell一覧は再利用できます。
                // 単独ComputeForcesや準備上限に達したGridでは、既存のCell探索へ戻ります。
                m_SpatialHash.ForEachPreparedNeighborParticle(particle.Position, h, [&](uint32_t j)
                {
                    ++counts.m_Candidates;
                    if (j >= particles.size() || j == static_cast<uint32_t>(i))
                    {
                        return;
                    }

                    const FluidParticle& neighbor = particles[j];
                    const math::Vec3 displacement = particle.Position - neighbor.Position;
                    const float rSq = displacement.LengthSq();
                    if (rSq <= math::Epsilon * math::Epsilon || rSq > hSq)
                    {
                        return;
                    }
                    applyForce(j, displacement, rSq);
                });
            }
            // 近傍の加算順は維持し、担当粒子のForceへの書き込みを1回にします。
            particle.Force = force;
        }
    };
    if (m_Settings.Viscosity > 0.0f)
    {
        computeRange.template operator()<true>();
    }
    else
    {
        computeRange.template operator()<false>();
    }

    return counts;
}

float SPHSolver::ComputeStableTimeStep(
    const std::vector<FluidParticle>& particles,
    float maximumDeltaTime) const
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.StableTimeStep",
        Diagnostics::DiagnosticCategory::Physics);
    if (maximumDeltaTime <= 0.0f)
    {
        return 0.0f;
    }
    if (m_Settings.StableTimeStepEnabled == false)
    {
        return maximumDeltaTime;
    }

    return ComputeStableTimeStep(ScanParticleStatistics(particles), maximumDeltaTime);
}

void SPHSolver::ParticleStatistics::Accumulate(const FluidParticle& particle, float mass)
{
    m_MaximumDensity = std::max(m_MaximumDensity, particle.Density);
    m_MaximumAbsolutePressure = std::max(m_MaximumAbsolutePressure, std::abs(particle.Pressure));
    m_MaximumSpeedSquared = std::max(m_MaximumSpeedSquared, LengthSquaredForStatistics(particle.Velocity));
    if (mass > math::Epsilon && std::isfinite(mass))
    {
        // Force二乗 / Mass二乗の大小を交差積で比較し、粒子ごとの除算と平方根を省きます。
        // float成分から作った双方の二乗の積は、有限値であればdoubleの範囲に収まります。
        const double forceSquared = LengthSquaredForStatistics(particle.Force);
        const double wideMass = mass;
        const double massSquared = wideMass * wideMass;
        if (forceSquared * m_MaximumAccelerationMassSquared > m_MaximumAccelerationForceSquared * massSquared)
        {
            m_MaximumAccelerationForceSquared = forceSquared;
            m_MaximumAccelerationMassSquared = massSquared;
            m_MaximumAccelerationMass = mass;
        }
    }
}

float SPHSolver::ParticleStatistics::GetMaximumSpeed() const
{
    return NarrowMagnitude(std::sqrt(m_MaximumSpeedSquared));
}

float SPHSolver::ParticleStatistics::GetMaximumAcceleration() const
{
    // 通常値は選ばれた粒子に対して従来のsqrt(float) / massを適用し、丸めを維持します。
    if (m_MaximumAccelerationForceSquared >= std::numeric_limits<float>::min()
        && m_MaximumAccelerationForceSquared <= std::numeric_limits<float>::max())
    {
        return std::sqrt(static_cast<float>(m_MaximumAccelerationForceSquared)) / m_MaximumAccelerationMass;
    }
    return NarrowMagnitude(std::sqrt(m_MaximumAccelerationForceSquared / m_MaximumAccelerationMassSquared));
}

SPHSolver::ParticleStatistics SPHSolver::ScanParticleStatistics(
    const std::vector<FluidParticle>& particles) const
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.InitialParticleStatistics",
        Diagnostics::DiagnosticCategory::Physics);
    ParticleStatistics statistics;
    for (const FluidParticle& particle : particles)
    {
        statistics.Accumulate(particle, std::max(0.0f, particle.Mass));
    }
    return statistics;
}

float SPHSolver::ComputeStableTimeStep(const ParticleStatistics& statistics, float maximumDeltaTime) const
{
    const float maximumSpeed = statistics.GetMaximumSpeed();
    const float maximumAcceleration = statistics.GetMaximumAcceleration();
    // 線形EOS p=k(rho-rho0) では c^2=dp/drho=k。数値的な特性速度としてsqrt(k)を使います。
    float speedOfSound = 0.0f;
    if (m_Settings.PressureStiffness > 0.0f)
    {
        speedOfSound = std::sqrt(m_Settings.PressureStiffness)
            * m_Settings.SpeedOfSoundScale;
    }

    float stableDt = maximumDeltaTime;
    const float characteristicSpeed = std::max(maximumSpeed, speedOfSound);
    if (m_Settings.CFLFactor > 0.0f && characteristicSpeed > math::Epsilon)
    {
        stableDt = std::min(
            stableDt,
            m_Settings.CFLFactor * m_Settings.SmoothingRadius / characteristicSpeed);
    }
    if (m_Settings.AccelerationTimeStepFactor > 0.0f
        && maximumAcceleration > math::Epsilon)
    {
        stableDt = std::min(
            stableDt,
            m_Settings.AccelerationTimeStepFactor
                * std::sqrt(m_Settings.SmoothingRadius / maximumAcceleration));
    }
    if (m_Settings.MinimumTimeStep > 0.0f)
    {
        stableDt = std::max(stableDt, m_Settings.MinimumTimeStep);
    }
    return std::min(stableDt, maximumDeltaTime);
}

void SPHSolver::Integrate(std::vector<FluidParticle>& particles, float deltaTime) const
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Integrate", Diagnostics::DiagnosticCategory::Physics);
    if (deltaTime <= 0.0f)
    {
        return;
    }
    for (FluidParticle& particle : particles)
    {
        IntegrateParticle(particle, std::max(0.0f, particle.Mass), deltaTime);
    }
}

void SPHSolver::ResolveBoundary(std::vector<FluidParticle>& particles) const
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.ResolveBoundary", Diagnostics::DiagnosticCategory::Physics);
    if (m_Settings.BoundaryEnabled == false)
    {
        return;
    }
    const BoundaryBounds bounds = ComputeBoundaryBounds(m_Settings);
    for (FluidParticle& particle : particles)
    {
        ResolveParticleBoundary(particle, bounds, m_Settings.BoundaryRestitution);
    }
}

bool SPHSolver::CanReuseVerletList(const std::vector<FluidParticle>& particles, const VerletStepState& verlet) const
{
    if (verlet.m_HasValidList == false || particles.size() != m_VerletReferencePositions.size())
    {
        return false;
    }

    Diagnostics::TraceScope trace("Physics.Fluid.SPH.VerletDisplacementCheck", Diagnostics::DiagnosticCategory::Physics);
    for (std::size_t index = 0u; index < particles.size(); ++index)
    {
        const auto& current = particles[index].Position;
        const auto& reference = m_VerletReferencePositions[index];
        const double dx = static_cast<double>(current.x) - reference.x;
        const double dy = static_cast<double>(current.y) - reference.y;
        const double dz = static_cast<double>(current.z) - reference.z;
        // 2粒子が接近する場合でも合計移動量がskinに達する前に作り直します。
        if ((dx * dx + dy * dy + dz * dz < verlet.m_MaximumDisplacementSquared) == false)
        {
            return false;
        }
    }
    return true;
}

SPHSolver::ParticleStatistics SPHSolver::AdvanceSubstep(
    std::vector<FluidParticle>& particles, float deltaTime, JobSystem* pJobSystem, VerletStepState* pVerlet)
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Substep",
        Diagnostics::DiagnosticCategory::Physics);
    // Neighbor関係と圧力は位置に依存するため、各Substepで再計算します。
    const auto cachedRanges = ComputeDensity(particles, true, pJobSystem, pVerlet);
    ComputeForcesUsingCurrentGrid(particles, cachedRanges, pJobSystem);
    RecordSPHCounter("Physics.Fluid.SPH.ForceNeighborCacheUsed",
        cachedRanges.empty() ? 0.0 : 1.0);
    Diagnostics::ProfiledTraceScope passTrace("Physics.Fluid.SPH.IntegrateBoundaryStatistics",
        Diagnostics::DiagnosticCategory::Physics);
    const BoundaryBounds bounds = m_Settings.BoundaryEnabled ? ComputeBoundaryBounds(m_Settings) : BoundaryBounds{};
    ParticleStatistics statistics;
    for (FluidParticle& particle : particles)
    {
        const float mass = std::max(0.0f, particle.Mass);
        IntegrateParticle(particle, mass, deltaTime);
        if (m_Settings.BoundaryEnabled == true)
        {
            // 無効Massで積分を省略した粒子もBox内へ戻し、反射後の速度をCFLへ渡します。
            ResolveParticleBoundary(particle, bounds, m_Settings.BoundaryRestitution);
        }
        statistics.Accumulate(particle, mass);
    }
    return statistics;
}

void SPHSolver::Step(std::vector<FluidParticle>& particles, float deltaTime, JobSystem* pJobSystem)
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Step", Diagnostics::DiagnosticCategory::Physics);

    m_LastSubstepCount = 0u;
    m_LastMinimumSubstepDeltaTime = 0.0f;
    m_LastSubstepLimitReached = false;
    if (deltaTime <= 0.0f || particles.empty())
    {
        return;
    }

    ParticleStatistics statistics;
    if (m_Settings.StableTimeStepEnabled == false)
    {
        statistics = AdvanceSubstep(particles, deltaTime, pJobSystem);
        m_LastSubstepCount = 1u;
        m_LastMinimumSubstepDeltaTime = deltaTime;
    }
    else
    {
        // 外部で並び替えや再初期化ができるため、有効なリストをStep間で引き継ぎません。
        VerletStepState verlet;
        VerletStepState* pVerlet = nullptr;
        if (m_Settings.VerletSkinScale > 0.0f && m_Settings.MaximumCachedNeighbors >= particles.size()
            && m_Settings.MaximumSubsteps > 1u)
        {
            const float h = m_Settings.SmoothingRadius;
            const float skin = h * m_Settings.VerletSkinScale;
            const float searchRadius = h + skin;
            // floatの距離判定と探索半径の丸めを見込み、skin/2よりわずかに早く再構築します。
            const double guard = 8.0 * std::numeric_limits<float>::epsilon() * searchRadius;
            const double maximumDisplacement = 0.5 * (std::min<double>(skin, static_cast<double>(searchRadius) - h) - guard);
            if (maximumDisplacement > 0.0 && std::isfinite(searchRadius * searchRadius))
            {
                verlet.m_SearchRadius = searchRadius;
                verlet.m_MaximumDisplacementSquared = maximumDisplacement * maximumDisplacement;
                pVerlet = &verlet;
            }
        }
        // 呼び出し元が前Step後に粒子を更新できるため、初回のCFLだけ再走査します。
        statistics = ScanParticleStatistics(particles);
        float remainingTime = deltaTime;
        float minimumSubstepDeltaTime = std::numeric_limits<float>::max();
        while (remainingTime > math::Epsilon
            && m_LastSubstepCount < m_Settings.MaximumSubsteps)
        {
            float substepDt = ComputeStableTimeStep(statistics, remainingTime);
            const uint32_t remainingSlots = m_Settings.MaximumSubsteps - m_LastSubstepCount;
            if (remainingSlots == 1u && substepDt + math::Epsilon < remainingTime)
            {
                // 安全なdtでは残時間を消化できない状態を記録し、最後のslotでFrame時間を消化します。
                m_LastSubstepLimitReached = true;
                substepDt = remainingTime;
            }
            if (substepDt <= 0.0f)
            {
                break;
            }
            substepDt = std::min(substepDt, remainingTime);
            statistics = AdvanceSubstep(particles, substepDt, pJobSystem, pVerlet);
            remainingTime -= substepDt;
            minimumSubstepDeltaTime = std::min(minimumSubstepDeltaTime, substepDt);
            ++m_LastSubstepCount;
        }
        if (m_LastSubstepCount > 0u)
        {
            m_LastMinimumSubstepDeltaTime = minimumSubstepDeltaTime;
        }
    }

    // 粒子走査中にProfilerを更新せず、最後のSubstepで得た統計をここで発行します。
    CPUProfiler& profiler = CPUProfiler::Get();
    profiler.AddCounter("Physics.Fluid.SPH.ParticleCount", static_cast<double>(particles.size()));
    profiler.AddCounter("Physics.Fluid.SPH.SubstepCount", static_cast<double>(m_LastSubstepCount));
    profiler.AddCounter("Physics.Fluid.SPH.MinimumSubstepMilliseconds",
        static_cast<double>(m_LastMinimumSubstepDeltaTime) * 1000.0);
    profiler.AddCounter("Physics.Fluid.SPH.SubstepLimitReached",
        m_LastSubstepLimitReached ? 1.0 : 0.0);
    // Particle/Neighbor loopでは発行せず、Fixed Stepの集計値を同じFrame IDで記録します。
    Diagnostics::RecordCounter("Physics.Fluid.SPH.ParticleCount",
        static_cast<double>(particles.size()), Diagnostics::DiagnosticCategory::Physics);
    Diagnostics::RecordCounter("Physics.Fluid.SPH.SubstepCount",
        static_cast<double>(m_LastSubstepCount), Diagnostics::DiagnosticCategory::Physics);
    Diagnostics::RecordCounter("Physics.Fluid.SPH.SubstepLimitReached",
        m_LastSubstepLimitReached ? 1.0 : 0.0, Diagnostics::DiagnosticCategory::Physics);
    profiler.AddCounter("Physics.Fluid.SPH.MaximumDensity", static_cast<double>(statistics.m_MaximumDensity));
    profiler.AddCounter("Physics.Fluid.SPH.MaximumAbsolutePressure",
        static_cast<double>(statistics.m_MaximumAbsolutePressure));
    profiler.AddCounter("Physics.Fluid.SPH.MaximumSpeed", static_cast<double>(statistics.GetMaximumSpeed()));
    profiler.AddCounter("Physics.Fluid.SPH.MaximumAcceleration",
        static_cast<double>(statistics.GetMaximumAcceleration()));
}

} // namespace ph
} // namespace Raven
