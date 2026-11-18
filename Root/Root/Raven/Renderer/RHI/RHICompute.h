#pragma once

#include "Raven/Core/Base.h"

#include <array>
#include <cstddef>
#include <cstdint>
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
};

struct RHIComputePipelineSpecification
{
    std::string OpenGLSource;
    RHIComputeExtent LocalSize{1, 1, 1};
    std::vector<RHIComputeBindingLayout> Layout;
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

class RHIComputeReadback
{
public:
    virtual ~RHIComputeReadback() = default;
    // Readyになった場合だけCPU所有のコピーを返します。Pending中は出力を変更しません。
    virtual RHIComputeReadbackStatus Poll(std::vector<std::byte>& data) = 0;
};

class RHIComputeCommandEncoder
{
public:
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
};

} // namespace Raven
