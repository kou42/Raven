#include "Raven/Editor/Panels/StatisticsRavenPanel.h"

#include "Raven/Editor/Panels/StatisticsActions.h"
#include "Raven/UI/Core/UIContext.h"
#include "Raven/UI/Widgets/UICheckbox.h"
#include "Raven/UI/Widgets/UIButton.h"
#include "Raven/UI/Widgets/UILabel.h"
#include "Raven/UI/Widgets/UIScrollView.h"
#include "Raven/UI/Widgets/UITable.h"
#include "Raven/UI/Widgets/UIWindow.h"

#include <algorithm>
#include <cstdio>
#include <chrono>
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
constexpr std::size_t UIHeader = 8u;
constexpr std::size_t UIBuildAndCommands = 9u;
constexpr std::size_t UIGeometry = 10u;
constexpr std::size_t UIResources = 11u;
constexpr std::size_t UIElements = 12u;
constexpr std::size_t PhysicsHeader = 13u;
constexpr std::size_t PhysicsStatus = 14u;
constexpr std::size_t PhysicsRigidBodies = 15u;
constexpr std::size_t PhysicsColliders = 16u;
constexpr std::size_t PhysicsBroadPhasePairs = 17u;
constexpr std::size_t PhysicsManifolds = 18u;
constexpr std::size_t PhysicsContactPoints = 19u;
constexpr std::size_t PhysicsWarmStarted = 20u;
constexpr std::size_t PhysicsVelocityIterations = 21u;
constexpr std::size_t PhysicsMaxPenetration = 22u;
constexpr std::size_t AstroHeader = 23u;
constexpr std::size_t AstroSolver = 24u;
constexpr std::size_t AstroBodies = 25u;
constexpr std::size_t AstroThresholds = 26u;
constexpr std::size_t AstroTheta = 27u;
constexpr std::size_t AstroTimingA = 28u;
constexpr std::size_t AstroTimingB = 29u;
constexpr std::size_t AstroWork = 30u;
constexpr std::size_t ProfilerHeader = 31u;
constexpr std::size_t ProfilerFrame = 32u;
constexpr std::size_t ProfilerCounts = 33u;
constexpr std::size_t SummaryLabelCount = 34u;

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
    // Dock Resize後もScroll範囲の基準を実際の本文Viewportへ一致させます。
    scroll->SetHorizontalAlignment(UIAlignment::Stretch);
    scroll->SetVerticalAlignment(UIAlignment::Stretch);
    UIThickness scrollMargin;
    scrollMargin.Left = 8.0f;
    scrollMargin.Right = 8.0f;
    scrollMargin.Top = 32.0f;
    scrollMargin.Bottom = 8.0f;
    scroll->SetMargin(scrollMargin);

    auto content = CreateScope<UIElement>();
    content->SetSize(math::Vec2(738.0f, 2650.0f));
    content->SetPreferredSize(math::Vec2(738.0f, 2650.0f));
    for (std::size_t index = 0u; index < SummaryLabelCount; ++index)
    {
        m_SummaryLabels.push_back(AddLabel(
            *content, 8.0f + static_cast<float>(index) * 25.0f, font));
    }

    auto profilerEnabled = CreateScope<UICheckbox>();
    profilerEnabled->SetPosition(math::Vec2(150.0f, 779.0f));
    profilerEnabled->SetSize(math::Vec2(170.0f, 28.0f));
    profilerEnabled->SetPreferredSize(math::Vec2(170.0f, 28.0f));
    profilerEnabled->SetCaption("Enabled", font);
    profilerEnabled->SetOnToggle(
        [this](bool enabled)
        {
            OnProfilerEnabledToggled(enabled);
        });
    m_ProfilerEnabledCheckbox = static_cast<UICheckbox*>(
        content->AddChild(std::move(profilerEnabled)));

    auto actionStatus = CreateScope<UILabel>();
    actionStatus->SetFont(font);
    actionStatus->SetPosition(math::Vec2(334.0f, 783.0f));
    actionStatus->SetSize(math::Vec2(380.0f, 23.0f));
    actionStatus->SetHitTestVisible(false);
    m_ActionStatusLabel = static_cast<UILabel*>(content->AddChild(std::move(actionStatus)));

    auto profileTable = CreateScope<UITable>();
    profileTable->SetFont(font);
    profileTable->SetPosition(math::Vec2(8.0f, 877.0f));
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
    counterTable->SetPosition(math::Vec2(8.0f, 1165.0f));
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

    auto rawProfileTable = CreateScope<UITable>();
    rawProfileTable->SetFont(font);
    rawProfileTable->SetPosition(math::Vec2(8.0f, 1483.0f));
    rawProfileTable->SetSize(math::Vec2(712.0f, 300.0f));
    rawProfileTable->AddColumn("Raw Scope", 560.0f);
    rawProfileTable->AddColumn("Time ms", 130.0f);
    rawProfileTable->SetDataSource(
        [this]() { return GetRawProfileRowCount(); },
        [this](std::size_t row, std::size_t column)
        {
            return GetRawProfileCellText(row, column);
        });
    m_RawProfileTable = static_cast<UITable*>(
        content->AddChild(std::move(rawProfileTable)));

    // 記録の所有者はPanelです。Dock移動や非表示では維持し、Detach時に破棄します。
    m_RecordingSummary = AddLabel(*content, 1800.0f, font);
    m_RecordingStatus = AddLabel(*content, 1860.0f, font);
    m_RecordingStatus->SetText("All rendered Windows / C++ new: profiling build only.");
    const auto addButton = [&](float x, const char* caption, UIButton::ClickHandler handler)
    {
        auto button = CreateScope<UIButton>();
        button->SetPosition(math::Vec2(x, 1828.0f));
        button->SetSize(math::Vec2(180.0f, 28.0f));
        AddLabel(*button, 2.0f, font)->SetText(caption);
        button->SetOnClick(std::move(handler));
        content->AddChild(std::move(button));
    };
    addButton(8.0f, "Start / Restart", [this]()
    {
        m_UIRecording.Start();
        m_RecordingStatus->SetText("Recording new session (all rendered Windows).");
    });
    addButton(200.0f, "Stop", [this]()
    {
        m_UIRecording.Stop();
        m_RecordingStatus->SetText("Stopped; results retained until restart.");
    });
    addButton(392.0f, "Save CSV", [this]()
    {
        // セッションごとに別名を提案し、既存の記録を上書きしません。
        const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const std::string path = "RavenUI-recording-" + std::to_string(stamp) + ".csv";
        std::string message;
        m_UIRecording.SaveCSV(path, message);
        m_RecordingStatus->SetText(message);
    });
    auto recordingTable = CreateScope<UITable>();
    recordingTable->SetFont(font);
    recordingTable->SetPosition(math::Vec2(8.0f, 1895.0f));
    recordingTable->SetSize(math::Vec2(712.0f, 350.0f));
    recordingTable->AddColumn("Recorded metric", 340.0f);
    recordingTable->AddColumn("Average", 180.0f);
    recordingTable->AddColumn("Max", 180.0f);
    recordingTable->SetDataSource(
        []() { return StatisticsUIRecording::MetricCount; },
        [this](std::size_t row, std::size_t column) -> std::string
        {
            if (row >= StatisticsUIRecording::MetricCount)
            {
                return {};
            }
            if (column == 0u)
            {
                return StatisticsUIRecording::GetMetricName(row);
            }
            if (m_UIRecording.GetTotal().GetMetricSampleCount(row) == 0u)
            {
                return "N/A";
            }
            return FormatDouble(column == 1u ? m_UIRecording.GetTotal().GetAverages()[row]
                : m_UIRecording.GetTotal().GetMaxima()[row]);
        });
    content->AddChild(std::move(recordingTable));

    m_UIWindowSummary = AddLabel(*content, 2265.0f, font);
    auto windowTable = CreateScope<UITable>();
    windowTable->SetFont(font);
    windowTable->SetPosition(math::Vec2(8.0f, 2300.0f));
    windowTable->SetSize(math::Vec2(712.0f, 330.0f));
    windowTable->AddColumn("Window ID", 110.0f);
    windowTable->AddColumn("Build ms", 100.0f);
    windowTable->AddColumn("Commands", 100.0f);
    windowTable->AddColumn("Vertices", 100.0f);
    windowTable->AddColumn("Indices", 100.0f);
    windowTable->AddColumn("C++ alloc / bytes", 190.0f);
    windowTable->SetDataSource(
        [this]() { return m_Snapshot.UIWindows.size(); },
        [this](std::size_t row, std::size_t column) -> std::string
        {
            if (row >= m_Snapshot.UIWindows.size())
            {
                return {};
            }
            const auto& window = m_Snapshot.UIWindows[row];
            const auto& metrics = window.Metrics;
            switch (column)
            {
            case 0u: return std::to_string(window.WindowID) + (window.MainWindow == true ? " Main" : "");
            case 1u: return FormatDouble(metrics.BuildMilliseconds);
            case 2u: return std::to_string(metrics.DrawCommandCount);
            case 3u: return std::to_string(metrics.VertexCount);
            case 4u: return std::to_string(metrics.IndexCount);
            case 5u: return metrics.CppAllocations.Available == true
                ? std::to_string(metrics.CppAllocations.Count) + " / " + std::to_string(metrics.CppAllocations.Bytes)
                : "N/A (profiling off)";
            default: return {};
            }
        });
    m_UIWindowTable = static_cast<UITable*>(content->AddChild(std::move(windowTable)));

    scroll->SetContent(std::move(content));
    window->AddChild(std::move(scroll));
    m_Context = &context;
    m_Root = context.GetRootElement().AddChild(std::move(window));
    if (m_Root == nullptr)
    {
        m_Context = nullptr;
        m_UIWindowTable = nullptr;
        m_UIWindowSummary = nullptr;
        m_RecordingSummary = nullptr;
        m_RecordingStatus = nullptr;
        m_SummaryLabels.clear();
        m_ProfilerEnabledCheckbox = nullptr;
        m_ActionStatusLabel = nullptr;
        m_ProfileTable = nullptr;
        m_CounterTable = nullptr;
        m_RawProfileTable = nullptr;
        return false;
    }

    UpdateSummaryLabels();
    return true;
}

void StatisticsRavenPanel::Detach()
{
    if (m_Root != nullptr && m_Root->GetParent() != nullptr)
    {
        m_Root->GetParent()->RemoveChild(m_Root);
    }
    m_Context = nullptr;
    m_Root = nullptr;
    m_SummaryLabels.clear();
    m_ProfilerEnabledCheckbox = nullptr;
    m_ActionStatusLabel = nullptr;
    m_ProfileTable = nullptr;
    m_CounterTable = nullptr;
    m_RawProfileTable = nullptr;
    m_Snapshot = StatisticsSnapshot{};
    m_UIWindowTable = nullptr;
    m_UIWindowSummary = nullptr;
    m_UIRecording = StatisticsUIRecordingSession{};
    m_RecordingSummary = nullptr;
    m_RecordingStatus = nullptr;
}

void StatisticsRavenPanel::Update(const StatisticsSnapshot& snapshot)
{
    m_Snapshot = snapshot;
    m_UIRecording.AddSample(snapshot);
    if (m_UIWindowTable != nullptr)
    {
        m_UIWindowTable->NotifyDataSourceChanged();
    }
    if (m_UIRecording.IsRecording() == true &&
        m_UIRecording.GetOmittedWindowSamples() > 0u && m_RecordingStatus != nullptr)
    {
        m_RecordingStatus->SetText("Window history limit reached; aggregate still includes all Windows.");
    }
    if (m_UIWindowSummary != nullptr)
    {
        m_UIWindowSummary->SetText("Rendered Windows: " + std::to_string(snapshot.UIWindows.size()) +
            " / Build sum: " + FormatDouble(snapshot.UIAllWindows.BuildMilliseconds) +
            " ms / C++ allocations: " + (snapshot.UIAllWindows.CppAllocations.Available == true
                ? std::to_string(snapshot.UIAllWindows.CppAllocations.Count) : "N/A (profiling off)"));
    }
    if (m_RecordingSummary != nullptr)
    {
        m_RecordingSummary->SetText(
            std::string(m_UIRecording.IsRecording() == true ? "Recording / Samples: " : "Stopped / Samples: ") +
            std::to_string(m_UIRecording.GetSampleCount()));
    }
    if (m_ProfilerEnabledCheckbox != nullptr)
    {
        m_ProfilerEnabledCheckbox->SetChecked(snapshot.CPUProfiler.Enabled);
    }
    UpdateSummaryLabels();
    if (m_ProfileTable != nullptr)
    {
        m_ProfileTable->NotifyDataSourceChanged();
    }
    if (m_CounterTable != nullptr)
    {
        m_CounterTable->NotifyDataSourceChanged();
    }
    if (m_RawProfileTable != nullptr)
    {
        m_RawProfileTable->NotifyDataSourceChanged();
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

std::size_t StatisticsRavenPanel::GetRawProfileRowCount() const
{
    return m_Snapshot.CPUProfiler.RawResults.size();
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

void StatisticsRavenPanel::OnProfilerEnabledToggled(bool enabled)
{
    const StatisticsActionResult result = SetCPUProfilerEnabledAction(enabled);
    if (m_ProfilerEnabledCheckbox != nullptr)
    {
        m_ProfilerEnabledCheckbox->SetChecked(result.Enabled);
    }
    if (m_ActionStatusLabel != nullptr)
    {
        m_ActionStatusLabel->SetText(result.Message);
    }
}

void StatisticsRavenPanel::UpdateSummaryLabels()
{
    if (m_SummaryLabels.size() != SummaryLabelCount)
    {
        return;
    }

    char buffer[256];
    m_SummaryLabels[RuntimeHeader]->SetText("Runtime");
    if (m_Snapshot.Runtime.FrameTimingAvailable == true)
    {
        std::snprintf(buffer, sizeof(buffer), "FPS: %.1f", m_Snapshot.Runtime.FramesPerSecond);
        m_SummaryLabels[RuntimeFPS]->SetText(buffer);
        std::snprintf(buffer, sizeof(buffer), "Frame Time: %.3f ms",
            m_Snapshot.Runtime.FrameTimeMilliseconds);
        m_SummaryLabels[RuntimeFrameTime]->SetText(buffer);
    }
    else
    {
        m_SummaryLabels[RuntimeFPS]->SetText("FPS: N/A (No frame timing)");
        m_SummaryLabels[RuntimeFrameTime]->SetText("Frame Time: N/A (No frame timing)");
    }
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

    m_SummaryLabels[UIHeader]->SetText("Raven UI (Previous Frame)");
    if (m_Snapshot.UI.Available == true)
    {
        std::snprintf(buffer, sizeof(buffer), "Build: %.3f ms / Draw Commands: %llu",
            m_Snapshot.UI.BuildMilliseconds,
            static_cast<unsigned long long>(m_Snapshot.UI.DrawCommandCount));
        m_SummaryLabels[UIBuildAndCommands]->SetText(buffer);
        std::snprintf(buffer, sizeof(buffer), "Vertices: %llu / Indices: %llu",
            static_cast<unsigned long long>(m_Snapshot.UI.VertexCount),
            static_cast<unsigned long long>(m_Snapshot.UI.IndexCount));
        m_SummaryLabels[UIGeometry]->SetText(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "Texture Switches: %llu / Draw Allocations: %llu (%.1f KiB)",
            static_cast<unsigned long long>(m_Snapshot.UI.TextureSwitchCount),
            static_cast<unsigned long long>(m_Snapshot.UI.FrameAllocationCount),
            static_cast<double>(m_Snapshot.UI.FrameAllocatedBytes) / 1024.0);
        m_SummaryLabels[UIResources]->SetText(buffer);
        std::snprintf(buffer, sizeof(buffer),
            "DrawList Retained: %.1f KiB / Elements: %llu (Peak %llu)",
            static_cast<double>(m_Snapshot.UI.RetainedAllocationBytes) / 1024.0,
            static_cast<unsigned long long>(m_Snapshot.UI.ElementCount),
            static_cast<unsigned long long>(m_Snapshot.UI.PeakElementCount));
        m_SummaryLabels[UIElements]->SetText(buffer);
    }
    else
    {
        m_SummaryLabels[UIBuildAndCommands]->SetText("Build: N/A (No completed frame)");
        m_SummaryLabels[UIGeometry]->SetText("Vertices / Indices: N/A");
        m_SummaryLabels[UIResources]->SetText("Texture Switches / Draw Allocations: N/A");
        m_SummaryLabels[UIElements]->SetText("DrawList Retained / Elements: N/A");
    }

    const StatisticsPhysicsSnapshot& physics = m_Snapshot.Physics;
    m_SummaryLabels[PhysicsHeader]->SetText("Physics");
    m_SummaryLabels[PhysicsStatus]->SetText(
        physics.Available == true ? "Status: Active Scene" : "Status: No active scene");
    if (physics.Available == false)
    {
        // Sceneなしの既定値0は実測値ではないため、Physics/Astro全項目を値なしとして表示します。
        for (std::size_t index = PhysicsRigidBodies; index <= AstroWork; ++index)
        {
            m_SummaryLabels[index]->SetText("N/A (No active scene)");
        }
    }
    else
    {
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
        std::snprintf(buffer, sizeof(buffer),
            "Force Evaluations: %llu / Node Visits: %llu / Aggregate: %llu",
            static_cast<unsigned long long>(astro.GravityForceEvaluationCount),
            static_cast<unsigned long long>(astro.GravityVisitedNodeCount),
            static_cast<unsigned long long>(astro.GravityAcceptedAggregateNodeCount));
        m_SummaryLabels[AstroWork]->SetText(buffer);
    }

    m_SummaryLabels[ProfilerHeader]->SetText("CPU Profiler");
    if (m_Snapshot.CPUProfiler.Enabled == false)
    {
        m_SummaryLabels[ProfilerFrame]->SetText("Status: Disabled");
        m_SummaryLabels[ProfilerCounts]->SetText("Scopes: N/A / Counters: N/A (Disabled)");
        return;
    }
    if (m_Snapshot.CPUProfiler.FrameAvailable == false)
    {
        m_SummaryLabels[ProfilerFrame]->SetText("Status: No completed frame");
        m_SummaryLabels[ProfilerCounts]->SetText("Scopes: N/A / Counters: N/A");
        return;
    }
    std::snprintf(buffer, sizeof(buffer), "Previous Frame: %llu / CPU Frame: %.3f ms",
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

std::string StatisticsRavenPanel::GetRawProfileCellText(
    std::size_t row, std::size_t column) const
{
    if (row >= m_Snapshot.CPUProfiler.RawResults.size())
    {
        return {};
    }

    const CPUProfileResult& result = m_Snapshot.CPUProfiler.RawResults[row];
    switch (column)
    {
    case 0u:
    {
        // 異常なDepth値で巨大文字列を作らないよう表示上のindentだけに上限を設けます。
        const std::size_t depth = std::min<std::size_t>(result.Depth, 32u);
        return std::string(depth * 2u, ' ') + result.Name;
    }
    case 1u: return FormatDouble(result.DurationMilliseconds);
    default: return {};
    }
}

} // namespace Raven
