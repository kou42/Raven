#pragma once

#include "Raven/Core/Base.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Raven
{

using RHIComputeExtent = std::array<std::uint32_t, 3>;

enum class RHIComputeBindingType { StorageBuffer, StorageImage3D };
enum class RHIComputeAccess { Read, Write, ReadWrite };
enum class RHIComputeConsumer { Storage, TextureSampling, BufferReadback };
enum class RHIComputeReadbackStatus { Pending, Ready, Failed };

struct RHIComputeCapabilities
{
    bool Supported = false;
    RHIComputeExtent MaxGroups{};
    RHIComputeExtent MaxLocalSize{};
    std::uint32_t MaxInvocations = 0;
    std::uint32_t MaxStorageBufferSlots = 0;
    std::uint32_t MaxStorageImageSlots = 0;
    std::uint32_t MaxStorageTexture3DSize = 0;
    std::uint64_t MaxStorageBufferBytes = 0;
};

struct RHIComputeBindingLayout
{
    std::uint32_t Slot = 0;
    RHIComputeBindingType Type = RHIComputeBindingType::StorageBuffer;
    RHIComputeAccess Access = RHIComputeAccess::Read;
    bool operator==(const RHIComputeBindingLayout&) const = default;
};

struct RHIComputePipelineSpecification
{
    std::string OpenGLSource;
    RHIComputeExtent LocalSize{1, 1, 1};
    std::vector<RHIComputeBindingLayout> Layout;
    std::vector<std::byte> DirectX12Bytecode;
    std::vector<std::uint32_t> VulkanSpirv;
    std::string VulkanEntryPoint = "main";
    bool operator==(const RHIComputePipelineSpecification&) const = default;
};

class RHIComputePipeline
{
public:
    virtual ~RHIComputePipeline() = default;
};

class RHIComputeResource
{
public:
    virtual ~RHIComputeResource() = default;
};

struct RHIComputeBinding
{
    std::uint32_t Slot = 0;
    Ref<RHIComputeResource> Resource;
};

class RHIComputeTimestamp
{
public:
    virtual ~RHIComputeTimestamp() = default;
    // GPU 完了を待機せず、Ready の場合だけ ns を返す。
    virtual RHIComputeReadbackStatus Poll(double& nanoseconds) = 0;
};

// 成功した操作の累積。Bytes は論理ペイロードで、ドライバの配置・常駐量とは異なる。
struct RHIComputeStatistics
{
    std::uint64_t Dispatches = 0;
    std::uint64_t Pipelines = 0;
    std::uint64_t Buffers = 0;
    std::uint64_t BufferBytes = 0;
    std::uint64_t Images = 0;
    std::uint64_t ImageBytes = 0;
    std::uint64_t Readbacks = 0;
    std::uint64_t ReadbackBytes = 0;
    std::uint64_t DescriptorAllocations = 0;
    std::uint64_t DescriptorReuses = 0;
};

class RHIComputeReadback
{
public:
    virtual ~RHIComputeReadback() = default;
    // Readyになった場合だけCPU所有のコピーを返します。Pending中は出力を変更しません。
    virtual RHIComputeReadbackStatus Poll(std::vector<std::byte>& data) = 0;
};

// 次のExplicit Scene FrameのRender Pass開始前に記録する仕事です。
// Contextは記録済みの仕事と所属リソースをFrameのGPU完了まで保持します。
class RHIComputeFrameWork
{
public:
    virtual ~RHIComputeFrameWork() = default;
    virtual bool Record() = 0;
    virtual void Complete(bool gpuCompleted) noexcept = 0;
};

class RHIComputeCommandEncoder
{
public:
    RHIComputeCommandEncoder() = default;
    RHIComputeCommandEncoder(const RHIComputeCommandEncoder&) = delete;
    RHIComputeCommandEncoder& operator=(const RHIComputeCommandEncoder&) = delete;
    virtual ~RHIComputeCommandEncoder() = default;
    // OpenGLの初期実装では同じContext上で操作し、Context破棄前に全ハンドルを解放します。
    // Dispatch/Barrier/ReadbackはGraphics Passの外で呼び出してください。
    virtual Ref<RHIComputePipeline> CreatePipeline(const RHIComputePipelineSpecification& spec) = 0;
    virtual Ref<RHIComputeResource> CreateStorageBuffer(std::size_t size, const void* initialData) = 0;
    // Phase 03では単一MipのR32F 3D TextureをStorage/Sampled両用途で作成します。
    virtual Ref<RHIComputeResource> CreateStorageImage3D(const RHIComputeExtent& size) = 0;
    virtual bool Dispatch(const Ref<RHIComputePipeline>& pipeline,
        std::span<const RHIComputeBinding> bindings, const RHIComputeExtent& groups) = 0;
    virtual bool Barrier(RHIComputeConsumer consumer) = 0;
    virtual Ref<RHIComputeReadback> RequestBufferReadback(
        const Ref<RHIComputeResource>& resource, std::size_t offset, std::size_t size) = 0;
    // Explicit Backendでは、リソース準備後に次Frameの描画前に記録を予約します。
    // CaptureしたリソースはContextが完了を観測するまで保持されます。
    using FrameRecorder = std::function<bool(RHIComputeCommandEncoder&)>;
    virtual bool QueueFrame(FrameRecorder recorder) { (void)recorder; return false; }
    // work は同期的に一度記録される。タイムスタンプ非対応時も work を実行する。
    virtual bool MeasureGpuWork(const FrameRecorder& work, Ref<RHIComputeTimestamp>& timestamp)
    { timestamp.reset(); return work(*this); }
    RHIComputeStatistics GetStatistics() const { return m_Statistics; }
    std::weak_ptr<const std::byte> GetIdentity() const { return m_Identity; }

private:
    const Ref<const std::byte> m_Identity = CreateRef<const std::byte>();

protected:
    RHIComputeStatistics m_Statistics;
};

} // namespace Raven
