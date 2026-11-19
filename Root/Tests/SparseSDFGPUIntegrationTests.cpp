#include "Raven/Physics/Field/SparseSDF.h"
#include "Raven/Platform/DirectX12/DX12SceneContext.h"
#include "Raven/Platform/Vulkan/VulkanSceneContext.h"
#include "Raven/Renderer/RHI/RHIDevice.h"

#include <glad/glad.h>

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
using Microsoft::WRL::ComPtr;
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
    ~GpuTimer()
    {
        if (m_Vulkan != nullptr && m_VulkanQueries != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(m_Vulkan->GetDevice().GetHandle(), m_VulkanQueries, nullptr);
        }
        if (m_GLQueries[0] != 0)
        {
            glDeleteQueries(2, m_GLQueries);
        }
    }

    bool Init(RHISceneFrameLifecycle* frame)
    {
        m_DX = dynamic_cast<DX12SceneContext*>(frame);
        m_Vulkan = dynamic_cast<VulkanSceneContext*>(frame);
        if (m_DX != nullptr)
        {
            auto* device = m_DX->GetNativeDevice();
            std::uint64_t frequency = 0;
            if (m_DX->GetGraphicsTimestampFrequency(frequency) == false)
            {
                return false;
            }
            m_MicrosecondsPerTick = 1.0e6 / static_cast<double>(frequency);
            const D3D12_QUERY_HEAP_DESC query{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0};
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer{};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = 2 * sizeof(std::uint64_t);
            buffer.Height = 1;
            buffer.DepthOrArraySize = 1;
            buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1;
            buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            return SUCCEEDED(device->CreateQueryHeap(&query, IID_PPV_ARGS(&m_DXQueries))) &&
                SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_DXTicks)));
        }
        if (m_Vulkan != nullptr)
        {
            const auto& device = m_Vulkan->GetDevice();
            const auto physical = device.GetPhysicalDeviceHandle();
            std::uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
            const auto family = device.GetGraphicsQueueFamilyIndex();
            if (family >= count || queues[family].timestampValidBits == 0)
            {
                return false;
            }
            const auto bits = queues[family].timestampValidBits;
            m_Mask = bits == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1;
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physical, &properties);
            m_MicrosecondsPerTick = properties.limits.timestampPeriod / 1000.0;
            VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            query.queryType = VK_QUERY_TYPE_TIMESTAMP;
            query.queryCount = 2;
            return vkCreateQueryPool(device.GetHandle(), &query, nullptr, &m_VulkanQueries) == VK_SUCCESS;
        }
        glGenQueries(2, m_GLQueries);
        GLint bits = 0;
        glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
        return m_GLQueries[0] != 0 && bits > 0;
    }

    void Mark(std::uint32_t slot)
    {
        if (m_DX != nullptr)
        {
            auto* commands = m_DX->GetComputeCommandList();
            commands->EndQuery(m_DXQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot);
            if (slot == 1)
            {
                commands->ResolveQueryData(m_DXQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                    0, 2, m_DXTicks.Get(), 0);
            }
        }
        else if (m_Vulkan != nullptr)
        {
            const auto commands = m_Vulkan->GetComputeCommandBuffer();
            if (slot == 0)
            {
                vkCmdResetQueryPool(commands, m_VulkanQueries, 0, 2);
            }
            vkCmdWriteTimestamp(commands, slot == 0 ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT :
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_VulkanQueries, slot);
        }
        else
        {
            glQueryCounter(m_GLQueries[slot], GL_TIMESTAMP);
        }
    }

    double Read() const
    {
        std::uint64_t times[2]{};
        if (m_DX != nullptr)
        {
            void* mapped = nullptr;
            const D3D12_RANGE read{0, sizeof(times)};
            if (FAILED(m_DXTicks->Map(0, &read, &mapped)))
            {
                return -1.0;
            }
            std::memcpy(times, mapped, sizeof(times));
            const D3D12_RANGE written{0, 0};
            m_DXTicks->Unmap(0, &written);
        }
        else if (m_Vulkan != nullptr)
        {
            if (vkGetQueryPoolResults(m_Vulkan->GetDevice().GetHandle(), m_VulkanQueries, 0, 2,
                sizeof(times), times, sizeof(times[0]), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
            {
                return -1.0;
            }
        }
        else
        {
            for (std::size_t i = 0; i < 2; ++i)
            {
                GLuint64 value = 0;
                glGetQueryObjectui64v(m_GLQueries[i], GL_QUERY_RESULT, &value);
                times[i] = value;
            }
        }
        return static_cast<double>((times[1] - times[0]) & m_Mask) * m_MicrosecondsPerTick;
    }

private:
    DX12SceneContext* m_DX = nullptr;
    VulkanSceneContext* m_Vulkan = nullptr;
    ComPtr<ID3D12QueryHeap> m_DXQueries;
    ComPtr<ID3D12Resource> m_DXTicks;
    VkQueryPool m_VulkanQueries = VK_NULL_HANDLE;
    GLuint m_GLQueries[2]{};
    std::uint64_t m_Mask = ~std::uint64_t{0};
    double m_MicrosecondsPerTick = 0.001;
};

bool Submit(RHIComputeCommandEncoder& encoder, RHISceneFrameLifecycle* frame, GpuTimer& timer,
    const RHIComputeCommandEncoder::FrameRecorder& record)
{
    const auto timed = [&](RHIComputeCommandEncoder& commands)
    {
        timer.Mark(0);
        const bool success = record(commands);
        timer.Mark(1);
        return success;
    };
    if (frame == nullptr)
    {
        return timed(encoder);
    }
    // Resizeは既存Contextの完了処理を通すために使い、GPU timestampの区間には含まれません。
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
    std::vector<float>& distances, double& microseconds)
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
    microseconds = timer.Read();
    return microseconds >= 0.0;
}

struct Measurement
{
    std::size_t Batches = 0;
    std::size_t PeakGpuPayload = 0;
    double GpuMicroseconds = 0;
};

bool FinishSparse(SparseSDFUpdater& updater, RHIComputeCommandEncoder& encoder,
    RHISceneFrameLifecycle* frame, GpuTimer& timer, const RHIComputePipelineSpecification& shader,
    std::uint32_t tileBudget, Measurement& result)
{
    const auto previous = updater.GetSnapshot();
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
        const double elapsed = timer.Read();
        if (elapsed < 0.0)
        {
            return false;
        }
        result.GpuMicroseconds += elapsed;
        ++result.Batches;
    }
    return updater.GetSnapshot() != nullptr;
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
    SparseSDFUpdater sparse;
    Measurement metrics;
    if (Check(GenerateDense(encoder, frame, timer, denseShader, source, dense, denseTime), "generate Dense reference") == false ||
        sparse.BeginUpdate(source, Grid, Band) == false ||
        FinishSparse(sparse, encoder, frame, timer, sparseShader, 64, metrics) == false)
    {
        return false;
    }
    const auto snapshot = sparse.GetSnapshot();
    const auto denseBytes = dense.size() * sizeof(float);
    std::cout << backend << ' ' << name << " denseBytes=" << denseBytes
        << " sparseBytes=" << snapshot->GetPayloadBytes() << " sparsePeakGpuPayload=" << metrics.PeakGpuPayload
        << " denseGpuUs=" << denseTime << " sparseGpuUs=" << metrics.GpuMicroseconds
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
    const auto editedSnapshot = incremental.GetSnapshot();
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
        << " batches=" << edited.Batches << '\n';
    if (incremental.BeginUpdate(start, Grid, Band) == false ||
        incremental.PrepareBatch(encoder, shader, 17) == false)
    {
        return false;
    }
    incremental.Cancel();
    return Check(incremental.GetSnapshot() == editedSnapshot && incremental.GetRemainingTileCount() == 0,
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
        LoadSDFTestShader(device.GetBackend(), true, sparseShader) && timer.Init(frame), "initialize shaders and GPU timer") == false)
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
