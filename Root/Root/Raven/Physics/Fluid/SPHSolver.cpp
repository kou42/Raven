#include "Raven/Physics/Fluid/SPHSolver.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "Raven/Core/CPUProfiler.h"
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
// 密集時は粒子数の二乗に近づくため、補助バッファを固定上限に抑えます。
constexpr std::size_t MaximumCachedNeighbors = 256u * 1024u;
constexpr std::size_t InitialNeighborCapacity = 1024u;

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

bool SPHSolver::ComputeDensity(std::vector<FluidParticle>& particles, bool shouldCacheNeighbors)
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Density", Diagnostics::DiagnosticCategory::Physics);
    m_SpatialHash.Build(particles);
    const float h = m_Settings.SmoothingRadius;
    const float hSq = h * h;
    if (shouldCacheNeighbors == true)
    {
        m_SpatialHash.PrepareNeighborBuckets(h);
    }
    const float nearZeroDistanceSq = math::Epsilon * math::Epsilon;
    bool hasCachedNeighbors = shouldCacheNeighbors && particles.size() <= MaximumCachedNeighbors;
    if (hasCachedNeighbors == true)
    {
        m_CachedNeighbors.clear();
        m_CachedNeighborOffsets.resize(particles.size() + 1);
    }

    // Spatial Hashが返したCell候補数と、厳密なsupport radius判定を通過した数を分離します。
    // Hot loopでは整数加算だけに留め、Profiler登録は処理末尾でまとめて行います。
    // Candidate/Accepted比を見ることで、CellSizeや検索範囲に無駄があるか判断できます。
    uint64_t candidateVisitCount = 0u;
    uint64_t acceptedNeighborCount = 0u;

    for (std::size_t i = 0u; i < particles.size(); ++i)
    {
        if (hasCachedNeighbors == true)
        {
            m_CachedNeighborOffsets[i] = m_CachedNeighbors.size();
        }
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
            if (hasCachedNeighbors == true && j != i && (rSq <= nearZeroDistanceSq) == false)
            {
                if (m_CachedNeighbors.size() == MaximumCachedNeighbors)
                {
                    // 部分的な近傍リストを使うと力が欠けるため、このSubstep全体をGrid探索へ戻します。
                    hasCachedNeighbors = false;
                    return;
                }
                if (m_CachedNeighbors.size() == m_CachedNeighbors.capacity())
                {
                    m_CachedNeighbors.reserve(std::min(MaximumCachedNeighbors,
                        std::max(InitialNeighborCapacity, m_CachedNeighbors.capacity() * 2)));
                }
                m_CachedNeighbors.push_back({j, rSq});
            }
        });
        particle.Density = density;
    }
    if (hasCachedNeighbors == true)
    {
        m_CachedNeighborOffsets.back() = m_CachedNeighbors.size();
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
    return hasCachedNeighbors;
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
    ComputeForcesUsingCurrentGrid(particles, false);
}

void SPHSolver::ComputeForcesUsingCurrentGrid(
    std::vector<FluidParticle>& particles, bool hasCachedNeighbors)
{
    Diagnostics::ProfiledTraceScope trace("Physics.Fluid.SPH.Force", Diagnostics::DiagnosticCategory::Physics);
    const float h = m_Settings.SmoothingRadius;
    const float hSq = h * h;
    const bool hasViscosity = m_Settings.Viscosity > 0.0f;

    // DensityとPressureが確定した時点で、複数の近傍から参照される除算を粒子ごとに済ませます。
    // 公開ComputeForcesも呼び出し時点の粒子データから毎回作り直します。
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

    // Force側はself、無効Mass/Density、support radius外を除外した後の数をAcceptedとします。
    // Density側との差を見ることで、密度計算では有効でも力計算には使われない候補量も追跡できます。
    uint64_t candidateVisitCount = 0u;
    uint64_t acceptedNeighborCount = 0u;

    for (std::size_t i = 0u; i < particles.size(); ++i)
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
            ++acceptedNeighborCount;

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

        if (hasCachedNeighbors == true)
        {
            const std::size_t begin = m_CachedNeighborOffsets[i];
            const std::size_t end = m_CachedNeighborOffsets[i + 1];
            candidateVisitCount += end - begin;
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
                ++candidateVisitCount;
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

    CPUProfiler& profiler = CPUProfiler::Get();
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

float SPHSolver::ComputeStableTimeStep(
    const std::vector<FluidParticle>& particles,
    float maximumDeltaTime) const
{
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
    Diagnostics::TraceScope trace("Physics.Fluid.SPH.Integrate", Diagnostics::DiagnosticCategory::Physics);
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
    Diagnostics::TraceScope trace("Physics.Fluid.SPH.ResolveBoundary", Diagnostics::DiagnosticCategory::Physics);
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

void SPHSolver::AdvanceSubstep(std::vector<FluidParticle>& particles, float deltaTime)
{
    // Neighbor関係と圧力は位置に依存するため、各Substepで再計算します。
    const bool hasCachedNeighbors = ComputeDensity(particles, true);
    ComputePressure(particles);
    ComputeForcesUsingCurrentGrid(particles, hasCachedNeighbors);
    Diagnostics::RecordCounter("Physics.Fluid.SPH.ForceNeighborCacheUsed",
        hasCachedNeighbors ? 1.0 : 0.0, Diagnostics::DiagnosticCategory::Physics);
    Integrate(particles, deltaTime);
    ResolveBoundary(particles);
}

void SPHSolver::Step(std::vector<FluidParticle>& particles, float deltaTime)
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
        AdvanceSubstep(particles, deltaTime);
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
            AdvanceSubstep(particles, substepDt);
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
