#include "Raven/Physics/Field/SparseSDF.h"
#include "Raven/Physics/Collision/StaticSDFCollider.h"
#include "Raven/Renderer/RHI/RHIDevice.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

bool LoadSDFTestShader(Raven::RHIBackend backend, bool sparse, Raven::RHIComputePipelineSpecification& shader);

namespace
{
using namespace Raven;
using namespace Raven::ph;
using math::Vec3;
constexpr float Band = 0.13f;
constexpr float Tolerance = 1.0e-4f;
const DenseSDFGrid Grid{{-3.2f, -3.0f, -2.8f}, {0.10f, 0.10f, 0.10f}, {65, 61, 57}};

bool Check(bool passed, const char* label)
{
    if (passed == false)
    {
        std::cerr << "[FAIL] Sparse SDF: " << label << '\n';
    }
    return passed;
}

class GpuTimer
{
public:
    bool Record(RHIComputeCommandEncoder& commands, const RHIComputeCommandEncoder::FrameRecorder& record)
    {
        const bool success = commands.MeasureGpuWork([&](auto& scope)
        {
            const bool recorded = record(scope);
            m_ReadbackRequested = Clock::now();
            return recorded;
        }, m_Timestamp);
        return success;
    }

    double ReadbackMicroseconds() const
    {
        return std::chrono::duration<double, std::micro>(Clock::now() - m_ReadbackRequested).count();
    }

    double Read() const
    {
        double nanoseconds = 0.0;
        if (m_Timestamp == nullptr)
        {
            return -1.0;
        }
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        auto status = m_Timestamp->Poll(nanoseconds);
        while (status == RHIComputeReadbackStatus::Pending && Clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            status = m_Timestamp->Poll(nanoseconds);
        }
        return status == RHIComputeReadbackStatus::Ready ? nanoseconds / 1000.0 : -1.0;
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_ReadbackRequested;
    Ref<RHIComputeTimestamp> m_Timestamp;
};

bool Submit(RHIComputeCommandEncoder& encoder, RHISceneFrameLifecycle* frame, GpuTimer& timer,
    const RHIComputeCommandEncoder::FrameRecorder& record)
{
    const auto timed = [&](auto& commands) { return timer.Record(commands, record); };
    if (frame == nullptr)
    {
        return timed(encoder);
    }
    // Resize は既存の Frame retire 経路を通す。ホスト側の Readback 観測時間には含まれる。
    return encoder.QueueFrame(timed) && frame->BeginFrame() == RHIFrameResult::Success &&
        frame->EndFrame() == RHIFrameResult::Success && frame->Present() != RHIFrameResult::FatalError &&
        frame->Resize(320, 240);
}

std::optional<DenseSDFSource> MakeBoxes(DenseSDFSource::Mode mode, float offset)
{
    constexpr DenseSDFSource::Triangle indices[] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7},
        {0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    const Vec3 cube[] = {{-0.4f,-0.4f,-0.4f}, {0.4f,-0.4f,-0.4f}, {0.4f,0.4f,-0.4f}, {-0.4f,0.4f,-0.4f},
        {-0.4f,-0.4f,0.4f}, {0.4f,-0.4f,0.4f}, {0.4f,0.4f,0.4f}, {-0.4f,0.4f,0.4f}};
    std::vector<Vec3> vertices;
    std::vector<DenseSDFSource::Triangle> triangles;
    for (std::uint32_t box = 0; box < 2; ++box)
    {
        const Vec3 center = box == 0 ? Vec3{-1.5f, 0.02f, offset} : Vec3{1.5f, -0.07f, 0.04f};
        for (auto vertex : cube)
        {
            vertices.push_back(vertex + center);
        }
        for (const auto& ids : indices)
        {
            triangles.push_back({ids[0] + 8 * box, ids[1] + 8 * box, ids[2] + 8 * box});
        }
    }
    return DenseSDFSource::CreateMesh(vertices, triangles, mode);
}

bool GenerateDense(RHIComputeCommandEncoder& encoder, RHISceneFrameLifecycle* frame, GpuTimer& timer,
    const RHIComputePipelineSpecification& shader, const DenseSDFSource& source,
    std::vector<float>& distances, double& microseconds, double& readbackMicroseconds)
{
    DenseSDFGenerator generator;
    if (generator.Prepare(encoder, shader, source, Grid) == false)
    {
        return false;
    }
    Ref<RHIComputeReadback> ticket;
    if (Submit(encoder, frame, timer, [&](auto& commands)
        {
            ticket = generator.Record(commands);
            return ticket != nullptr;
        }) == false)
    {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    std::vector<std::byte> bytes;
    auto status = ticket->Poll(bytes);
    while (status == RHIComputeReadbackStatus::Pending && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        status = ticket->Poll(bytes);
    }
    if (status != RHIComputeReadbackStatus::Ready || bytes.size() != Grid.GetVoxelCount() * sizeof(float))
    {
        return false;
    }
    distances.resize(Grid.GetVoxelCount());
    std::memcpy(distances.data(), bytes.data(), bytes.size());
    readbackMicroseconds = timer.ReadbackMicroseconds();
    microseconds = timer.Read();
    return microseconds >= 0.0;
}

struct Measurement
{
    std::size_t Batches = 0;
    std::size_t PeakGpuPayload = 0;
    double GpuMicroseconds = 0;
    double ReadbackMicroseconds = 0;
    std::uint64_t BufferAllocations = 0;
    std::uint64_t PipelineAllocations = 0;
    std::uint64_t AllocatedBufferBytes = 0;
};

bool FinishSparse(SparseSDFUpdater& updater, RHIComputeCommandEncoder& encoder,
    RHISceneFrameLifecycle* frame, GpuTimer& timer, const RHIComputePipelineSpecification& shader,
    std::uint32_t tileBudget, Measurement& result)
{
    const auto previous = updater.GetSnapshot();
    const auto before = encoder.GetStatistics();
    while (updater.GetRemainingTileCount() != 0)
    {
        if (Check(updater.PrepareBatch(encoder, shader, tileBudget), "prepare bounded batch") == false)
        {
            return false;
        }
        result.PeakGpuPayload = std::max(result.PeakGpuPayload, updater.GetPreparedPayloadBytes());
        if (Submit(encoder, frame, timer, [&](auto& commands) { return updater.RecordBatch(commands); }) == false ||
            updater.GetSnapshot() != previous)
        {
            return Check(false, "submit batch without publishing");
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        auto status = updater.Poll();
        while (status == SparseSDFUpdateStatus::Pending && std::chrono::steady_clock::now() < deadline)
        {
            if (updater.GetSnapshot() != previous)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            status = updater.Poll();
        }
        const bool last = updater.GetRemainingTileCount() == 0;
        if (status != (last ? SparseSDFUpdateStatus::Published : SparseSDFUpdateStatus::BatchComplete) ||
            (updater.GetSnapshot() != previous) != last)
        {
            return Check(false, "publish only after final batch");
        }
        result.ReadbackMicroseconds += timer.ReadbackMicroseconds();
        const double elapsed = timer.Read();
        if (elapsed < 0.0)
        {
            return false;
        }
        result.GpuMicroseconds += elapsed;
        ++result.Batches;
    }
    const auto after = encoder.GetStatistics();
    result.BufferAllocations = after.Buffers - before.Buffers;
    result.PipelineAllocations = after.Pipelines - before.Pipelines;
    result.AllocatedBufferBytes = after.BufferBytes - before.BufferBytes;
    return updater.GetSnapshot() != nullptr &&
        Check(result.Batches == 0 || (result.BufferAllocations == result.Batches + 1 &&
            after.Dispatches - before.Dispatches == result.Batches &&
            after.Readbacks - before.Readbacks == result.Batches), "reuse completed output buffer across batches");
}

std::size_t Index(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return x + Grid.Size[0] * (y + Grid.Size[1] * z);
}

bool Compare(const SparseSDFSnapshot& snapshot, std::span<const float> dense, const char* backend,
    const char* name, bool signedDistance)
{
    float maxDistanceError = 0.0f;
    float maxInterpolationError = 0.0f;
    float quantizationError = 0.0f;
    std::size_t samples = 0;
    std::size_t crossTileSamples = 0;
    std::size_t negative = 0;
    for (std::uint32_t z = 0; z < Grid.Size[2]; ++z)
    {
        for (std::uint32_t y = 0; y < Grid.Size[1]; ++y)
        {
            for (std::uint32_t x = 0; x < Grid.Size[0]; ++x)
            {
                const float expected = dense[Index(x, y, z)];
                float actual = 0.0f;
                const bool present = snapshot.TryGetCell({x, y, z}, actual);
                if ((std::abs(expected) < Band - Tolerance && present == false) ||
                    (std::abs(expected) > Band + Tolerance && present) ||
                    (present && std::abs(expected) > Tolerance && std::signbit(expected) != std::signbit(actual)))
                {
                    return Check(false, "narrow band coverage and sign");
                }
                if (present)
                {
                    maxDistanceError = std::max(maxDistanceError, std::abs(expected - actual));
                    const float support = Band + Grid.Spacing.Length();
                    const float quantized = std::round(actual * 127.0f / support) * support / 127.0f;
                    quantizationError = std::max(quantizationError, std::abs(quantized - actual));
                    negative += actual < -Tolerance ? 1 : 0;
                    ++samples;
                }
                if (x % SparseSDFSnapshot::TileWidth != SparseSDFSnapshot::TileWidth - 1 ||
                    x + 1 >= Grid.Size[0] || y + 1 >= Grid.Size[1] || z + 1 >= Grid.Size[2])
                {
                    continue;
                }
                float average = 0.0f;
                for (std::uint32_t dz = 0; dz < 2; ++dz)
                {
                    for (std::uint32_t dy = 0; dy < 2; ++dy)
                    {
                        for (std::uint32_t dx = 0; dx < 2; ++dx)
                        {
                            average += dense[Index(x + dx, y + dy, z + dz)] * 0.125f;
                        }
                    }
                }
                if (std::abs(average) < Band - Tolerance)
                {
                    const auto position = Grid.GetCellCenter(x, y, z) + Grid.Spacing * 0.5f;
                    if (snapshot.TrySample(position, actual) == false)
                    {
                        return Check(false, "trilinear lookup across tile boundary");
                    }
                    maxInterpolationError = std::max(maxInterpolationError, std::abs(average - actual));
                    ++crossTileSamples;
                }
            }
        }
    }
    std::cout << backend << ' ' << name << " tiles=" << snapshot.GetTileCount()
        << " inBand=" << samples << " crossTile=" << crossTileSamples
        << " maxDistanceError=" << maxDistanceError << " maxInterpolationError=" << maxInterpolationError
        << " maxInt8Error=" << quantizationError << '\n';
    return Check(samples > 0 && crossTileSamples > 0 && (signedDistance == (negative > 0)) &&
        maxDistanceError <= Tolerance && maxInterpolationError <= Tolerance &&
        quantizationError > Tolerance, "distance, interpolation and quantization comparison");
}

bool Measure(RHIComputeCommandEncoder& encoder, RHISceneFrameLifecycle* frame, GpuTimer& timer,
    const RHIComputePipelineSpecification& denseShader, const RHIComputePipelineSpecification& sparseShader,
    const DenseSDFSource& source, const char* backend, const char* name, bool signedDistance)
{
    std::vector<float> dense;
    double denseTime = 0.0;
    double denseReadback = 0.0;
    SparseSDFUpdater sparse;
    Measurement metrics;
    if (Check(GenerateDense(encoder, frame, timer, denseShader, source, dense, denseTime, denseReadback), "generate Dense reference") == false ||
        sparse.BeginUpdate(source, Grid, Band) == false ||
        FinishSparse(sparse, encoder, frame, timer, sparseShader, 64, metrics) == false)
    {
        return false;
    }
    const auto snapshot = sparse.GetSnapshot();
    if (source.GetMode() == DenseSDFSource::Mode::Sphere)
    {
        const auto boundary = StaticSDFBoundary::FromSnapshot(snapshot, 0.025f);
        SDFSphereContact contact{};
        const auto center = source.GetCenter() + Vec3{source.GetRadius() + 0.015f, 0, 0};
        if (Check(boundary != nullptr && GenerateSDFSphereContact(*boundary, center, 0.04f, contact) &&
            contact.OutwardNormal.x > 0.99f && contact.Penetration > 0.0f &&
            GenerateSDFSphereContact(*boundary, source.GetCenter(), 0.04f, contact) == false,
            "CPU contact with published signed Sparse SDF and missing band samples") == false)
        {
            return false;
        }
    }
    const auto denseBytes = dense.size() * sizeof(float);
    std::cout << backend << ' ' << name << " denseBytes=" << denseBytes
        << " sparseBytes=" << snapshot->GetPayloadBytes() << " sparsePeakGpuPayload=" << metrics.PeakGpuPayload
        << " denseGpuUs=" << denseTime << " sparseGpuUs=" << metrics.GpuMicroseconds
        << " denseReadbackUs=" << denseReadback << " sparseReadbackUs=" << metrics.ReadbackMicroseconds
        << " sparseBufferAllocations=" << metrics.BufferAllocations
        << " sparseAllocatedBufferBytes=" << metrics.AllocatedBufferBytes
        << " batches=" << metrics.Batches << '\n';
    return Check(snapshot->GetPayloadBytes() < denseBytes, "sparse storage uses less sample payload") &&
        Compare(*snapshot, dense, backend, name, signedDistance);
}

bool VerifyIncremental(RHIComputeCommandEncoder& encoder, RHISceneFrameLifecycle* frame, GpuTimer& timer,
    const RHIComputePipelineSpecification& shader, const char* backend, DenseSDFSource::Mode mode)
{
    const auto start = MakeBoxes(mode, 0.0f).value();
    const auto moved = MakeBoxes(mode, 0.34f).value();
    SparseSDFUpdater incremental;
    SparseSDFUpdater fresh;
    Measurement initial, edited, rebuilt;
    if (incremental.BeginUpdate(start, Grid, Band) == false ||
        FinishSparse(incremental, encoder, frame, timer, shader, 64, initial) == false)
    {
        return false;
    }
    const auto original = incremental.GetSnapshot();
    if (incremental.BeginUpdate(moved, Grid, Band) == false ||
        fresh.BeginUpdate(moved, Grid, Band) == false ||
        Check(incremental.BeginUpdate(start, Grid, Band) == false, "reject overlapping update") == false)
    {
        return false;
    }
    const auto dirty = incremental.GetRemainingTileCount();
    const auto total = fresh.GetRemainingTileCount();
    if (Check(dirty > 0 && dirty < total, "local edit keeps unaffected tiles") == false ||
        FinishSparse(incremental, encoder, frame, timer, shader, 17, edited) == false ||
        FinishSparse(fresh, encoder, frame, timer, shader, 64, rebuilt) == false)
    {
        return false;
    }
    if (Check(initial.PipelineAllocations == 1 && edited.PipelineAllocations == 0 &&
        rebuilt.PipelineAllocations == 1, "reuse shader pipeline across updates") == false)
    {
        return false;
    }
    const auto editedSnapshot = incremental.GetSnapshot();
    const auto beforeNoop = encoder.GetStatistics();
    if (Check(incremental.BeginUpdate(moved, Grid, Band) && incremental.GetRemainingTileCount() == 0 &&
        incremental.GetSnapshot()->GetGeneration() == editedSnapshot->GetGeneration() + 1 &&
        incremental.GetSnapshot()->GetTileCount() == editedSnapshot->GetTileCount() &&
        encoder.GetStatistics().Dispatches == beforeNoop.Dispatches, "identical update publishes without GPU work") == false)
    {
        return false;
    }
    const auto noOpSnapshot = incremental.GetSnapshot();
    if (mode == DenseSDFSource::Mode::UnsignedMesh &&
        Check(StaticSDFBoundary::FromSnapshot(editedSnapshot, 0.025f) == nullptr,
            "unsigned Sparse SDF cannot be registered as a signed boundary") == false)
    {
        return false;
    }
    const auto rebuiltSnapshot = fresh.GetSnapshot();
    if (Check(edited.Batches > 1 && editedSnapshot->GetGeneration() == original->GetGeneration() + 1 &&
        editedSnapshot->GetTileCount() == rebuiltSnapshot->GetTileCount(), "multiframe generation and tile removal") == false)
    {
        return false;
    }
    for (std::uint32_t z = 0; z < Grid.Size[2]; ++z)
    {
        for (std::uint32_t y = 0; y < Grid.Size[1]; ++y)
        {
            for (std::uint32_t x = 0; x < Grid.Size[0]; ++x)
            {
                float editedValue = 0.0f;
                float rebuiltValue = 0.0f;
                if (editedSnapshot->TryGetCell({x, y, z}, editedValue) != rebuiltSnapshot->TryGetCell({x, y, z}, rebuiltValue) ||
                    std::abs(editedValue - rebuiltValue) > Tolerance)
                {
                    return Check(false, "incremental versus rebuilt samples");
                }
            }
        }
    }
    std::cout << backend << " incremental-" << (mode == DenseSDFSource::Mode::ClosedMesh ? "signed" : "unsigned")
        << " dirtyTiles=" << dirty << " rebuiltTiles=" << total
        << " buffers=" << edited.BufferAllocations << " pipelines=" << edited.PipelineAllocations << " batches=" << edited.Batches << '\n';
    if (incremental.BeginUpdate(start, Grid, Band) == false ||
        incremental.PrepareBatch(encoder, shader, 17) == false)
    {
        return false;
    }
    incremental.Cancel();
    return Check(incremental.GetSnapshot() == noOpSnapshot && incremental.GetRemainingTileCount() == 0,
        "cancel keeps published snapshot");
}
} // namespace

bool RunSparseSDFCases(Raven::RHIDevice& device, const char* backend, Raven::RHISceneFrameLifecycle* frame)
{
    auto encoder = device.CreateComputeCommandEncoder();
    RHIComputePipelineSpecification denseShader;
    RHIComputePipelineSpecification sparseShader;
    GpuTimer timer;
    if (Check(encoder != nullptr && LoadSDFTestShader(device.GetBackend(), false, denseShader) &&
        LoadSDFTestShader(device.GetBackend(), true, sparseShader), "initialize SDF shaders") == false)
    {
        return false;
    }
    const auto sphere = DenseSDFSource::CreateSphere({0.03f, -0.02f, 0.04f}, 0.73f).value();
    const auto closed = MakeBoxes(DenseSDFSource::Mode::ClosedMesh, 0.0f).value();
    return Measure(*encoder, frame, timer, denseShader, sparseShader, sphere, backend, "sphere", true) &&
        Measure(*encoder, frame, timer, denseShader, sparseShader, closed, backend, "closed-boxes", true) &&
        VerifyIncremental(*encoder, frame, timer, sparseShader, backend, DenseSDFSource::Mode::UnsignedMesh) &&
        VerifyIncremental(*encoder, frame, timer, sparseShader, backend, DenseSDFSource::Mode::ClosedMesh);
}
