#include "Raven/Scene/Tests/SceneRuntimeComparisonTests.h"

#include "Raven/Scene/Scene.h"
#include "Raven/Core/Memory/AllocationMeasurement.h"
#include "Raven/Renderer/RHI/RHISceneDrawItemBuilder.h"
#include "Raven/Renderer/Renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Raven::tests
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr std::size_t ChunkCapacity = 128;
constexpr float TimeStep = 1.0f / 64.0f;
volatile double ComparisonSink = 0.0;

void Require(bool condition, const char* message)
{
    if (condition == false)
    {
        throw std::runtime_error(message);
    }
}

double Elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

struct Velocity { float X = 1.0f; };
struct RenderPayload
{
    std::shared_ptr<const int> Asset;
    float Tint = 1.0f;
};

// 比較専用の2種類の固定型SoAです。RuntimeのRegistry/Handle allocatorは実装せず、
// SceneのHandleを非所有で借ります。Sceneはこの試作より長く生存する必要があります。
// 全型を16KiBへ詰める汎用レイアウトではなく、同じ128行で型ごとの末尾空きを計測します。
struct MotionChunk
{
    std::array<EntityHandle, ChunkCapacity> Handles{};
    std::array<TransformComponent, ChunkCapacity> Transforms{};
    std::array<Velocity, ChunkCapacity> Velocities{};
    std::size_t Count = 0;
};
struct RenderChunk : MotionChunk
{
    std::array<RenderPayload, ChunkCapacity> Renders{};
};
static_assert(sizeof(RenderChunk) <= 16 * 1024);

struct Location
{
    EntityGeneration Generation = 0;
    std::size_t Chunk = 0;
    std::size_t Row = 0;
    bool Render = false;
    bool Valid = false;
};

class ChunkMirror final
{
public:
    explicit ChunkMirror(Scene& owner);
    bool Insert(Entity entity);
    bool Erase(EntityHandle handle);
    bool Migrate(Entity entity);
    bool Contains(EntityHandle handle) const;
    void SyncInput();
    void Step();
    void WriteBack();
    double SnapshotChecksum() const;
    std::size_t ReservedBytes() const;
    std::size_t Count() const;

private:
    MotionChunk& At(const Location& location);
    const MotionChunk& At(const Location& location) const;
    Location Allocate(bool render, EntityHandle handle);
    Scene& m_Owner;
    std::size_t m_FirstMotionFree = 0;
    std::size_t m_FirstRenderFree = 0;
    std::vector<Location> m_Locations;
    std::vector<std::unique_ptr<MotionChunk>> m_Motion;
    std::vector<std::unique_ptr<RenderChunk>> m_Render;
};

ChunkMirror::ChunkMirror(Scene& owner) : m_Owner(owner) {}

MotionChunk& ChunkMirror::At(const Location& location)
{
    return location.Render ? *m_Render[location.Chunk] : *m_Motion[location.Chunk];
}

const MotionChunk& ChunkMirror::At(const Location& location) const
{
    return location.Render ? *m_Render[location.Chunk] : *m_Motion[location.Chunk];
}

Location ChunkMirror::Allocate(bool render, EntityHandle handle)
{
    // 空き行は同じ型の任意Chunkから再利用します。構成変更後の空Chunkも保持し、
    // 定常時の確保回避と、その代償となるhigh-water memoryの両方を測ります。
    std::size_t& firstFree = render ? m_FirstRenderFree : m_FirstMotionFree;
    std::size_t index = firstFree;
    const std::size_t count = render ? m_Render.size() : m_Motion.size();
    for (; index < count; ++index)
    {
        const MotionChunk& chunk = render ? *m_Render[index] : *m_Motion[index];
        if (chunk.Count < ChunkCapacity)
        {
            break;
        }
    }
    if (index == count)
    {
        if (render == true)
        {
            m_Render.push_back(std::make_unique<RenderChunk>());
        }
        else
        {
            m_Motion.push_back(std::make_unique<MotionChunk>());
        }
    }
    MotionChunk& chunk = render ? *m_Render[index] : *m_Motion[index];
    Location result{handle.m_Generation, index, chunk.Count, render, true};
    chunk.Handles[chunk.Count++] = handle;
    firstFree = chunk.Count == ChunkCapacity ? index + 1 : index;
    return result;
}

bool ChunkMirror::Contains(EntityHandle handle) const
{
    return handle.m_Index < m_Locations.size() && m_Locations[handle.m_Index].Valid == true &&
        m_Locations[handle.m_Index].Generation == handle.m_Generation;
}

bool ChunkMirror::Insert(Entity entity)
{
    if (m_Owner.IsEntityAlive(entity) == false ||
        entity.HasComponent<Velocity>() == false || Contains(entity.GetHandle()) == true)
    {
        return false;
    }
    const EntityHandle handle = entity.GetHandle();
    // 未通知の旧Generationを上書きしません。破棄通知漏れは呼び出し側の契約違反です。
    if (handle.m_Index < m_Locations.size() && m_Locations[handle.m_Index].Valid == true)
    {
        return false;
    }
    m_Locations.resize(std::max(m_Locations.size(), static_cast<std::size_t>(handle.m_Index) + 1));
    const bool render = entity.HasComponent<RenderPayload>();
    Location location = Allocate(render, handle);
    MotionChunk& chunk = At(location);
    chunk.Transforms[location.Row] = entity.GetComponent<TransformComponent>();
    chunk.Velocities[location.Row] = entity.GetComponent<Velocity>();
    if (render == true)
    {
        m_Render[location.Chunk]->Renders[location.Row] = entity.GetComponent<RenderPayload>();
    }
    m_Locations[handle.m_Index] = location;
    return true;
}

bool ChunkMirror::Erase(EntityHandle handle)
{
    // Scene破棄通知を処理する時点ではEntityが既に死んでいてもよいので、
    // Sceneの生存判定ではなく保存したGenerationを照合します。
    if (Contains(handle) == false)
    {
        return false;
    }
    const Location location = m_Locations[handle.m_Index];
    MotionChunk& chunk = At(location);
    const std::size_t last = --chunk.Count;
    std::size_t& firstFree = location.Render ? m_FirstRenderFree : m_FirstMotionFree;
    firstFree = std::min(firstFree, location.Chunk);
    if (location.Row != last)
    {
        chunk.Handles[location.Row] = chunk.Handles[last];
        chunk.Transforms[location.Row] = chunk.Transforms[last];
        chunk.Velocities[location.Row] = chunk.Velocities[last];
        m_Locations[chunk.Handles[location.Row].m_Index].Row = location.Row;
    }
    if (location.Render == true)
    {
        auto& renders = m_Render[location.Chunk]->Renders;
        if (location.Row != last)
        {
            renders[location.Row] = std::move(renders[last]);
        }
        // Count外にも非trivial値を残さず、参照先Assetを破棄通知で解放します。
        renders[last] = {};
    }
    chunk.Handles[last] = InvalidEntityHandle;
    m_Locations[handle.m_Index].Valid = false;
    return true;
}

bool ChunkMirror::Migrate(Entity entity)
{
    if (m_Owner.IsEntityAlive(entity) == false || Contains(entity.GetHandle()) == false)
    {
        return false;
    }
    const EntityHandle handle = entity.GetHandle();
    const Location old = m_Locations[handle.m_Index];
    const bool render = entity.HasComponent<RenderPayload>();
    if (old.Render == render)
    {
        return true;
    }
    // 確保が失敗しても元の行は生存させます。共通成分を移してからswap-removeし、
    // 移動元Chunkの行補正後に、新しいLocationを公開します。
    const Location next = Allocate(render, handle);
    At(next).Transforms[next.Row] = At(old).Transforms[old.Row];
    At(next).Velocities[next.Row] = At(old).Velocities[old.Row];
    if (render == true)
    {
        m_Render[next.Chunk]->Renders[next.Row] = entity.GetComponent<RenderPayload>();
    }
    Erase(handle);
    m_Locations[handle.m_Index] = next;
    return true;
}

void ChunkMirror::SyncInput()
{
    // 測定は親なしWorld-space互換Transformのみ。Physics Dynamic姿勢やLocalTransformを
    // このコピーで上書きするRuntime APIにはしません。構造変更通知は先に完了させます。
    for (auto [entity, transform, velocity] : m_Owner.View<TransformComponent, Velocity>())
    {
        Require(Contains(entity.GetHandle()), "Missing structural notification.");
        const Location& location = m_Locations[entity.GetIndex()];
        At(location).Transforms[location.Row] = transform;
        At(location).Velocities[location.Row] = velocity;
    }
    for (auto [entity, render] : m_Owner.View<RenderPayload>())
    {
        Require(Contains(entity.GetHandle()), "Missing render row.");
        const Location& location = m_Locations[entity.GetIndex()];
        Require(location.Render, "Missing migration notification.");
        m_Render[location.Chunk]->Renders[location.Row] = render;
    }
}

void ChunkMirror::Step()
{
    for (const auto& chunk : m_Motion)
    {
        for (std::size_t row = 0; row < chunk->Count; ++row)
        {
            chunk->Transforms[row].Position.x += chunk->Velocities[row].X * TimeStep;
        }
    }
    for (const auto& chunk : m_Render)
    {
        for (std::size_t row = 0; row < chunk->Count; ++row)
        {
            chunk->Transforms[row].Position.x += chunk->Velocities[row].X * TimeStep;
        }
    }
}

void ChunkMirror::WriteBack()
{
    for (std::size_t index = 1; index < m_Locations.size(); ++index)
    {
        const Location& location = m_Locations[index];
        if (location.Valid == false)
        {
            continue;
        }
        const EntityHandle handle{static_cast<EntityIndex>(index), location.Generation};
        Require(m_Owner.IsEntityAlive(handle), "Dead row must be removed at the barrier.");
        auto* transform = m_Owner.TryGetComponent<TransformComponent>(handle.m_Index);
        Require(transform != nullptr, "Missing writeback target.");
        *transform = At(location).Transforms[location.Row];
    }
}

double ChunkMirror::SnapshotChecksum() const
{
    double sum = 0.0;
    uint64_t handles = 0;
    for (const auto& chunk : m_Render)
    {
        for (std::size_t row = 0; row < chunk->Count; ++row)
        {
            // Handle全体とAssetを消費し、最適化によるループ消失を防ぎます。
            const auto& render = chunk->Renders[row];
            sum += chunk->Transforms[row].Position.x * render.Tint + *render.Asset;
            handles += chunk->Handles[row].Value();
        }
    }
    return sum + static_cast<double>(handles);
}

std::size_t ChunkMirror::ReservedBytes() const
{
    return sizeof(*this) + m_Locations.capacity() * sizeof(Location) +
        m_Motion.capacity() * sizeof(std::unique_ptr<MotionChunk>) +
        m_Render.capacity() * sizeof(std::unique_ptr<RenderChunk>) +
        m_Motion.size() * sizeof(MotionChunk) + m_Render.size() * sizeof(RenderChunk);
}

std::size_t ChunkMirror::Count() const
{
    std::size_t count = 0;
    for (const auto& chunk : m_Motion) { count += chunk->Count; }
    for (const auto& chunk : m_Render) { count += chunk->Count; }
    return count;
}

void SceneStep(Scene& scene)
{
    for (auto [entity, transform, velocity] : scene.View<TransformComponent, Velocity>())
    {
        transform.Position.x += velocity.X * TimeStep;
    }
}

double SceneSnapshotChecksum(Scene& scene)
{
    double sum = 0.0;
    uint64_t handles = 0;
    for (auto [entity, transform, render] : scene.View<TransformComponent, RenderPayload>())
    {
        sum += transform.Position.x * render.Tint + *render.Asset;
        handles += entity.GetHandle().Value();
    }
    return sum + static_cast<double>(handles);
}

std::vector<Entity> Populate(Scene& scene, std::size_t count, const std::shared_ptr<const int>& asset)
{
    std::vector<Entity> entities;
    entities.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        Entity entity = scene.CreateEntity("SG5");
        entity.GetComponent<TransformComponent>().Position.x = static_cast<float>(index % 32);
        entity.AddComponent<Velocity>().X = static_cast<float>(index % 4 + 1);
        if (index % 2 == 0)
        {
            entity.AddComponent<RenderPayload>().Asset = asset;
        }
        entities.push_back(entity);
    }
    return entities;
}

void Toggle(Entity entity, const std::shared_ptr<const int>& asset)
{
    if (entity.HasComponent<RenderPayload>() == true)
    {
        entity.RemoveComponent<RenderPayload>();
    }
    else
    {
        entity.AddComponent<RenderPayload>().Asset = asset;
    }
}

void TestMirror()
{
    Scene scene;
    Scene foreign;
    auto asset = std::make_shared<const int>(7);
    std::weak_ptr<const int> weak = asset;
    auto entities = Populate(scene, ChunkCapacity * 4 + 7, asset);
    ChunkMirror mirror(scene);
    for (Entity entity : entities) { Require(mirror.Insert(entity), "Insert failed."); }
    Entity wrong = foreign.CreateEntity("Foreign");
    wrong.AddComponent<Velocity>();
    Require(mirror.Insert(wrong) == false && mirror.Migrate(wrong) == false, "Foreign Scene accepted.");
    Require(mirror.Insert(entities.front()) == false, "Duplicate insertion accepted.");
    for (int round = 0; round < 4; ++round)
    {
        for (std::size_t index = 0; index < entities.size(); index += 3)
        {
            Toggle(entities[index], asset);
            Require(mirror.Migrate(entities[index]), "Migration failed.");
        }
        mirror.SyncInput();
        mirror.Step();
        SceneStep(scene);
        Require(mirror.SnapshotChecksum() == SceneSnapshotChecksum(scene), "Chunk query differs from Sparse Set.");
        mirror.WriteBack();
        for (std::size_t index = 0; index < entities.size(); ++index)
        {
            const float expected = static_cast<float>(index % 32) +
                static_cast<float>(index % 4 + 1) * TimeStep * static_cast<float>(round + 1);
            Require(entities[index].GetComponent<TransformComponent>().Position.x == expected,
                "Writeback differs after swap-remove, including non-render rows.");
        }
        Require(mirror.Count() == entities.size(), "Migration lost rows.");
    }
    const EntityHandle old = entities.front().GetHandle();
    scene.DestroyEntity(entities.front());
    Require(mirror.Erase(old), "Post-destruction notification rejected.");
    Entity reused = scene.CreateEntity("Reused");
    reused.AddComponent<Velocity>();
    Require(reused.GetIndex() == old.m_Index && reused.GetHandle() != old, "Fixture must reuse generation.");
    Require(mirror.Insert(reused), "Reused entity insertion failed.");
    Require(mirror.Erase(old) == false && mirror.Contains(reused.GetHandle()), "Stale notification erased new generation.");
    for (Entity entity : entities)
    {
        mirror.Erase(entity.GetHandle());
        scene.DestroyEntity(entity);
    }
    mirror.Erase(reused.GetHandle());
    scene.DestroyEntity(reused);
    asset.reset();
    Require(weak.expired(), "Vacated Chunk row retained nontrivial Asset.");
}

// GPUを作らず、SnapshotがResourceの所有参照を保持する契約だけを確認します。
class SnapshotBuffer final : public RHIBuffer
{
public:
    bool TrySetData(const void*, std::size_t, std::size_t) override;
    bool TryResize(std::size_t, const void*) override;
    const RHIBufferSpecification& GetSpecification() const override;
private:
    RHIBufferSpecification m_Specification{};
};
bool SnapshotBuffer::TrySetData(const void*, std::size_t, std::size_t) { return false; }
bool SnapshotBuffer::TryResize(std::size_t, const void*) { return false; }
const RHIBufferSpecification& SnapshotBuffer::GetSpecification() const { return m_Specification; }

void TestSnapshot()
{
    const math::Mat4 identity = math::Mat4::Identity();
    std::vector<RHISceneMesh> meshes(2);
    auto buffer = std::make_shared<SnapshotBuffer>();
    std::weak_ptr<SnapshotBuffer> weak = buffer;
    meshes[0].VertexBuffer = buffer;
    meshes[0].IndexBuffer = buffer;
    meshes[0].EntityID = 42;
    meshes[0].IndexCount = 3;
    meshes[0].Material.Tint = {0.25f, 0.5f, 0.75f, 1.0f};
    meshes[0].Model = RHISceneDrawItemBuilder::ToColumnMajor(math::Mat4::Translation({2.0f, 4.0f, -6.0f}));
    meshes[0].LocalCenter = {0.0f, 0.0f, 1.0f};
    std::vector<RHISceneDrawItem> items;
    RHISceneDrawItemBuilder::BuildInto(identity, identity, meshes,
        RHISceneDrawItemBuilder::VulkanClipCorrection(), items);
    Require(items.size() == 2 && items[0].EntityID == 42 && items[0].IndexCount == 3,
        "Snapshot metadata changed.");
    Require(items[0].ClipTransform[12] == 2.0f && items[0].ClipTransform[13] == -4.0f &&
        items[0].ClipTransform[14] == -2.5f && items[0].ViewDepth == -5.0f,
        "Column-major / Vulkan clip / local center convention changed.");
    meshes[0].Model[12] = 100.0f;
    meshes[0].Material.Tint[0] = 1.0f;
    Require(items[0].ClipTransform[12] == 2.0f && items[0].Material.Tint[0] == 0.25f,
        "Snapshot retained mutable source values.");
    // このCPU入口ではScene Queueを開始していません。失敗時に呼び出し側の
    // 既存出力と、そのResource参照が保持される公開API契約を確認します。
    Require(Renderer::BuildRHISceneDrawItems(nullptr, identity, items) == false &&
        items.size() == 2 && items[0].EntityID == 42 && items[0].VertexBuffer == buffer,
        "Failed extraction changed caller output.");
    meshes.clear();
    buffer.reset();
    Require(weak.expired() == false, "Snapshot did not retain GPU resource.");
    const std::size_t capacity = items.capacity();
    RHISceneDrawItemBuilder::BuildInto(identity, identity, meshes, identity, items);
    Require(items.empty() && weak.expired() && items.capacity() == capacity,
        "Empty extraction must release resources while retaining allocation.");
    meshes.resize(1);
    meshes[0].EntityID = 9;
    meshes[0].Model[14] = -3.0f;
    RHISceneDrawItemBuilder::BuildInto(identity, identity, meshes, identity, items);
    Require(items.size() == 1 && items[0].EntityID == 9 && items[0].ViewDepth == -3.0f,
        "Reused output retained previous frame data.");
    const auto compatibility = RHISceneDrawItemBuilder::Build(identity, identity, meshes, identity);
    Require(compatibility[0].ClipTransform == items[0].ClipTransform,
        "Compatibility overload differs from reusable extraction.");
}

double Median(std::array<double, 5> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

void MeasureSnapshot(std::size_t count)
{
    const math::Mat4 identity = math::Mat4::Identity();
    std::vector<RHISceneMesh> meshes(count);
    auto buffer = std::make_shared<SnapshotBuffer>();
    for (std::size_t i = 0; i < count; ++i)
    {
        meshes[i].EntityID = static_cast<uint32_t>(i + 1);
        meshes[i].VertexBuffer = buffer;
        meshes[i].IndexBuffer = buffer;
        meshes[i].Model[14] = -static_cast<float>(i % 256);
    }
    std::vector<RHISceneDrawItem> reused;
    RHISceneDrawItemBuilder::BuildInto(identity, identity, meshes, identity, reused);
    std::array<double, 5> freshTimes{}, reuseTimes{};
    constexpr int frames = 12;
    for (std::size_t sample = 0; sample < freshTimes.size(); ++sample)
    {
        // 値を消費するコスト、毎Frameの参照解放を両方に含めます。
        auto start = Clock::now();
        for (int frame = 0; frame < frames; ++frame)
        {
            auto items = RHISceneDrawItemBuilder::Build(identity, identity, meshes, identity);
            ComparisonSink = items.back().ViewDepth;
        }
        freshTimes[sample] = Elapsed(start) / frames;
        start = Clock::now();
        for (int frame = 0; frame < frames; ++frame)
        {
            RHISceneDrawItemBuilder::BuildInto(identity, identity, meshes, identity, reused);
            ComparisonSink = reused.back().ViewDepth;
            reused.clear();
        }
        reuseTimes[sample] = Elapsed(start) / frames;
    }
    AllocationMeasurement freshAllocation, reuseAllocation;
    {
        AllocationMeasurementScope scope(freshAllocation);
        auto items = RHISceneDrawItemBuilder::Build(identity, identity, meshes, identity);
        ComparisonSink = items.back().ViewDepth;
    }
    {
        AllocationMeasurementScope scope(reuseAllocation);
        RHISceneDrawItemBuilder::BuildInto(identity, identity, meshes, identity, reused);
    }
    std::cout << "SG5 snapshot count=" << count << " fresh_us=" << Median(freshTimes)
        << " reuse_us=" << Median(reuseTimes) << " allocation_available=" << freshAllocation.Available
        << " fresh_new_count=" << freshAllocation.Count << " reuse_new_count=" << reuseAllocation.Count
        << " fresh_new_bytes=" << freshAllocation.Bytes
        << " reuse_reserved_bytes=" << reused.capacity() * sizeof(RHISceneDrawItem) << '\n';
}

void MeasureWorld(std::size_t count, std::size_t churnPercent)
{
    std::array<double, 5> sceneTimes{}, mirrorTimes{}, kernelTimes{}, inputTimes{}, outputTimes{};
    std::array<double, 5> sceneCreate{}, mirrorCreate{}, sceneDestroy{}, mirrorDestroy{}, sceneMove{}, mirrorMove{};
    std::size_t retainedBytes = 0;
    AllocationMeasurement sceneAllocation, mirrorAllocation;
    for (std::size_t sample = 0; sample < sceneTimes.size(); ++sample)
    {
        Scene scene;
        Scene mirroredScene;
        auto asset = std::make_shared<const int>(7);
        auto start = Clock::now();
        std::vector<Entity> entities;
        {
            AllocationMeasurementScope scope(sceneAllocation);
            entities = Populate(scene, count, asset);
        }
        sceneCreate[sample] = Elapsed(start);
        ChunkMirror mirror(mirroredScene);
        start = Clock::now();
        std::vector<Entity> mirrored;
        {
            AllocationMeasurementScope scope(mirrorAllocation);
            mirrored = Populate(mirroredScene, count, asset);
            for (Entity entity : mirrored) { Require(mirror.Insert(entity), "Measurement insert failed."); }
        }
        mirrorCreate[sample] = Elapsed(start);
        constexpr std::size_t frames = 12;
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            const std::size_t changes = count * churnPercent / 100;
            start = Clock::now();
            for (std::size_t i = 0; i < changes; ++i) { Toggle(entities[(i + frame * changes) % count], asset); }
            sceneMove[sample] += Elapsed(start);
            start = Clock::now();
            for (std::size_t i = 0; i < changes; ++i)
            {
                Entity entity = mirrored[(i + frame * changes) % count];
                Toggle(entity, asset);
                Require(mirror.Migrate(entity), "Measurement migration failed.");
            }
            mirrorMove[sample] += Elapsed(start);
            start = Clock::now();
            SceneStep(scene);
            const double expected = SceneSnapshotChecksum(scene);
            sceneTimes[sample] += Elapsed(start);
            const auto totalStart = Clock::now();
            start = Clock::now();
            mirror.SyncInput();
            inputTimes[sample] += Elapsed(start);
            start = Clock::now();
            mirror.Step();
            const double actual = mirror.SnapshotChecksum();
            kernelTimes[sample] += Elapsed(start);
            start = Clock::now();
            mirror.WriteBack();
            outputTimes[sample] += Elapsed(start);
            mirrorTimes[sample] += Elapsed(totalStart);
            Require(expected == actual, "End-to-end comparison differs.");
            ComparisonSink = actual;
        }
        sceneTimes[sample] /= frames;
        mirrorTimes[sample] /= frames;
        kernelTimes[sample] /= frames;
        inputTimes[sample] /= frames;
        outputTimes[sample] /= frames;
        sceneMove[sample] /= frames;
        mirrorMove[sample] /= frames;
        retainedBytes = mirror.ReservedBytes();
        start = Clock::now();
        for (Entity entity : entities) { scene.DestroyEntity(entity); }
        sceneDestroy[sample] = Elapsed(start);
        start = Clock::now();
        for (Entity entity : mirrored)
        {
            const EntityHandle handle = entity.GetHandle();
            mirroredScene.DestroyEntity(entity);
            Require(mirror.Erase(handle), "Measurement destruction failed.");
        }
        mirrorDestroy[sample] = Elapsed(start);
    }
    std::cout << "SG5 world count=" << count << " churn_percent=" << churnPercent
        << " sparse_frame_us=" << Median(sceneTimes) << " mirror_frame_us=" << Median(mirrorTimes)
        << " chunk_kernel_us=" << Median(kernelTimes) << " input_us=" << Median(inputTimes)
        << " writeback_us=" << Median(outputTimes) << " sparse_migration_us=" << Median(sceneMove)
        << " mirror_migration_us=" << Median(mirrorMove)
        << " sparse_create_us=" << Median(sceneCreate) << " mirror_create_us=" << Median(mirrorCreate)
        << " sparse_destroy_us=" << Median(sceneDestroy) << " mirror_destroy_us=" << Median(mirrorDestroy)
        << " mirror_extra_reserved_bytes=" << retainedBytes
        << " allocation_available=" << sceneAllocation.Available
        << " sparse_create_new_bytes_5samples=" << sceneAllocation.Bytes
        << " mirror_create_new_bytes_5samples=" << mirrorAllocation.Bytes << '\n';
}
}

void RunSceneRuntimeComparisonTests()
{
    TestMirror();
    TestSnapshot();
}

void RunSceneRuntimeMeasurements()
{
    RunSceneRuntimeComparisonTests();
    std::cout << "SG5 fixed_chunk_capacity=" << ChunkCapacity << " motion_chunk_bytes=" << sizeof(MotionChunk)
        << " render_chunk_bytes=" << sizeof(RenderChunk) << " samples=5 frames_per_sample=12\n";
    for (std::size_t count : {4096u, 32768u})
    {
        for (std::size_t churn : {0u, 1u, 20u}) { MeasureWorld(count, churn); }
        MeasureSnapshot(count);
    }
    std::cout << "Scene Runtime measurements passed.\n";
}
}
