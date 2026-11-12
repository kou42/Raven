#include "Raven/Scene/Tests/SceneViewSelfTests.h"

#include "Raven/Scene/Scene.h"
#include "Raven/Core/CPUProfiler.h"
#include "Raven/Core/Memory/AllocationMeasurement.h"
#include "Raven/Animation/Animator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Raven::tests
{
namespace
{
struct ViewA { uint64_t Value = 0; };
struct ViewB { uint64_t Value = 0; };
struct ViewC { uint64_t Value = 0; };
struct Missing {};
struct Unrelated {};
struct FailingConstruction
{
    explicit FailingConstruction(bool fail)
    {
        if (fail == true)
        {
            throw std::runtime_error("Component construction failed.");
        }
    }
};
using Clock = std::chrono::steady_clock;
volatile uint64_t MeasurementSink = 0;

void Require(bool value, const char* message)
{
    if (value == false)
    {
        throw std::runtime_error(message);
    }
}

double Microseconds(Clock::time_point first, Clock::time_point last)
{
    return std::chrono::duration<double, std::micro>(last - first).count();
}

struct ScanResult
{
    uint64_t Sum = 0;
    std::size_t Count = 0;
    std::size_t Candidates = 0;
};

ScanResult Scan(Scene& scene, bool single, ComponentViewStatistics* statistics = nullptr)
{
    ScanResult result;
    if (single == true)
    {
        auto view = scene.View<ViewA>(statistics);
        result.Candidates = view.CandidateCount();
        for (auto [entity, a] : view)
        {
            result.Sum += a.Value;
            ++result.Count;
        }
    }
    else
    {
        auto view = scene.View<ViewA, ViewB, ViewC>(statistics);
        result.Candidates = view.CandidateCount();
        for (auto [entity, a, b, c] : view)
        {
            result.Sum += a.Value + b.Value + c.Value;
            ++result.Count;
        }
    }
    return result;
}

void CheckScan(const ScanResult& actual, uint64_t sum, std::size_t count)
{
    Require(actual.Sum == sum && actual.Count == count, "View scan differs from fixture reference.");
}

#ifndef NDEBUG
void RequireInvalidated(const std::function<void()>& operation)
{
    bool rejected = false;
    try
    {
        operation();
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    Require(rejected, "A structurally invalidated View must be rejected in Debug.");
}

void TestInvalidation()
{
    Scene scene;
    Entity first = scene.CreateEntity("First");
    Entity second = scene.CreateEntity("Second");
    first.AddComponent<ViewA>();
    first.AddComponent<ViewB>();
    second.AddComponent<ViewA>();
    second.AddComponent<ViewB>();
    auto view = scene.View<ViewA, ViewB>();
    auto iterator = view.begin();
    second.RemoveComponent<ViewB>(); // 非候補Storageの削除も検出する必要があります。
    RequireInvalidated([&] { static_cast<void>(*iterator); });
    RequireInvalidated([&] { ++iterator; });
    RequireInvalidated([&] { static_cast<void>(view.begin()); });
    auto beforeAdd = scene.View<ViewA>();
    Entity third = scene.CreateEntity("Third");
    third.AddComponent<ViewA>();
    RequireInvalidated([&] { static_cast<void>(beforeAdd.CandidateCount()); });
    auto beforeFlush = scene.View<ViewA>();
    scene.QueueDestroyEntity(first);
    Require(beforeFlush.Size() == 3, "Destruction reservation must not invalidate a View.");
    scene.FlushDestroyedEntities();
    RequireInvalidated([&] { static_cast<void>(beforeFlush.Empty()); });
    auto beforeShutdown = scene.View<ViewA>();
    auto shutdownIterator = beforeShutdown.begin();
    scene.OnDestroy();
    RequireInvalidated([&] { static_cast<void>(*shutdownIterator); });
    RequireInvalidated([&] { static_cast<void>(beforeShutdown.end()); });
    Require(scene.View<ViewA>().Empty() == true, "A fresh View after shutdown must be empty.");

    // 候補Storage自体のswap-removeと、空Viewが保持するScene寿命Versionも確認します。
    Scene removal;
    Entity target = removal.CreateEntity("Target");
    target.AddComponent<ViewA>();
    auto beforeDestroy = removal.View<ViewA>();
    auto destroyIterator = beforeDestroy.begin();
    removal.DestroyEntity(target);
    RequireInvalidated([&] { static_cast<void>(*destroyIterator); });
    RequireInvalidated([&] { static_cast<void>(beforeDestroy.Size()); });
    auto emptyBeforeShutdown = removal.View<Missing>();
    removal.OnDestroy();
    RequireInvalidated([&] { static_cast<void>(emptyBeforeShutdown.Empty()); });

    Scene failed;
    Entity existing = failed.CreateEntity("Existing");
    Entity attempted = failed.CreateEntity("Attempted");
    existing.AddComponent<FailingConstruction>(false);
    auto beforeFailure = failed.View<FailingConstruction>();
    bool failedConstruction = false;
    try
    {
        attempted.AddComponent<FailingConstruction>(true);
    }
    catch (const std::runtime_error&)
    {
        failedConstruction = true;
    }
    Require(failedConstruction, "Fixture must fail during Component construction.");
    // 失敗でもvectorの再確保が起き得るため、旧Viewは再利用せず取得し直します。
    RequireInvalidated([&] { static_cast<void>(beforeFailure.begin()); });
    Require(failed.View<FailingConstruction>().Size() == 1, "Failed construction must not publish membership.");
    failed.OnDestroy();

    ComponentStorage<ViewA> storage;
    storage.Emplace(1);
    const auto initial = storage.GetStructuralVersion();
    storage.Get(1).Value = 10;
    Require(storage.GetStructuralVersion() == initial, "A value edit must preserve the structural version.");
    storage.Reserve(128);
    Require(storage.GetStructuralVersion() != initial, "Reallocation must invalidate Component references.");
    const auto reserved = storage.GetStructuralVersion();
    storage.Reserve(128);
    storage.ReserveEntities(1024);
    Require(storage.GetStructuralVersion() == reserved, "No-op Reserve and sparse growth must preserve dense references.");
    storage.Clear();
    Require(storage.GetStructuralVersion() != reserved, "Clear must invalidate references.");
}
#endif

void TestStructuralContract()
{
    Scene scene;
    Entity first = scene.CreateEntity("First");
    Entity second = scene.CreateEntity("Second");
    first.AddComponent<ViewA>(ViewA{10});
    second.AddComponent<ViewA>(ViewA{20});
    auto view = scene.View<ViewA>();
    auto iterator = view.begin();
    bool duplicateRejected = false;
    try
    {
        first.AddComponent<ViewA>(ViewA{99});
    }
    catch (const std::runtime_error&)
    {
        duplicateRejected = true;
    }
    Require(duplicateRejected, "Duplicate addition must be rejected before mutation.");
    Require(first.RemoveComponent<ViewB>() == false, "Missing removal must be a no-op.");
    scene.FlushDestroyedEntities();
    Require(view.Size() == 2 && std::get<1>(*iterator).Value == 10,
        "Rejected duplicate, missing removal and empty Flush must preserve the View.");

    // Queueは値編集と併用でき、Flushだけが構造変更となります。
    scene.QueueDestroyEntity(first);
    scene.QueueDestroyEntity(first);
    for (auto [entity, value] : view)
    {
        static_cast<void>(entity);
        ++value.Value;
    }
    Require(view.Size() == 2, "Queued destruction must not modify membership during traversal.");
    scene.FlushDestroyedEntities();
    // DebugだけでなくReleaseも旧View/Iteratorを触らず、Entityから参照を取り直します。
    auto reacquired = scene.View<ViewA>();
    Require(reacquired.Size() == 1 && std::get<0>(*reacquired.begin()) == second
        && std::get<1>(*reacquired.begin()).Value == 21, "Reacquisition must resolve the moved last row.");
    Entity reused = scene.CreateEntity("Reused");
    reused.AddComponent<ViewA>(ViewA{30});
    Require(scene.View<ViewA>().Size() == 2, "Reacquired View must see the reused Entity.");
    Require(scene.IsEntityAlive(first) == false, "Structural changes must not revive stale Entity handles.");
    scene.OnDestroy();

    // 所有Componentの参照は再配置後に再取得します。削除/Clearで所有資源が解放されることも確認します。
    ComponentStorage<std::shared_ptr<int>> owned;
    auto firstResource = std::make_shared<int>(10);
    auto lastResource = std::make_shared<int>(20);
    const std::weak_ptr<int> firstWeak = firstResource;
    const std::weak_ptr<int> lastWeak = lastResource;
    owned.Emplace(1, std::move(firstResource));
    owned.Emplace(2, std::move(lastResource));
    owned.Reserve(128);
    auto* stableReference = &owned.Get(2);
    owned.Reserve(128);
    owned.ReserveEntities(1024);
    Require(&owned.Get(2) == stableReference, "No-op Reserve and sparse growth must preserve dense references.");
    Require(owned.Remove(1) && firstWeak.expired(), "Swap-remove must release the removed resource.");
    Require(*owned.Get(2) == 20 && lastWeak.expired() == false, "Moved resource must remain associated with its Entity.");
    owned.Clear();
    Require(lastWeak.expired() && owned.Empty(), "Clear must release remaining Component resources.");
}

// Physics RegistryとLayerは非所有でログを参照します。ログ/ParticipantをSceneより長生きさせます。
struct PhaseLog { std::vector<int> Events; };
class PhaseScene final : public Scene
{
public:
    PhaseLog* Log = nullptr;
    Entity Probe;
    bool Paused = false;
protected:
    void OnUpdateGame(float dt) override;
    bool ShouldUpdateSimulation() const override;
};
void PhaseScene::OnUpdateGame(float)
{
    Log->Events.push_back(1);
    Probe.GetComponent<TransformComponent>().Position.x = -1;
}
bool PhaseScene::ShouldUpdateSimulation() const
{
    return Paused == false;
}
class PhaseParticipant final : public ph::SoftBodySimulationParticipant
{
public:
    PhaseLog* Log = nullptr;
    Entity Probe;
    void SimulateSoftBody(float dt) override;
    void SynchronizeSoftBodyOutput() override;
};
void PhaseParticipant::SimulateSoftBody(float dt)
{
    Require(dt > 0 && Probe.GetComponent<TransformComponent>().Position.x == 11,
        "Physics must see Animation output after Game Logic.");
    Log->Events.push_back(2);
}
void PhaseParticipant::SynchronizeSoftBodyOutput()
{
    Log->Events.push_back(3);
}
class PhaseLayer final : public Layer
{
public:
    PhaseLayer(PhaseScene& scene, PhaseLog& log);
    void OnUpdate(float dt) override;
    Entity Target;
private:
    PhaseScene& m_Scene;
    PhaseLog& m_Log;
};
PhaseLayer::PhaseLayer(PhaseScene& scene, PhaseLog& log) : m_Scene(scene), m_Log(log) {}
void PhaseLayer::OnUpdate(float)
{
    Require(m_Scene.Probe.GetComponent<TransformComponent>().Position.x == 11,
        "Layer must see evaluated Animation output.");
    m_Log.Events.push_back(4);
    m_Scene.QueueDestroyEntity(Target);
}

void TestSystemPhases()
{
    PhaseLog log;
    PhaseParticipant participant;
    PhaseScene scene;
    scene.Log = &log;
    scene.Probe = scene.CreateEntity("AnimationProbe");
    auto clip = std::make_shared<AnimationClip>(1.0f);
    clip->GetTransformTrack().PositionKeys = {{0, math::Vec3(11, 0, 0)}};
    auto animator = std::make_shared<Animator>();
    animator->Play(clip);
    scene.Probe.AddComponent<AnimatorComponent>().Instance = animator;
    participant.Log = &log;
    participant.Probe = scene.Probe;
    Require(scene.GetPhysicsSimulationWorld().GetSoftBodyWorld().RegisterSimulationParticipant(participant),
        "Phase participant registration failed.");
    auto layer = std::make_unique<PhaseLayer>(scene, log);
    auto* layerPointer = layer.get();
    scene.PushLayer(std::move(layer));
    auto& profiler = CPUProfiler::Get();
    const bool wasEnabled = profiler.IsEnabled();
    profiler.SetEnabled(true);
    profiler.BeginFrame();
    layerPointer->Target = scene.CreateEntity("ZeroStepTarget");
    scene.OnUpdate(0);
    Require(log.Events == std::vector<int>{1, 4}, "Zero-step frame must not simulate or synchronize Physics.");
    Require(scene.IsEntityAlive(layerPointer->Target) == false, "Layer destruction must flush after the layer.");
    profiler.BeginFrame();
    log.Events.clear();
    layerPointer->Target = scene.CreateEntity("CatchUpTarget");
    scene.OnUpdate(2.0f / 60.0f + 0.00001f);
    Require(log.Events == std::vector<int>{1, 2, 2, 3, 4}, "Catch-up must synchronize output once before layers.");
    profiler.BeginFrame();
    bool fixedCount = false;
    bool outputScope = false;
    for (const auto& counter : profiler.GetLastFrame().Counters)
    {
        if (counter.Name == "Physics.FixedStep.Count")
        {
            fixedCount = counter.Value == 2;
        }
    }
    for (const auto& result : profiler.GetLastFrame().Results)
    {
        if (result.Name == "Physics.OutputSynchronization")
        {
            outputScope = true;
        }
    }
    Require(fixedCount && outputScope, "Profiler must agree with observed phase execution.");
    scene.Paused = true;
    log.Events.clear();
    Entity pausedTarget = scene.CreateEntity("PausedTarget");
    scene.QueueDestroyEntity(pausedTarget);
    scene.OnUpdate(1.0f / 60.0f);
    Require(log.Events == std::vector<int>{1} && scene.IsEntityAlive(pausedTarget) == false,
        "Pause must run Game Logic and destruction without Physics or layers.");
    scene.OnDestroy();
    profiler.SetEnabled(wasEnabled);
}

ScanResult ScanWorkload(Scene& scene, int shape, ComponentViewStatistics* statistics = nullptr)
{
    if (shape != 6)
    {
        return Scan(scene, shape == 0, statistics);
    }
    ScanResult result;
    auto view = scene.View<TransformComponent, RigidBodyComponent, ColliderComponent>(statistics);
    result.Candidates = view.CandidateCount();
    for (auto [entity, transform, body, collider] : view)
    {
        result.Sum += entity.GetIndex() + static_cast<uint64_t>(transform.Scale.x + body.Mass + collider.Radius * 2);
        ++result.Count;
    }
    return result;
}

// MeasurementごとにSceneを所有し、View/Iterator/Component参照を構造変更の前後へ持ち越しません。
void MeasureCase(std::size_t count, int shape)
{
    const bool single = shape == 0;
    const char* names[] = {"single", "dense", "last-smallest", "sparse-join", "disjoint", "missing", "rigid-join"};
    AllocationMeasurement setupMemory;
    auto setupStart = Clock::now();
    std::unique_ptr<Scene> scene;
    std::vector<Entity> entities;
    uint64_t expectedSum = 0;
    std::size_t expectedCount = 0;
    {
        AllocationMeasurementScope memory(setupMemory);
        scene = std::make_unique<Scene>();
        entities.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            Entity entity = scene->CreateEntity("ViewMeasurement");
            entities.push_back(entity);
            if (shape == 6)
            {
                if (i % 4 == 0)
                {
                    entity.AddComponent<RigidBodyComponent>();
                    entity.AddComponent<ColliderComponent>();
                    expectedSum += i + 4;
                    ++expectedCount;
                }
                continue;
            }
            const bool a = shape != 3 && shape != 4 ? true : i % 2 == 0;
            const bool b = shape == 3 ? i % 3 == 0 : (shape == 4 ? i % 2 == 1 : true);
            const bool c = shape == 2 || shape == 4 ? i % 4 == 0 : (shape == 3 ? i % 5 == 0 : true);
            if (a == true)
            {
                entity.AddComponent<ViewA>(ViewA{static_cast<uint64_t>(i + 1)});
            }
            if (single == false && b == true)
            {
                entity.AddComponent<ViewB>(ViewB{static_cast<uint64_t>(2 * (i + 1))});
            }
            if (single == false && shape != 5 && c == true)
            {
                entity.AddComponent<ViewC>(ViewC{static_cast<uint64_t>(3 * (i + 1))});
            }
            if (a == true && (single == true || (b == true && c == true && shape != 5)))
            {
                expectedSum += static_cast<uint64_t>((single == true ? 1 : 6) * (i + 1));
                ++expectedCount;
            }
        }
    }
    const double setupUs = Microseconds(setupStart, Clock::now());
    CheckScan(ScanWorkload(*scene, shape), expectedSum, expectedCount); // warm-up / correctness
    constexpr int Samples = 5;
    constexpr int Repetitions = 8;
    std::array<double, Samples> durations{};
    for (double& duration : durations)
    {
        uint64_t sum = 0;
        const auto first = Clock::now();
        for (int repeat = 0; repeat < Repetitions; ++repeat)
        {
            sum += ScanWorkload(*scene, shape).Sum;
        }
        duration = Microseconds(first, Clock::now()) / Repetitions;
        Require(sum == expectedSum * Repetitions, "Timed View checksum differs.");
        MeasurementSink = sum;
    }
    std::sort(durations.begin(), durations.end());

    // 同じ反復数・sample数で統計とProfilerの負荷を分離します。単発計測との差を
    // オーバーヘッドと誤認しないよう、Counterの検証/Frame公開は計時区間外へ置きます。
    std::array<std::array<double, Samples>, 3> instrumented{};
    auto& profiler = CPUProfiler::Get();
    const bool wasEnabled = profiler.IsEnabled();
    for (int sample = 0; sample < Samples; ++sample)
    {
        // 常に同じmodeを先に測らず、cache/温度/CPU動作変動の偏りを軽減します。
        for (int offset = 0; offset < 3; ++offset)
        {
            const int mode = (sample + offset) % 3;
            profiler.SetEnabled(mode == 1);
            profiler.BeginFrame();
            ComponentViewStatistics counted;
            uint64_t sum = 0;
            const auto first = Clock::now();
            for (int repeat = 0; repeat < Repetitions; ++repeat)
            {
                if (mode == 0)
                {
                    sum += ScanWorkload(*scene, shape, &counted).Sum;
                }
                else
                {
                    CPUProfileScope scope("SceneView.Query");
                    const auto scan = ScanWorkload(*scene, shape);
                    sum += scan.Sum;
                    profiler.AddCounter("SceneView.Matches", static_cast<double>(scan.Count));
                }
            }
            instrumented[mode][sample] = Microseconds(first, Clock::now()) / Repetitions;
            Require(sum == expectedSum * Repetitions, "Instrumented checksum differs.");
            if (mode == 0)
            {
                Require(counted.Matches == expectedCount * Repetitions, "Repeated match counter differs.");
            }
            if (mode == 1)
            {
                profiler.BeginFrame();
                const auto& frame = profiler.GetLastFrame();
                double matches = 0;
                for (const auto& counter : frame.Counters)
                {
                    Require(counter.Name == "SceneView.Matches", "Unexpected profiler counter.");
                    matches += counter.Value;
                }
                Require(frame.Results.size() == Repetitions && frame.Counters.size() == Repetitions
                    && matches == static_cast<double>(expectedCount * Repetitions), "Published profiler data differs.");
            }
            MeasurementSink = sum;
        }
    }
    profiler.SetEnabled(wasEnabled);
    for (auto& times : instrumented)
    {
        std::sort(times.begin(), times.end());
    }

    // Counter/heapの計測は時間計測と別pass。Counterを有効にした走査時間も別に報告します。
    ComponentViewStatistics statistics;
    AllocationMeasurement scanMemory;
    const auto measuredStart = Clock::now();
    ScanResult measured;
    {
        AllocationMeasurementScope memory(scanMemory);
        measured = ScanWorkload(*scene, shape, &statistics);
    }
    const double measuredUs = Microseconds(measuredStart, Clock::now());
    CheckScan(measured, expectedSum, expectedCount);
    Require(statistics.Candidates == measured.Candidates && statistics.Matches == measured.Count,
        "View candidate/match diagnostics differ from actual traversal.");
    Require(statistics.ComponentReads == measured.Count * (single == true ? 1 : 3),
        "View component read diagnostics differ.");

    AllocationMeasurement destroyMemory;
    const auto destroyStart = Clock::now();
    {
        AllocationMeasurementScope memory(destroyMemory);
        for (Entity entity : entities)
        {
            scene->QueueDestroyEntity(entity);
        }
        scene->FlushDestroyedEntities();
    }
    const double destroyUs = Microseconds(destroyStart, Clock::now());
    Require(ScanWorkload(*scene, shape).Count == 0, "View sees destroyed entities.");
    const auto releaseStart = Clock::now();
    scene.reset();
    const double releaseUs = Microseconds(releaseStart, Clock::now());
    std::cout << "SceneView case=" << names[shape] << " entities=" << count
        << " candidates=" << measured.Candidates << " matches=" << measured.Count
        << " median_us=" << durations[Samples / 2] << " min_us=" << durations.front() << " max_us=" << durations.back()
        << " counted_us=" << instrumented[0][Samples / 2]
        << " profiler_on_us=" << instrumented[1][Samples / 2]
        << " profiler_off_us=" << instrumented[2][Samples / 2]
        << " diagnostic_pass_us=" << measuredUs << " lookups=" << statistics.StorageLookups
        << " filter_checks=" << statistics.FilterChecks << " read_checks=" << statistics.ReadChecks
        << " alive_checks=" << statistics.AliveChecks << " checksum=" << measured.Sum
        << " structural_checks=" << statistics.StructuralChecks
        << " setup_us=" << setupUs << " destroy_us=" << destroyUs << " release_us=" << releaseUs
        << " allocation_available=" << scanMemory.Available << " scan_allocs=" << scanMemory.Count
        << " setup_allocs=" << setupMemory.Count << " setup_allocated_bytes=" << setupMemory.Bytes
        << " destroy_allocs=" << destroyMemory.Count << '\n';
}
}

void RunSceneViewSelfTests()
{
    TestStructuralContract();
    TestSystemPhases();
#ifndef NDEBUG
    TestInvalidation();
#endif
    Scene scene;
    Require(scene.View<ViewA, ViewB>().Empty() == true, "Missing storage must produce an empty View.");
    std::vector<Entity> entities;
    for (uint64_t i = 0; i < 12; ++i)
    {
        Entity entity = scene.CreateEntity("ViewTest");
        entities.push_back(entity);
        entity.AddComponent<ViewA>(ViewA{i + 1});
        if (i % 2 == 0)
        {
            entity.AddComponent<ViewB>(ViewB{2 * (i + 1)});
        }
        if (i % 3 == 0)
        {
            entity.AddComponent<ViewC>(ViewC{3 * (i + 1)});
        }
    }
    ComponentViewStatistics statistics;
    const auto result = Scan(scene, false, &statistics);
    CheckScan(result, 48, 2);
    Require(result.Candidates == 4 && statistics.Candidates == 4 && statistics.Matches == 2,
        "Smallest storage and diagnostics must agree.");
    Require(statistics.StorageLookups == 3 && statistics.FilterChecks == 8
        && statistics.ReadChecks == 4 && statistics.AliveChecks == 4 && statistics.ComponentReads == 6,
        "View diagnostics must count only actual lookups/checks/reads, excluding the base storage.");
    ComponentViewStatistics singleStatistics;
    Scan(scene, true, &singleStatistics);
    Require(singleStatistics.StorageLookups == 1 && singleStatistics.FilterChecks == 0
        && singleStatistics.ReadChecks == 0 && singleStatistics.ComponentReads == 12,
        "Single-component iteration must not repeat membership checks.");
    Require(scene.View<ViewA, ViewB, ViewC>().Size() == 2, "Size must count matches rather than candidates.");
    uint64_t reverseSum = 0;
    const auto reversed = scene.View<ViewC, ViewB, ViewA>();
    for (auto [entity, c, b, a] : reversed)
    {
        reverseSum += a.Value + b.Value + c.Value;
    }
    Require(reverseSum == result.Sum, "Component order must preserve the join result.");
    for (auto [entity, first, second] : scene.View<ViewA, ViewA>())
    {
        Require(&first == &second, "Repeated component types must alias the same value.");
    }
    auto stable = scene.View<ViewA>();
    auto independent = scene.View<ViewA>().begin(); // Iteratorは一時View自体を参照しません。
    Require(std::get<1>(*independent).Value == 1, "Iterator must not depend on a temporary View lifetime.");
    ComponentStorage<ViewA> foreign;
    bool foreignRejected = false;
    try
    {
        ComponentView<ViewA> invalid(scene, &foreign);
    }
    catch (const std::invalid_argument&)
    {
        foreignRejected = true;
    }
    Require(foreignRejected, "A View must reject a base storage outside its Scene component storages.");
    entities[1].AddComponent<Unrelated>();
    // 多種類のStorage追加でSceneのmapがrehashする場合も、既存pointeeと値参照は所有Scene内で生存します。
    std::apply([&](auto... value)
    {
        (entities[1].AddComponent<decltype(value)>(), ...);
    }, std::tuple<char, signed char, unsigned char, wchar_t, char16_t, char32_t, short, unsigned short,
        int, unsigned int, long, unsigned long, long long, unsigned long long, float, double, long double>{});
    entities[1].GetComponent<ViewA>().Value = 100;
    Require(stable.Size() == 12, "Unrelated storage and value edits must preserve a View.");
    Require(scene.View<ViewA, Missing>().Empty() == true, "Missing component is not a match.");
    entities[0].AddComponent<Missing>();
    Require(scene.View<ViewA, Missing>().Size() == 1, "Reacquired View must see a newly created storage.");
    Require(entities[0].RemoveComponent<ViewB>() == true, "Component removal failed.");
    Require(scene.View<ViewA, ViewB, ViewC>().Size() == 1, "Reacquired View must see swap-and-pop.");
    for (auto [entity, a] : scene.View<ViewA>())
    {
        ++a.Value;
        scene.QueueDestroyEntity(entity);
    }
    scene.FlushDestroyedEntities();
    Require(scene.View<ViewA>().Empty() == true, "Queue/flush must destroy every visited entity.");
    const EntityHandle old = entities.back().GetHandle();
    Entity reused = scene.CreateEntity("Reused");
    reused.AddComponent<ViewA>(ViewA{7});
    Require(reused.GetIndex() == old.m_Index && scene.IsEntityAlive(old) == false && scene.View<ViewA>().Size() == 1,
        "Reused entity slots must not revive old handles.");
    scene.OnDestroy();
}

void RunSceneViewMeasurements()
{
    RunSceneViewSelfTests();
#ifdef NDEBUG
    const char* configuration = "Release";
#else
    const char* configuration = "Debug";
#endif
    std::cout << "SceneView configuration=" << configuration
        << " view_bytes=" << sizeof(ComponentView<ViewA, ViewB, ViewC>)
        << " iterator_bytes=" << sizeof(ComponentView<ViewA, ViewB, ViewC>::Iterator)
        << " storage_bytes=" << sizeof(ComponentStorage<ViewA>) << '\n';
    for (std::size_t count : {4096u, 16384u})
    {
        for (int shape = 0; shape < 7; ++shape)
        {
            MeasureCase(count, shape);
        }
    }
    std::cout << "Scene View measurements passed.\n";
}
}
