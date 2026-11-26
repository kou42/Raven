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
// 小さい範囲ではJob投入・待機の固定費が目立つため、粒子数とJob数の双方を制限します。
constexpr std::size_t MinimumParticlesPerForceRange = 512u;
constexpr std::size_t MaximumForceJobs = 7u;

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
        std::vector<CachedNeighbor>().swap(m_CachedNeighbors);
        std::vector<std::size_t>().swap(m_CachedNeighborOffsets);
    }
    m_Settings = settings;
    m_Settings.SmoothingRadius = std::max(settings.SmoothingRadius, MinimumSmoothingRadius);
    m_Kernel = SPHKernel(m_Settings.SmoothingRadius);
    m_Settings.SpatialHashCellSizeScale = std::max(
        settings.SpatialHashCellSizeScale,
        MinimumSpatialHashCellSizeScale);
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
    ComputeDensity(particles, false);
}

std::size_t SPHSolver::ComputeDensity(std::vector<FluidParticle>& particles, bool isSubstep)
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Density", Diagnostics::DiagnosticCategory::Physics);
    m_SpatialHash.Build(particles);
    const float h = m_Settings.SmoothingRadius;
    const float hSq = h * h;
    if (isSubstep == true)
    {
        // 診断TraceはGrid側で発行し、ここではCPUProfiler用の区間だけを追加します。
        CPUProfileScope bucketProfile("Physics.Fluid.SPH.PrepareNeighborBuckets");
        m_SpatialHash.PrepareNeighborBuckets(h);
    }
    const float nearZeroDistanceSq = math::Epsilon * math::Epsilon;
    const std::size_t limit = m_Settings.MaximumCachedNeighbors;
    bool isCaching = isSubstep && limit > 0u;
    bool hasOverflowed = false;
    std::size_t cachedRowCount = 0u;
    if (isSubstep == true)
    {
        m_CachedNeighbors.clear();
    }
    if (isCaching == true)
    {
        m_CachedNeighborOffsets.resize(particles.size() + 1);
        m_CachedNeighborOffsets[0] = 0u;
    }

    // Spatial Hashが返したCell候補数と、厳密なsupport radius判定を通過した数を分離します。
    // Hot loopでは整数加算だけに留め、Profiler登録は処理末尾でまとめて行います。
    // Candidate/Accepted比を見ることで、CellSizeや検索範囲に無駄があるか判断できます。
    uint64_t candidateVisitCount = 0u;
    uint64_t acceptedNeighborCount = 0u;

    {
        Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.DensityNeighborLoop",
            Diagnostics::DiagnosticCategory::Physics);
        for (std::size_t i = 0u; i < particles.size(); ++i)
        {
            FluidParticle& particle = particles[i];
            float density = 0.0f;
            // rho_i = sum_j m_j W(|x_i-x_j|, h)。self contributionも含めます。
            m_SpatialHash.ForEachPreparedNeighborParticle(particle.Position, h, [&](uint32_t j)
            {
                ++candidateVisitCount;

                if (j >= particles.size())
                {
                    return;
                }

                const FluidParticle& neighbor = particles[j];
                const float rSq = (particle.Position - neighbor.Position).LengthSq();
                if (rSq > hSq)
                {
                    return;
                }

                ++acceptedNeighborCount;
                density += std::max(0.0f, neighbor.Mass)
                    * m_Kernel.EvaluatePoly6Density(rSq);

                // 圧力はこの後に更新されるため、ここでは位置だけで確定する条件を保存します。
                if (isCaching == true && j != i && (rSq <= nearZeroDistanceSq) == false)
                {
                    if (m_CachedNeighbors.size() == limit)
                    {
                        // 途中の行を破棄して、Forceはその行以降をGridから再探索します。
                        // 全体フォールバックでは完成済みの行も使用しません。
                        isCaching = false;
                        hasOverflowed = true;
                        if (m_Settings.ReuseCompletedNeighborRowsOnOverflow == true)
                        {
                            m_CachedNeighbors.resize(m_CachedNeighborOffsets[cachedRowCount]);
                        }
                        else
                        {
                            cachedRowCount = 0u;
                        }
                        return;
                    }
                    if (m_CachedNeighbors.size() == m_CachedNeighbors.capacity())
                    {
                        const std::size_t capacity = m_CachedNeighbors.capacity();
                        const std::size_t growth = std::max(InitialNeighborCapacity, capacity);
                        m_CachedNeighbors.reserve(capacity + std::min(growth, limit - capacity));
                    }
                    m_CachedNeighbors.push_back({j, rSq});
                }
            });
            particle.Density = density;
            if (isCaching == true)
            {
                ++cachedRowCount;
                m_CachedNeighborOffsets[cachedRowCount] = m_CachedNeighbors.size();
            }
        }
    }

    if (isSubstep == true)
    {
        RecordSPHCounter("Physics.Fluid.SPH.NeighborCacheCount",
            static_cast<double>(m_CachedNeighbors.size()));
        RecordSPHCounter("Physics.Fluid.SPH.NeighborCacheOverflow",
            hasOverflowed ? 1.0 : 0.0);
        // 使用中の件数と区別して、再利用のため保持している行オフセットも含めます。
        const std::size_t capacityBytes = m_CachedNeighbors.capacity() * sizeof(CachedNeighbor)
            + m_CachedNeighborOffsets.capacity() * sizeof(std::size_t);
        RecordSPHCounter("Physics.Fluid.SPH.NeighborCacheCapacityBytes",
            static_cast<double>(capacityBytes));
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
    return cachedRowCount;
}

void SPHSolver::ComputePressure(std::vector<FluidParticle>& particles) const
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Pressure", Diagnostics::DiagnosticCategory::Physics);
    for (FluidParticle& particle : particles)
    {
        particle.Pressure = m_Settings.PressureStiffness
            * (particle.Density - m_Settings.RestDensity);
    }
}

void SPHSolver::ComputeDensityAndPressure(std::vector<FluidParticle>& particles)
{
    ComputeDensity(particles);
    ComputePressure(particles);
}

void SPHSolver::ComputeForces(std::vector<FluidParticle>& particles)
{
    m_SpatialHash.Build(particles);
    ComputeForcesUsingCurrentGrid(particles, 0u, nullptr);
}

void SPHSolver::ComputeForcesUsingCurrentGrid(
    std::vector<FluidParticle>& particles, std::size_t cachedRowCount, JobSystem* pJobSystem)
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Force", Diagnostics::DiagnosticCategory::Physics);
    const bool hasViscosity = m_Settings.Viscosity > 0.0f;

    // DensityとPressureが確定した時点で、複数の近傍から参照される除算を粒子ごとに済ませます。
    // 公開ComputeForcesも呼び出し時点の粒子データから毎回作り直します。
    {
        Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.NeighborForceTerms",
            Diagnostics::DiagnosticCategory::Physics);
        m_NeighborForceTerms.resize(particles.size());
        for (std::size_t i = 0u; i < particles.size(); ++i)
        {
            const FluidParticle& particle = particles[i];
            NeighborForceTerms& terms = m_NeighborForceTerms[i];
            const float mass = std::max(0.0f, particle.Mass);
            if (mass <= math::Epsilon || particle.Density <= math::Epsilon)
            {
                terms = {};
                continue;
            }

            terms.Mass = mass;
            terms.PressureOverDensitySquared =
                particle.Pressure / (particle.Density * particle.Density);
            terms.InverseDensity = hasViscosity ? 1.0f / particle.Density : 0.0f;
        }
    }

    std::size_t jobLimit = 0u;
    if (pJobSystem != nullptr && particles.size() >= 2u * MinimumParticlesPerForceRange)
    {
        // 呼び出し元も最後の範囲を担当するため、その1範囲を残します。
        jobLimit = std::min({MaximumForceJobs, pJobSystem->GetWorkerCount(),
            particles.size() / MinimumParticlesPerForceRange - 1u});
    }

    std::array<std::future<ForceNeighborCounts>, MaximumForceJobs> jobs;
    std::size_t jobCount = 0u;
    std::size_t rangeBegin = 0u;
    const std::size_t rangeSize = particles.size() / (jobLimit + 1u);
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
                jobs[jobCount] = pJobSystem->Submit(
                    [this, &particles, rangeBegin, rangeSize, cachedRowCount, shouldMeasureJobs]()
                    {
                        const auto start = shouldMeasureJobs ? Clock::now() : Clock::time_point{};
                        ForceNeighborCounts result = ComputeForceRange(
                            particles, rangeBegin, rangeBegin + rangeSize, cachedRowCount);
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
            rangeBegin += rangeSize;
        }
    }

    ForceNeighborCounts counts = ComputeForceRange(
        particles, rangeBegin, particles.size(), cachedRowCount);
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
            counts.m_Candidates += jobCounts.m_Candidates;
            counts.m_Accepted += jobCounts.m_Accepted;
            minimumJobMilliseconds = std::min(minimumJobMilliseconds, jobCounts.m_Milliseconds);
            maximumJobMilliseconds = std::max(maximumJobMilliseconds, jobCounts.m_Milliseconds);
        }
    }

    // Workerは計測値をfutureで返し、共有Profilerへの登録は同期後に行います。
    if (jobCount > 0u && shouldMeasureJobs == true)
    {
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
    std::size_t rangeBegin, std::size_t rangeEnd, std::size_t cachedRowCount) const noexcept
{
    const float h = m_Settings.SmoothingRadius;
    const float hSq = h * h;
    const bool hasViscosity = m_Settings.Viscosity > 0.0f;

    // Force側はself、無効Mass/Density、support radius外を除外した後の数をAcceptedとします。
    // Density側との差を見ることで、密度計算では有効でも力計算には使われない候補量も追跡できます。
    ForceNeighborCounts counts;

    for (std::size_t i = rangeBegin; i < rangeEnd; ++i)
    {
        FluidParticle& particle = particles[i];
        const float massI = std::max(0.0f, particle.Mass);
        particle.Force = m_Settings.Gravity * massI;
        if (massI <= math::Epsilon || particle.Density <= math::Epsilon)
        {
            continue;
        }

        const float inverseDensityISq = 1.0f / (particle.Density * particle.Density);
        const float pressureTermI = particle.Pressure * inverseDensityISq;
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
            const float pressureTerm = pressureTermI + terms.PressureOverDensitySquared;
            particle.Force += m_Kernel.EvaluateSpikyGradient(displacement, r)
                * (-massI * terms.Mass * pressureTerm);

            if (hasViscosity == true)
            {
                particle.Force += (particles[neighborIndex].Velocity - particle.Velocity)
                    * (viscosityMassI * terms.Mass * terms.InverseDensity
                        * m_Kernel.EvaluateViscosityLaplacian(r));
            }
        };

        if (i < cachedRowCount)
        {
            const std::size_t begin = m_CachedNeighborOffsets[i];
            const std::size_t end = m_CachedNeighborOffsets[i + 1];
            counts.m_Candidates += end - begin;
            for (std::size_t index = begin; index < end; ++index)
            {
                const CachedNeighbor& cached = m_CachedNeighbors[index];
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

    float maximumSpeed = 0.0f;
    float maximumAcceleration = 0.0f;
    for (const FluidParticle& particle : particles)
    {
        maximumSpeed = std::max(maximumSpeed, particle.Velocity.Length());
        const float mass = std::max(0.0f, particle.Mass);
        if (mass > math::Epsilon)
        {
            maximumAcceleration = std::max(
                maximumAcceleration,
                particle.Force.Length() / mass);
        }
    }

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
        const float mass = std::max(0.0f, particle.Mass);
        if (mass <= math::Epsilon)
        {
            continue;
        }
        const math::Vec3 acceleration = particle.Force / mass;
        particle.Velocity += acceleration * deltaTime;
        particle.Position += particle.Velocity * deltaTime;
    }
}

void SPHSolver::ResolveBoundary(std::vector<FluidParticle>& particles) const
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.ResolveBoundary", Diagnostics::DiagnosticCategory::Physics);
    if (m_Settings.BoundaryEnabled == false)
    {
        return;
    }
    const float radius = m_Settings.BoundaryParticleRadius;
    const float restitution = m_Settings.BoundaryRestitution;
    math::Vec3 minimum = m_Settings.BoundaryMinimum + math::Vec3(radius);
    math::Vec3 maximum = m_Settings.BoundaryMaximum - math::Vec3(radius);
    if (minimum.x > maximum.x)
    {
        const float center = (m_Settings.BoundaryMinimum.x + m_Settings.BoundaryMaximum.x) * 0.5f;
        minimum.x = center;
        maximum.x = center;
    }
    if (minimum.y > maximum.y)
    {
        const float center = (m_Settings.BoundaryMinimum.y + m_Settings.BoundaryMaximum.y) * 0.5f;
        minimum.y = center;
        maximum.y = center;
    }
    if (minimum.z > maximum.z)
    {
        const float center = (m_Settings.BoundaryMinimum.z + m_Settings.BoundaryMaximum.z) * 0.5f;
        minimum.z = center;
        maximum.z = center;
    }
    for (FluidParticle& particle : particles)
    {
        ResolveBoundaryAxis(particle.Position.x, particle.Velocity.x, minimum.x, maximum.x, restitution);
        ResolveBoundaryAxis(particle.Position.y, particle.Velocity.y, minimum.y, maximum.y, restitution);
        ResolveBoundaryAxis(particle.Position.z, particle.Velocity.z, minimum.z, maximum.z, restitution);
    }
}

void SPHSolver::AdvanceSubstep(
    std::vector<FluidParticle>& particles, float deltaTime, JobSystem* pJobSystem)
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Substep",
        Diagnostics::DiagnosticCategory::Physics);
    // Neighbor関係と圧力は位置に依存するため、各Substepで再計算します。
    const std::size_t cachedRowCount = ComputeDensity(particles, true);
    RecordSPHCounter("Physics.Fluid.SPH.NeighborCacheCompletedRowCount",
        static_cast<double>(cachedRowCount));
    ComputePressure(particles);
    ComputeForcesUsingCurrentGrid(particles, cachedRowCount, pJobSystem);
    RecordSPHCounter("Physics.Fluid.SPH.ForceNeighborCacheUsed",
        cachedRowCount > 0u ? 1.0 : 0.0);
    Integrate(particles, deltaTime);
    ResolveBoundary(particles);
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

    if (m_Settings.StableTimeStepEnabled == false)
    {
        AdvanceSubstep(particles, deltaTime, pJobSystem);
        m_LastSubstepCount = 1u;
        m_LastMinimumSubstepDeltaTime = deltaTime;
    }
    else
    {
        float remainingTime = deltaTime;
        float minimumSubstepDeltaTime = std::numeric_limits<float>::max();
        while (remainingTime > math::Epsilon
            && m_LastSubstepCount < m_Settings.MaximumSubsteps)
        {
            float substepDt = ComputeStableTimeStep(particles, remainingTime);
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
            AdvanceSubstep(particles, substepDt, pJobSystem);
            remainingTime -= substepDt;
            minimumSubstepDeltaTime = std::min(minimumSubstepDeltaTime, substepDt);
            ++m_LastSubstepCount;
        }
        if (m_LastSubstepCount > 0u)
        {
            m_LastMinimumSubstepDeltaTime = minimumSubstepDeltaTime;
        }
    }

    // Hot loop内ではProfilerへ触らず、Step完了後にParticleを1回だけ走査して状態を集約します。
    // これによりSPH本体のNeighbor loopへmutex/文字列処理を持ち込まず、計測汚染を抑えます。
    float maximumDensity = 0.0f;
    float maximumAbsolutePressure = 0.0f;
    float maximumSpeed = 0.0f;
    float maximumAcceleration = 0.0f;
    {
        Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.StepStatistics",
            Diagnostics::DiagnosticCategory::Physics);
        for (const FluidParticle& particle : particles)
        {
            maximumDensity = std::max(maximumDensity, particle.Density);
            maximumAbsolutePressure = std::max(maximumAbsolutePressure, std::abs(particle.Pressure));
            maximumSpeed = std::max(maximumSpeed, particle.Velocity.Length());

            const float mass = std::max(0.0f, particle.Mass);
            if (mass > math::Epsilon)
            {
                maximumAcceleration = std::max(
                    maximumAcceleration,
                    particle.Force.Length() / mass);
            }
        }
    }

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
    profiler.AddCounter("Physics.Fluid.SPH.MaximumDensity", static_cast<double>(maximumDensity));
    profiler.AddCounter("Physics.Fluid.SPH.MaximumAbsolutePressure",
        static_cast<double>(maximumAbsolutePressure));
    profiler.AddCounter("Physics.Fluid.SPH.MaximumSpeed", static_cast<double>(maximumSpeed));
    profiler.AddCounter("Physics.Fluid.SPH.MaximumAcceleration",
        static_cast<double>(maximumAcceleration));
}

} // namespace ph
} // namespace Raven
