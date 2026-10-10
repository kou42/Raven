#include "Raven/Editor/Panels/StatisticsRavenPanel.h"

#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIScrollView.h"
#include "Raven/UI/Widgets/UITable.h"
#include "Raven/UI/Widgets/UIWindow.h"

#include <cstdio>
#include <string>

namespace Raven
{
namespace
{
constexpr std::size_t RuntimeHeader = 0u;
constexpr std::size_t RuntimeFPS = 1u;
constexpr std::size_t RuntimeFrameTime = 2u;
constexpr std::size_t RuntimeWindow = 3u;
constexpr std::size_t RendererHeader = 4u;
constexpr std::size_t RendererDrawCalls = 5u;
constexpr std::size_t RendererIndexCount = 6u;
constexpr std::size_t RendererTriangleCount = 7u;
constexpr std::size_t PhysicsHeader = 8u;
constexpr std::size_t PhysicsStatus = 9u;
constexpr std::size_t PhysicsRigidBodies = 10u;
constexpr std::size_t PhysicsColliders = 11u;
constexpr std::size_t PhysicsBroadPhasePairs = 12u;
constexpr std::size_t PhysicsManifolds = 13u;
constexpr std::size_t PhysicsContactPoints = 14u;
constexpr std::size_t PhysicsWarmStarted = 15u;
constexpr std::size_t PhysicsVelocityIterations = 16u;
constexpr std::size_t PhysicsMaxPenetration = 17u;
constexpr std::size_t AstroHeader = 18u;
constexpr std::size_t AstroSolver = 19u;
constexpr std::size_t AstroBodies = 20u;
constexpr std::size_t AstroThresholds = 21u;
constexpr std::size_t AstroTheta = 22u;
constexpr std::size_t AstroTimingA = 23u;
constexpr std::size_t AstroTimingB = 24u;
constexpr std::size_t AstroWork = 25u;
constexpr std::size_t ProfilerHeader = 26u;
constexpr std::size_t ProfilerFrame = 27u;
constexpr std::size_t ProfilerCounts = 28u;
constexpr std::size_t SummaryLabelCount = 29u;

std::string FormatDouble(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}
} // namespace

bool StatisticsRavenPanel::Attach(UIContext& context, const Ref<UIFontAtlas>& font)
{
    if (m_Root != nullptr)
    {
        return false;
    }

    auto window = CreateScope<UIWindow>();
    window->SetTitle("Raven Debug / Statistics");
    window->SetPosition(math::Vec2(24.0f, 24.0f));
    window->SetSize(math::Vec2(780.0f, 720.0f));
    window->SetPreferredSize(math::Vec2(780.0f, 720.0f));

    auto title = CreateScope<UILabel>();
    title->SetFont(font);
    title->SetText("Raven Debug / Statistics");
    title->SetPosition(math::Vec2(12.0f, 3.0f));
    title->SetSize(math::Vec2(740.0f, 24.0f));
    title->SetHitTestVisible(false);
    window->AddChild(std::move(title));

    auto scroll = CreateScope<UIScrollView>();
    scroll->SetPosition(math::Vec2(8.0f, 32.0f));
    scroll->SetSize(math::Vec2(764.0f, 680.0f));
    scroll->SetPreferredSize(math::Vec2(764.0f, 680.0f));

    auto content = CreateScope<UIElement>();
    content->SetSize(math::Vec2(738.0f, 1370.0f));
    content->SetPreferredSize(math::Vec2(738.0f, 1370.0f));
    for (std::size_t index = 0u; index < SummaryLabelCount; ++index)
    {
        m_SummaryLabels.push_back(AddLabel(
            *content, 8.0f + static_cast<float>(index) * 25.0f, font));
    }

    auto profileTable = CreateScope<UITable>();
    profileTable->SetFont(font);
    profileTable->SetPosition(math::Vec2(8.0f, 752.0f));
    profileTable->SetSize(math::Vec2(712.0f, 270.0f));
    profileTable->AddColumn("Scope", 360.0f);
    profileTable->AddColumn("Total ms", 110.0f);
    profileTable->AddColumn("Max ms", 110.0f);
    profileTable->AddColumn("Calls", 90.0f);
    profileTable->SetDataSource(
        [this]() { return GetProfileRowCount(); },
        [this](std::size_t row, std::size_t column)
        {
            return GetProfileCellText(row, column);
        });
    m_ProfileTable = static_cast<UITable*>(content->AddChild(std::move(profileTable)));

    auto counterTable = CreateScope<UITable>();
    counterTable->SetFont(font);
    counterTable->SetPosition(math::Vec2(8.0f, 1040.0f));
    counterTable->SetSize(math::Vec2(712.0f, 300.0f));
    counterTable->AddColumn("Counter", 300.0f);
    counterTable->AddColumn("Total", 100.0f);
    counterTable->AddColumn("Average", 100.0f);
    counterTable->AddColumn("Max", 100.0f);
    counterTable->AddColumn("Samples", 90.0f);
    counterTable->SetDataSource(
        [this]() { return GetCounterRowCount(); },
        [this](std::size_t row, std::size_t column)
        {
            return GetCounterCellText(row, column);
        });
    m_CounterTable = static_cast<UITable*>(content->AddChild(std::move(counterTable)));

    scroll->SetContent(std::move(content));
    window->AddChild(std::move(scroll));
    m_Context = &context;
    m_Root = context.GetRootElement().AddChild(std::move(window));
    if (m_Root == nullptr)
    {
        m_Context = nullptr;
        m_SummaryLabels.clear();
        m_ProfileTable = nullptr;
        m_CounterTable = nullptr;
        return false;
    }

    UpdateSummaryLabels();
    return true;
}

void StatisticsRavenPanel::Detach()
{
    if (m_Context != nullptr && m_Root != nullptr)
    {
        m_Context->GetRootElement().RemoveChild(m_Root);
    }
    m_Context = nullptr;
    m_Root = nullptr;
    m_SummaryLabels.clear();
    m_ProfileTable = nullptr;
    m_CounterTable = nullptr;
    m_Snapshot = StatisticsSnapshot{};
}

void StatisticsRavenPanel::Update(const StatisticsSnapshot& snapshot)
{
    m_Snapshot = snapshot;
    UpdateSummaryLabels();
    if (m_ProfileTable != nullptr)
    {
        m_ProfileTable->NotifyDataSourceChanged();
    }
    if (m_CounterTable != nullptr)
    {
        m_CounterTable->NotifyDataSourceChanged();
    }
}

void StatisticsRavenPanel::SetVisible(bool visible)
{
    if (m_Root != nullptr)
    {
        m_Root->SetVisible(visible);
    }
}

bool StatisticsRavenPanel::IsAttached() const
{
    return m_Root != nullptr;
}

bool StatisticsRavenPanel::IsVisible() const
{
    return m_Root != nullptr && m_Root->IsVisible() == true;
}

std::size_t StatisticsRavenPanel::GetProfileRowCount() const
{
    return m_Snapshot.CPUProfiler.ProfileAggregates.size();
}

std::size_t StatisticsRavenPanel::GetCounterRowCount() const
{
    return m_Snapshot.CPUProfiler.CounterAggregates.size();
}

UILabel* StatisticsRavenPanel::AddLabel(
    UIElement& parent, float y, const Ref<UIFontAtlas>& font)
{
    auto label = CreateScope<UILabel>();
    label->SetFont(font);
    label->SetPosition(math::Vec2(8.0f, y));
    label->SetSize(math::Vec2(700.0f, 23.0f));
    label->SetHitTestVisible(false);
    return static_cast<UILabel*>(parent.AddChild(std::move(label)));
}

void StatisticsRavenPanel::UpdateSummaryLabels()
{
    if (m_SummaryLabels.size() != SummaryLabelCount)
    {
        return;
    }

    char buffer[256];
    m_SummaryLabels[RuntimeHeader]->SetText("Runtime");
    std::snprintf(buffer, sizeof(buffer), "FPS: %.1f", m_Snapshot.Runtime.FramesPerSecond);
    m_SummaryLabels[RuntimeFPS]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Frame Time: %.3f ms",
        m_Snapshot.Runtime.FrameTimeMilliseconds);
    m_SummaryLabels[RuntimeFrameTime]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Window: %u x %u",
        m_Snapshot.Runtime.WindowWidth, m_Snapshot.Runtime.WindowHeight);
    m_SummaryLabels[RuntimeWindow]->SetText(buffer);

    m_SummaryLabels[RendererHeader]->SetText("Renderer");
    std::snprintf(buffer, sizeof(buffer), "Draw Calls: %u", m_Snapshot.Renderer.DrawCalls);
    m_SummaryLabels[RendererDrawCalls]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Index Count: %u", m_Snapshot.Renderer.IndexCount);
    m_SummaryLabels[RendererIndexCount]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Triangles: %u", m_Snapshot.Renderer.TriangleCount);
    m_SummaryLabels[RendererTriangleCount]->SetText(buffer);

    const StatisticsPhysicsSnapshot& physics = m_Snapshot.Physics;
    m_SummaryLabels[PhysicsHeader]->SetText("Physics");
    m_SummaryLabels[PhysicsStatus]->SetText(
        physics.Available == true ? "Status: Active Scene" : "Status: No active scene");
    std::snprintf(buffer, sizeof(buffer), "Rigid Bodies: %u", physics.RigidBodyCount);
    m_SummaryLabels[PhysicsRigidBodies]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Colliders: %u", physics.ColliderCount);
    m_SummaryLabels[PhysicsColliders]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Broad Phase Pairs: %u", physics.BroadPhasePairCount);
    m_SummaryLabels[PhysicsBroadPhasePairs]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Contact Manifolds: %u", physics.ManifoldCount);
    m_SummaryLabels[PhysicsManifolds]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Contact Points: %u", physics.ContactPointCount);
    m_SummaryLabels[PhysicsContactPoints]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Warm Started Constraints: %u",
        physics.WarmStartedConstraintCount);
    m_SummaryLabels[PhysicsWarmStarted]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Velocity Iterations: %u", physics.VelocityIterations);
    m_SummaryLabels[PhysicsVelocityIterations]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Max Penetration: %.5f", physics.MaxPenetration);
    m_SummaryLabels[PhysicsMaxPenetration]->SetText(buffer);

    const StatisticsAstroSnapshot& astro = physics.Astro;
    m_SummaryLabels[AstroHeader]->SetText("Astro Gravity");
    std::snprintf(buffer, sizeof(buffer), "Mode: %s / Active Solver: %s",
        astro.SolverMode.c_str(), astro.ActiveSolver.c_str());
    m_SummaryLabels[AstroSolver]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Active Bodies: %llu",
        static_cast<unsigned long long>(astro.ActiveBodyCount));
    m_SummaryLabels[AstroBodies]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Switch Up / Down: %llu / %llu",
        static_cast<unsigned long long>(astro.BarnesHutBodyThreshold),
        static_cast<unsigned long long>(astro.DirectBodyThreshold));
    m_SummaryLabels[AstroThresholds]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Barnes-Hut Theta: %.3f", astro.BarnesHutTheta);
    m_SummaryLabels[AstroTheta]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "State Collection: %.3f ms / Gravity Solve: %.3f ms",
        astro.StateCollectionTimeMilliseconds, astro.GravitySolveTimeMilliseconds);
    m_SummaryLabels[AstroTimingA]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Octree Build: %.3f ms / Force Feedback: %.3f ms",
        astro.GravityTreeBuildTimeMilliseconds, astro.ForceFeedbackTimeMilliseconds);
    m_SummaryLabels[AstroTimingB]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Force Evaluations: %llu / Node Visits: %llu / Aggregate: %llu",
        static_cast<unsigned long long>(astro.GravityForceEvaluationCount),
        static_cast<unsigned long long>(astro.GravityVisitedNodeCount),
        static_cast<unsigned long long>(astro.GravityAcceptedAggregateNodeCount));
    m_SummaryLabels[AstroWork]->SetText(buffer);

    m_SummaryLabels[ProfilerHeader]->SetText("CPU Profiler");
    if (m_Snapshot.CPUProfiler.Enabled == false)
    {
        m_SummaryLabels[ProfilerFrame]->SetText("Status: Disabled");
        m_SummaryLabels[ProfilerCounts]->SetText("Scopes: 0 / Counters: 0");
        return;
    }
    std::snprintf(buffer, sizeof(buffer), "Profile Frame: %llu / CPU Frame: %.3f ms",
        static_cast<unsigned long long>(m_Snapshot.CPUProfiler.FrameIndex),
        m_Snapshot.CPUProfiler.FrameTimeMilliseconds);
    m_SummaryLabels[ProfilerFrame]->SetText(buffer);
    std::snprintf(buffer, sizeof(buffer), "Scopes: %llu / Counters: %u",
        static_cast<unsigned long long>(m_Snapshot.CPUProfiler.RawResults.size()),
        m_Snapshot.CPUProfiler.RecordedCounterCount);
    m_SummaryLabels[ProfilerCounts]->SetText(buffer);
}

std::string StatisticsRavenPanel::GetProfileCellText(
    std::size_t row, std::size_t column) const
{
    if (row >= m_Snapshot.CPUProfiler.ProfileAggregates.size())
    {
        return {};
    }
    const StatisticsProfileAggregate& aggregate =
        m_Snapshot.CPUProfiler.ProfileAggregates[row];
    switch (column)
    {
    case 0u: return aggregate.Name;
    case 1u: return FormatDouble(aggregate.TotalMilliseconds);
    case 2u: return FormatDouble(aggregate.MaxMilliseconds);
    case 3u: return std::to_string(aggregate.CallCount);
    default: return {};
    }
}

std::string StatisticsRavenPanel::GetCounterCellText(
    std::size_t row, std::size_t column) const
{
    if (row >= m_Snapshot.CPUProfiler.CounterAggregates.size())
    {
        return {};
    }
    const StatisticsCounterAggregate& aggregate =
        m_Snapshot.CPUProfiler.CounterAggregates[row];
    const double average = aggregate.SampleCount > 0u
        ? aggregate.Total / static_cast<double>(aggregate.SampleCount)
        : 0.0;
    switch (column)
    {
    case 0u: return aggregate.Name;
    case 1u: return FormatDouble(aggregate.Total);
    case 2u: return FormatDouble(average);
    case 3u: return FormatDouble(aggregate.Max);
    case 4u: return std::to_string(aggregate.SampleCount);
    default: return {};
    }
}

} // namespace Raven
