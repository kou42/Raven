#include "Raven/Physics/Astro/AstroWorld.h"

#include <chrono>
#include <cstddef>
#include <cmath>

#include "Raven/Physics/Astro/CelestialBody.h"
#include "Raven/Scene/Components.h"
#include "Raven/Scene/Scene.h"

namespace Raven::ph
{
namespace
{
bool CanReceiveGravity(const RigidBodyComponent& rigidBody)
{
    return rigidBody.Type == BodyType::Dynamic && rigidBody.InverseMass > 0.0f;
}

void WakeRigidBody(RigidBodyComponent& rigidBody)
{
    rigidBody.IsSleeping = false;
    rigidBody.SleepTimer = 0.0f;
}
}

void AstroWorld::AccumulateGravityForces(Scene& scene, float fixedDeltaTime)
{
    static_cast<void>(fixedDeltaTime);

    m_Bodies.clear();
    m_Forces.clear();
    m_Statistics.Clear();

    using Clock = std::chrono::steady_clock;
    const auto collectionBegin = Clock::now();

    for (auto [entity, transform, rigidBody, celestialBody]
        : scene.View<TransformComponent, RigidBodyComponent, CelestialBodyComponent>())
    {
        if (celestialBody.GenerateGravity == false && celestialBody.ReceiveGravity == false)
        {
            continue;
        }

        AstroBodyState state{};
        state.Entity = entity.GetHandle();
        state.Position = AstroVector3(transform.Position);
        state.Velocity = AstroVector3(rigidBody.LinearVelocity);
        state.Mass = static_cast<double>(rigidBody.Mass);
        state.GenerateGravity = celestialBody.GenerateGravity;
        state.ReceiveGravity = celestialBody.ReceiveGravity;
        m_Bodies.push_back(state);
    }

    const auto collectionEnd = Clock::now();
    m_Statistics.ActiveBodyCount = static_cast<std::uint64_t>(m_Bodies.size());
    m_Statistics.StateCollectionTimeMs =
        std::chrono::duration<double, std::milli>(collectionEnd - collectionBegin).count();

    GravitySolver* gravitySolver = ResolveGravitySolver(m_Bodies.size());
    if (gravitySolver == nullptr)
    {
        return;
    }

    // 診断値は現在stepのsnapshotから毎回計算します。Multi-rateでForceを再利用しても、
    // Energy/Momentum診断まで古いsnapshotへ固定しないことで誤差を追跡できます。
    m_LastDiagnostics = OrbitalDiagnosticsCalculator::Compute(m_Bodies, m_Settings);

    if (IsNearFarMultiRateEnabled() == true)
    {
        std::vector<AstroVector3> nearForces;
        ComputeNearGravityForces(nearForces);

        const std::uint32_t farInterval = m_MultiRateSettings.FarGravityUpdateIntervalSteps;
        const bool farIntervalElapsed =
            farInterval <= 1u || m_StepsSinceGravitySolve >= (farInterval - 1u);
        const bool reuseCachedFarForces =
            farIntervalElapsed == false && CanReuseCachedGravityForces()
            && m_CachedFarGravityForces.size() == m_Bodies.size();

        if (reuseCachedFarForces == true)
        {
            m_Forces.resize(m_Bodies.size());
            for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
            {
                m_Forces[i] = nearForces[i] + m_CachedFarGravityForces[i];
            }
            ++m_StepsSinceGravitySolve;
            m_Statistics.CachedFarGravityForceUsed = true;
        }
        else
        {
            const auto solveBegin = Clock::now();
            gravitySolver->ComputeForces(m_Bodies, m_Settings, m_Forces, &m_Statistics);
            const auto solveEnd = Clock::now();

            if (gravitySolver == &m_BarnesHutGravitySolver)
            {
                m_Statistics.SolverKind = AstroGravitySolverKind::BarnesHut;
            }
            else if (gravitySolver == &m_DirectGravitySolver)
            {
                m_Statistics.SolverKind = AstroGravitySolverKind::Direct;
            }
            else
            {
                m_Statistics.SolverKind = AstroGravitySolverKind::Custom;
            }

            m_CachedFarGravityForces.resize(m_Bodies.size());
            for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
            {
                // Full - Near をFar成分として保存することで、既存Solver契約を変更せず
                // 時間方向のLODだけをAstroWorld境界へ追加します。
                m_CachedFarGravityForces[i] = m_Forces[i] - nearForces[i];
                m_Forces[i] = nearForces[i] + m_CachedFarGravityForces[i];
            }

            m_Statistics.GravitySolveTimeMs =
                std::chrono::duration<double, std::milli>(solveEnd - solveBegin).count();
            m_Statistics.GravitySolveExecuted = true;
            m_Statistics.FarGravitySolveExecuted = true;
            m_StepsSinceGravitySolve = 0u;
            CacheGravityForces();
        }
    }
    else
    {
        const std::uint32_t interval = m_MultiRateSettings.GravityUpdateIntervalSteps;
        const bool intervalElapsed =
            interval <= 1u || m_StepsSinceGravitySolve >= (interval - 1u);
        const bool reuseCachedForces =
            intervalElapsed == false && CanReuseCachedGravityForces();

        if (reuseCachedForces == true)
        {
            m_Forces = m_CachedGravityForces;
            ++m_StepsSinceGravitySolve;
            m_Statistics.CachedGravityForceUsed = true;
        }
        else
        {
            const auto solveBegin = Clock::now();
            gravitySolver->ComputeForces(m_Bodies, m_Settings, m_Forces, &m_Statistics);
            const auto solveEnd = Clock::now();

            // Custom Solverが受け取ったStatisticsを初期化しても、実際に選択した種別は
            // AstroWorld境界の診断値として必ず復元します。
            if (gravitySolver == &m_BarnesHutGravitySolver)
            {
                m_Statistics.SolverKind = AstroGravitySolverKind::BarnesHut;
            }
            else if (gravitySolver == &m_DirectGravitySolver)
            {
                m_Statistics.SolverKind = AstroGravitySolverKind::Direct;
            }
            else
            {
                m_Statistics.SolverKind = AstroGravitySolverKind::Custom;
            }
            m_Statistics.GravitySolveTimeMs =
                std::chrono::duration<double, std::milli>(solveEnd - solveBegin).count();
            m_Statistics.GravitySolveExecuted = true;
            m_StepsSinceGravitySolve = 0u;
            CacheGravityForces();
        }
    }
    if (m_Forces.size() != m_Bodies.size())
    {
        // Solver境界違反時に部分的なForceだけをSceneへ反映しないよう、step全体を破棄します。
        return;
    }

    if (m_MultiRateSettings.MeasureDirectReferenceError == true)
    {
        MeasureDirectReferenceError();
    }

    const auto feedbackBegin = Clock::now();
    for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
    {
        const AstroBodyState& state = m_Bodies[i];
        if (state.ReceiveGravity == false || scene.IsEntityAlive(state.Entity) == false)
        {
            continue;
        }

        RigidBodyComponent* rigidBody =
            scene.TryGetComponent<RigidBodyComponent>(state.Entity.m_Index);
        if (rigidBody == nullptr || CanReceiveGravity(*rigidBody) == false)
        {
            continue;
        }

        const AstroVector3& astroForce = m_Forces[i];
        const math::Vec3 force = astroForce.ToSceneVector();
        if (std::isfinite(force.x) == false
            || std::isfinite(force.y) == false
            || std::isfinite(force.z) == false)
        {
            // double側で有限でもfloat Scene境界への縮小変換でoverflowする可能性があります。
            // 非有限値を既存Rigid Force accumulatorへ流さないことを境界で保証します。
            continue;
        }
        rigidBody->Force += force;
        if (astroForce.LengthSq() > 0.0)
        {
            WakeRigidBody(*rigidBody);
        }
    }
    const auto feedbackEnd = Clock::now();
    m_Statistics.ForceFeedbackTimeMs =
        std::chrono::duration<double, std::milli>(feedbackEnd - feedbackBegin).count();
}

void AstroWorld::Clear()
{
    m_Bodies.clear();
    m_Forces.clear();
    m_LastDiagnostics = OrbitalDiagnostics{};
    m_Statistics.Clear();
    InvalidateGravityForceCache();
}

void AstroWorld::SetGravitySolver(GravitySolver* solver)
{
    InvalidateGravityForceCache();
    // 外部Solverは非所有です。呼び出し側はAstroWorldより長いLifetimeを保証する必要があります。
    // nullptrは従来どおり「Direct Solverへ戻す」と解釈し、自動選択には暗黙で戻しません。
    m_CustomGravitySolver = solver;
    if (solver != nullptr)
    {
        m_GravitySolver = solver;
        return;
    }

    m_SolverSelectionSettings.Mode = AstroGravitySolverMode::Direct;
    m_GravitySolver = &m_DirectGravitySolver;
}

void AstroWorld::SetGravitySolverSelectionSettings(
    const AstroGravitySolverSelectionSettings& settings)
{
    InvalidateGravityForceCache();
    m_SolverSelectionSettings = settings;
    if (std::isfinite(m_SolverSelectionSettings.BarnesHutTheta) == false
        || m_SolverSelectionSettings.BarnesHutTheta <= 0.0)
    {
        // 不正なthetaで全Forceがゼロになることを防ぎ、検証済みの標準値へ戻します。
        m_SolverSelectionSettings.BarnesHutTheta = 0.5;
    }
    if (m_SolverSelectionSettings.DirectBodyThreshold
        > m_SolverSelectionSettings.BarnesHutBodyThreshold)
    {
        // 下側閾値が上側を超えると同じbody数で選択が反転し得るため、上側へClampします。
        m_SolverSelectionSettings.DirectBodyThreshold =
            m_SolverSelectionSettings.BarnesHutBodyThreshold;
    }

    // 明示設定を適用した時点で、非所有Custom Solverのoverrideを解除します。
    m_CustomGravitySolver = nullptr;
    m_AutomaticUsingBarnesHut = false;
    if (m_SolverSelectionSettings.Mode == AstroGravitySolverMode::BarnesHut)
    {
        m_BarnesHutGravitySolver.SetTheta(m_SolverSelectionSettings.BarnesHutTheta);
        m_GravitySolver = &m_BarnesHutGravitySolver;
    }
    else
    {
        // Automaticは次の状態収集までbody数が未確定なので、Directを安全な初期状態にします。
        m_GravitySolver = &m_DirectGravitySolver;
    }
}

const AstroGravitySolverSelectionSettings& AstroWorld::GetGravitySolverSelectionSettings() const
{
    return m_SolverSelectionSettings;
}

GravitySolver* AstroWorld::ResolveGravitySolver(std::size_t bodyCount)
{
    if (m_CustomGravitySolver != nullptr)
    {
        m_Statistics.SolverKind = AstroGravitySolverKind::Custom;
        m_GravitySolver = m_CustomGravitySolver;
        return m_GravitySolver;
    }

    bool useBarnesHut = m_SolverSelectionSettings.Mode == AstroGravitySolverMode::BarnesHut;
    if (m_SolverSelectionSettings.Mode == AstroGravitySolverMode::Automatic)
    {
        if (m_AutomaticUsingBarnesHut == true)
        {
            // 一度Barnes-Hutへ切り替えた後は下側閾値未満まで維持し、
            // spawn/despawnで上側閾値付近を往復しても毎step反転しないようにします。
            useBarnesHut = bodyCount >= m_SolverSelectionSettings.DirectBodyThreshold;
        }
        else
        {
            useBarnesHut = bodyCount >= m_SolverSelectionSettings.BarnesHutBodyThreshold;
        }
        m_AutomaticUsingBarnesHut = useBarnesHut;
    }
    if (useBarnesHut == true)
    {
        m_BarnesHutGravitySolver.SetTheta(m_SolverSelectionSettings.BarnesHutTheta);
        m_Statistics.SolverKind = AstroGravitySolverKind::BarnesHut;
        m_GravitySolver = &m_BarnesHutGravitySolver;
        return m_GravitySolver;
    }

    m_Statistics.SolverKind = AstroGravitySolverKind::Direct;
    m_GravitySolver = &m_DirectGravitySolver;
    return m_GravitySolver;
}


void AstroWorld::SetGravitySolverSettings(const GravitySolverSettings& settings)
{
    m_Settings = settings;
    InvalidateGravityForceCache();
}

void AstroWorld::SetMultiRateSettings(const AstroMultiRateSettings& settings)
{
    m_MultiRateSettings = settings;
    if (m_MultiRateSettings.GravityUpdateIntervalSteps == 0u)
    {
        // 0 step間隔は意味を持たないため、従来互換の毎step更新へClampします。
        m_MultiRateSettings.GravityUpdateIntervalSteps = 1u;
    }
    if (m_MultiRateSettings.FarGravityUpdateIntervalSteps == 0u)
    {
        m_MultiRateSettings.FarGravityUpdateIntervalSteps = 1u;
    }
    if (std::isfinite(m_MultiRateSettings.NearGravityDistance) == false
        || m_MultiRateSettings.NearGravityDistance < 0.0)
    {
        m_MultiRateSettings.NearGravityDistance = 0.0;
    }
    InvalidateGravityForceCache();
}

bool AstroWorld::CanReuseCachedGravityForces() const
{
    if (m_CachedGravityForces.size() != m_Bodies.size()
        || m_CachedGravityBodies.size() != m_Bodies.size())
    {
        return false;
    }

    // ECSの列挙順が変わった場合に別Entityへ古いForceを適用しないよう、
    // IndexだけでなくGenerationを含むEntityHandleでsnapshot対応を検証します。
    for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
    {
        const AstroBodyState& cachedBody = m_CachedGravityBodies[i];
        const AstroBodyState& currentBody = m_Bodies[i];
        if (cachedBody.Entity != currentBody.Entity
            || cachedBody.Mass != currentBody.Mass
            || cachedBody.GenerateGravity != currentBody.GenerateGravity
            || cachedBody.ReceiveGravity != currentBody.ReceiveGravity)
        {
            // Position/Velocityは通常のMulti-rateでは意図的に比較しません。質量や参加flagの
            // 変更だけはForceの意味自体が変わるため即時solveを要求します。
            return false;
        }
    }

    if (IsNearFarMultiRateEnabled() == true)
    {
        const double nearDistanceSquared =
            m_MultiRateSettings.NearGravityDistance * m_MultiRateSettings.NearGravityDistance;
        for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
        {
            for (std::size_t j = i + 1u; j < m_Bodies.size(); ++j)
            {
                const bool cachedNear =
                    (m_CachedGravityBodies[j].Position - m_CachedGravityBodies[i].Position)
                        .LengthSq() <= nearDistanceSquared;
                const bool currentNear =
                    (m_Bodies[j].Position - m_Bodies[i].Position).LengthSq()
                        <= nearDistanceSquared;
                if (cachedNear != currentNear)
                {
                    // Far cacheに含まれていたpairがNearへ入る（または逆）stepで古い成分を
                    // 足し引きすると二重加算/欠落になるため、LOD境界横断時だけ即時更新します。
                    return false;
                }
            }
        }
    }
    return true;
}

bool AstroWorld::IsNearFarMultiRateEnabled() const
{
    return m_MultiRateSettings.NearGravityDistance > 0.0;
}

void AstroWorld::ComputeNearGravityForces(std::vector<AstroVector3>& outForces)
{
    outForces.assign(m_Bodies.size(), AstroVector3{});
    const double nearDistanceSquared =
        m_MultiRateSettings.NearGravityDistance * m_MultiRateSettings.NearGravityDistance;

    for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
    {
        for (std::size_t j = i + 1u; j < m_Bodies.size(); ++j)
        {
            const AstroVector3 delta = m_Bodies[j].Position - m_Bodies[i].Position;
            const double distanceSquared = delta.LengthSq();
            if (distanceSquared > nearDistanceSquared)
            {
                continue;
            }

            // Near領域だけをDirectで評価します。Far側のBarnes-Hut空間近似とは別カウンタにし、
            // Phase 8の時間LODコストを独立して観測できるようにします。
            ++m_Statistics.NearGravityPairEvaluationCount;
            if (m_Bodies[i].GenerateGravity == true && m_Bodies[j].ReceiveGravity == true)
            {
                std::vector<AstroBodyState> pair{ m_Bodies[j], m_Bodies[i] };
                pair[0].ReceiveGravity = true;
                pair[0].GenerateGravity = false;
                pair[1].ReceiveGravity = false;
                pair[1].GenerateGravity = true;
                std::vector<AstroVector3> pairForces;
                m_DirectGravitySolver.ComputeForces(pair, m_Settings, pairForces);
                if (pairForces.size() == 2u)
                {
                    outForces[j] += pairForces[0];
                }
            }
            if (m_Bodies[j].GenerateGravity == true && m_Bodies[i].ReceiveGravity == true)
            {
                std::vector<AstroBodyState> pair{ m_Bodies[i], m_Bodies[j] };
                pair[0].ReceiveGravity = true;
                pair[0].GenerateGravity = false;
                pair[1].ReceiveGravity = false;
                pair[1].GenerateGravity = true;
                std::vector<AstroVector3> pairForces;
                m_DirectGravitySolver.ComputeForces(pair, m_Settings, pairForces);
                if (pairForces.size() == 2u)
                {
                    outForces[i] += pairForces[0];
                }
            }
        }
    }
}

void AstroWorld::MeasureDirectReferenceError()
{
    std::vector<AstroVector3> referenceForces;
    // Reference計算にはstatisticsを渡しません。検証用Direct評価のO(N^2)コストを
    // 実際に選択されたRuntime Solverのwork counterへ混ぜないためです。
    m_DirectGravitySolver.ComputeForces(m_Bodies, m_Settings, referenceForces);
    if (referenceForces.size() != m_Forces.size())
    {
        return;
    }

    double relativeErrorSum = 0.0;
    std::uint64_t measuredBodyCount = 0u;
    for (std::size_t i = 0u; i < m_Bodies.size(); ++i)
    {
        if (m_Bodies[i].ReceiveGravity == false)
        {
            continue;
        }

        const double absoluteForceError = (m_Forces[i] - referenceForces[i]).Length();
        const double referenceMagnitude = referenceForces[i].Length();
        double relativeError = 0.0;
        if (referenceMagnitude > 1.0e-12)
        {
            relativeError = absoluteForceError / referenceMagnitude;
        }
        else if (absoluteForceError > 1.0e-12)
        {
            // Referenceが実質ゼロなのに近似側だけForceを持つ場合は相対誤差を有限値へ
            // 正規化できないため、絶対誤差をそのまま異常度として扱います。
            relativeError = absoluteForceError;
        }

        m_Statistics.MaximumGravityForceRelativeError =
            std::max(m_Statistics.MaximumGravityForceRelativeError, relativeError);
        relativeErrorSum += relativeError;
        ++measuredBodyCount;

        if (m_Bodies[i].Mass > 0.0 && std::isfinite(m_Bodies[i].Mass) == true)
        {
            const double accelerationError = absoluteForceError / m_Bodies[i].Mass;
            m_Statistics.MaximumGravityAccelerationError =
                std::max(m_Statistics.MaximumGravityAccelerationError, accelerationError);
        }
    }

    if (measuredBodyCount > 0u)
    {
        m_Statistics.MeanGravityForceRelativeError =
            relativeErrorSum / static_cast<double>(measuredBodyCount);
    }
    m_Statistics.DirectReferenceErrorMeasured = true;
}

void AstroWorld::CacheGravityForces()
{
    m_CachedGravityForces = m_Forces;
    m_CachedGravityBodies = m_Bodies;
}

void AstroWorld::InvalidateGravityForceCache()
{
    m_CachedGravityBodies.clear();
    m_CachedGravityForces.clear();
    m_CachedFarGravityForces.clear();
    m_StepsSinceGravitySolve = 0u;
}

} // namespace Raven::ph
