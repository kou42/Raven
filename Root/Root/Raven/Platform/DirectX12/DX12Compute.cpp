#include "DX12SceneRHIDevice.h"
#include "Raven/Renderer/RHI/RHIExplicitComputeFrame.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace Raven
{
namespace
{
using Microsoft::WRL::ComPtr;
struct ComputeOwner {};

struct Pipeline final : RHIComputePipeline
{
    ComPtr<ID3D12RootSignature> Root;
    ComPtr<ID3D12PipelineState> State;
    std::vector<RHIComputeBindingLayout> Layout;
    Ref<ComputeOwner> Owner;
};

struct Resource final : RHIComputeResource
{
    ComPtr<ID3D12Resource> Native;
    ComPtr<ID3D12Resource> Upload;
    D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    RHIComputeBindingType Type = RHIComputeBindingType::StorageBuffer;
    std::size_t Size = 0;
    Ref<ComputeOwner> Owner;
};

struct Readback final : RHIComputeReadback
{
    ComPtr<ID3D12Resource> Staging;
    std::size_t Size = 0;
    RHIComputeReadbackStatus Status = RHIComputeReadbackStatus::Pending;

    RHIComputeReadbackStatus Poll(std::vector<std::byte>& data) override
    {
        if (Status != RHIComputeReadbackStatus::Ready)
        {
            return Status;
        }
        std::vector<std::byte> copy(Size);
        void* mapped = nullptr;
        const D3D12_RANGE range{0, Size};
        if (FAILED(Staging->Map(0, &range, &mapped)))
        {
            Status = RHIComputeReadbackStatus::Failed;
            return Status;
        }
        std::memcpy(copy.data(), mapped, Size);
        const D3D12_RANGE written{0, 0};
        Staging->Unmap(0, &written);
        data = std::move(copy);
        return Status;
    }
};

bool Fits(const RHIComputeExtent& values, const RHIComputeExtent& limits)
{
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (values[i] == 0 || values[i] > limits[i])
        {
            return false;
        }
    }
    return true;
}

RHIComputeCapabilities Capabilities(ID3D12Device* device)
{
    if (device == nullptr)
    {
        return {};
    }
    RHIComputeCapabilities caps{};
    caps.Supported = true;
    caps.MaxGroups = {65535, 65535, 65535};
    caps.MaxLocalSize = {1024, 1024, 64};
    caps.MaxInvocations = 1024;
    caps.MaxStorageBufferSlots = 8;
    caps.MaxStorageBufferBytes = std::numeric_limits<UINT>::max();
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format{DXGI_FORMAT_R32_FLOAT};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &format, sizeof(format))) &&
        (format.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE3D) != 0 &&
        (format.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0 &&
        (format.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0 &&
        (format.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0)
    {
        caps.MaxStorageImageSlots = 8;
        caps.MaxStorageTexture3DSize = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
    }
    return caps;
}

ComPtr<ID3D12Resource> Allocate(ID3D12Device& device, const D3D12_RESOURCE_DESC& description,
    D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    ComPtr<ID3D12Resource> resource;
    if (FAILED(device.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
        state, nullptr, IID_PPV_ARGS(&resource))))
    {
        return {};
    }
    return resource;
}

D3D12_RESOURCE_DESC BufferDescription(std::size_t size, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = flags;
    return description;
}

void Transition(ID3D12GraphicsCommandList& commands, Resource& resource, D3D12_RESOURCE_STATES state)
{
    if (resource.State == state)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource.Native.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        resource.State, state};
    commands.ResourceBarrier(1, &barrier);
    resource.State = state;
}

struct DispatchResources
{
    ComPtr<ID3D12DescriptorHeap> Heap;
    std::vector<Ref<Resource>> Bindings;
    std::vector<ComPtr<ID3D12Resource>> Uploads;
};

class DX12Encoder final : public RHIComputeCommandEncoder,
    public std::enable_shared_from_this<DX12Encoder>
{
public:
    explicit DX12Encoder(DX12SceneContext& context)
        : m_Context(context), m_Device(context.GetNativeDevice()), m_Caps(Capabilities(m_Device.Get()))
    {
    }

    bool QueueFrame(FrameRecorder recorder) override
    {
        if (recorder == nullptr || IsValid() == false)
        {
            return false;
        }
        auto frame = CreateRef<RHIExplicitComputeFrame>(
            [self = shared_from_this(), recorder = std::move(recorder)](RHIExplicitComputeFrame& work)
            {
                self->m_ActiveFrame = &work;
                const bool recorded = recorder(*self);
                self->m_ActiveFrame = nullptr;
                self->m_StorageImages.clear();
                self->m_UsedBuffers.clear();
                return recorded;
            });
        return m_Context.QueueComputeWork(frame);
    }

    Ref<RHIComputePipeline> CreatePipeline(const RHIComputePipelineSpecification& spec) override
    {
        if (CanPrepare() == false || spec.DirectX12Bytecode.empty() ||
            Fits(spec.LocalSize, m_Caps.MaxLocalSize) == false ||
            std::uint64_t(spec.LocalSize[0]) * spec.LocalSize[1] * spec.LocalSize[2] > m_Caps.MaxInvocations)
        {
            return nullptr;
        }
        std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
        for (const auto& binding : spec.Layout)
        {
            const auto limit = binding.Type == RHIComputeBindingType::StorageBuffer
                ? m_Caps.MaxStorageBufferSlots : m_Caps.MaxStorageImageSlots;
            if (binding.Slot >= limit || std::any_of(ranges.begin(), ranges.end(), [&](const auto& range)
                { return range.BaseShaderRegister == binding.Slot; }))
            {
                return nullptr;
            }
            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = binding.Slot;
            range.OffsetInDescriptorsFromTableStart = static_cast<UINT>(ranges.size());
            ranges.push_back(range);
        }
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable = {static_cast<UINT>(ranges.size()), ranges.data()};
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC root{};
        if (ranges.empty() == false)
        {
            root.NumParameters = 1;
            root.pParameters = &parameter;
        }
        ComPtr<ID3DBlob> serialized;
        if (FAILED(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, nullptr)))
        {
            return nullptr;
        }
        auto pipeline = CreateRef<Pipeline>();
        pipeline->Layout = spec.Layout;
        pipeline->Owner = m_Owner;
        if (FAILED(m_Device->CreateRootSignature(0, serialized->GetBufferPointer(),
            serialized->GetBufferSize(), IID_PPV_ARGS(&pipeline->Root))))
        {
            return nullptr;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC native{};
        native.pRootSignature = pipeline->Root.Get();
        native.CS = {spec.DirectX12Bytecode.data(), spec.DirectX12Bytecode.size()};
        if (FAILED(m_Device->CreateComputePipelineState(&native, IID_PPV_ARGS(&pipeline->State))))
        {
            return nullptr;
        }
        return pipeline;
    }

    Ref<RHIComputeResource> CreateStorageBuffer(std::size_t size, const void* initialData) override
    {
        if (CanPrepare() == false || size == 0 || size % 4 != 0 || size > m_Caps.MaxStorageBufferBytes)
        {
            return nullptr;
        }
        auto resource = CreateRef<Resource>();
        resource->Owner = m_Owner;
        resource->Size = size;
        resource->State = D3D12_RESOURCE_STATE_COMMON;
        if (initialData != nullptr)
        {
            resource->Upload = Allocate(*m_Device.Get(), BufferDescription(size, D3D12_RESOURCE_FLAG_NONE),
                D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
            void* mapped = nullptr;
            const D3D12_RANGE read{0, 0};
            if (resource->Upload == nullptr || FAILED(resource->Upload->Map(0, &read, &mapped)))
            {
                return nullptr;
            }
            std::memcpy(mapped, initialData, size);
            resource->Upload->Unmap(0, nullptr);
        }
        resource->Native = Allocate(*m_Device.Get(),
            BufferDescription(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_HEAP_TYPE_DEFAULT, resource->State);
        return resource->Native != nullptr ? resource : nullptr;
    }

    Ref<RHIComputeResource> CreateStorageImage3D(const RHIComputeExtent& size) override
    {
        const auto limit = m_Caps.MaxStorageTexture3DSize;
        if (CanPrepare() == false || Fits(size, {limit, limit, limit}) == false)
        {
            return nullptr;
        }
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        description.Width = size[0];
        description.Height = size[1];
        description.DepthOrArraySize = static_cast<UINT16>(size[2]);
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_R32_FLOAT;
        description.SampleDesc.Count = 1;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto resource = CreateRef<Resource>();
        resource->Owner = m_Owner;
        resource->Type = RHIComputeBindingType::StorageImage3D;
        resource->Native = Allocate(*m_Device.Get(), description, D3D12_HEAP_TYPE_DEFAULT, resource->State);
        return resource->Native != nullptr ? resource : nullptr;
    }

    bool Dispatch(const Ref<RHIComputePipeline>& pipeline, std::span<const RHIComputeBinding> bindings,
        const RHIComputeExtent& groups) override
    {
        auto* commands = GetCommands();
        const auto native = std::dynamic_pointer_cast<Pipeline>(pipeline);
        if (commands == nullptr || native == nullptr || native->Owner != m_Owner ||
            bindings.size() != native->Layout.size() || Fits(groups, m_Caps.MaxGroups) == false)
        {
            return false;
        }
        auto work = CreateRef<DispatchResources>();
        for (const auto& layout : native->Layout)
        {
            const auto binding = std::find_if(bindings.begin(), bindings.end(),
                [&](const auto& entry) { return entry.Slot == layout.Slot; });
            if (binding == bindings.end())
            {
                return false;
            }
            const auto resource = std::dynamic_pointer_cast<Resource>(binding->Resource);
            if (resource == nullptr || resource->Owner != m_Owner || resource->Type != layout.Type ||
                std::find(work->Bindings.begin(), work->Bindings.end(), resource) != work->Bindings.end())
            {
                return false;
            }
            work->Bindings.push_back(resource);
        }
        if (work->Bindings.empty() == false && PrepareDescriptors(*work) == false)
        {
            return false;
        }
        m_ActiveFrame->Retain(native);
        m_ActiveFrame->Retain(work);
        for (const auto& resource : work->Bindings)
        {
            BeginBufferUse(*resource);
            if (resource->Upload != nullptr)
            {
                Transition(*commands, *resource, D3D12_RESOURCE_STATE_COPY_DEST);
                commands->CopyBufferRegion(resource->Native.Get(), 0, resource->Upload.Get(), 0, resource->Size);
                // 転送元は初回DispatchのFrameだけが完了まで所有します。
                work->Uploads.push_back(std::move(resource->Upload));
            }
            Transition(*commands, *resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (resource->Type == RHIComputeBindingType::StorageImage3D &&
                std::find(m_StorageImages.begin(), m_StorageImages.end(), resource) == m_StorageImages.end())
            {
                m_StorageImages.push_back(resource);
            }
        }
        commands->SetPipelineState(native->State.Get());
        commands->SetComputeRootSignature(native->Root.Get());
        if (work->Heap != nullptr)
        {
            ID3D12DescriptorHeap* heaps[] = {work->Heap.Get()};
            commands->SetDescriptorHeaps(1, heaps);
            commands->SetComputeRootDescriptorTable(0, work->Heap->GetGPUDescriptorHandleForHeapStart());
        }
        commands->Dispatch(groups[0], groups[1], groups[2]);
        return true;
    }

    bool Barrier(RHIComputeConsumer consumer) override
    {
        auto* commands = GetCommands();
        if (commands == nullptr)
        {
            return false;
        }
        switch (consumer)
        {
        case RHIComputeConsumer::Storage:
        {
            // UAV状態を維持するDispatch間でもShader書き込みを順序付けます。
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            commands->ResourceBarrier(1, &barrier);
            return true;
        }
        case RHIComputeConsumer::TextureSampling:
            for (const auto& resource : m_StorageImages)
            {
                Transition(*commands, *resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
            }
            m_StorageImages.clear();
            return true;
        case RHIComputeConsumer::BufferReadback:
            // RequestBufferReadbackが指定したBufferだけをCOPY_SOURCEへ遷移します。
            return true;
        }
        return false;
    }

    Ref<RHIComputeReadback> RequestBufferReadback(const Ref<RHIComputeResource>& resource,
        std::size_t offset, std::size_t size) override
    {
        auto* commands = GetCommands();
        const auto source = std::dynamic_pointer_cast<Resource>(resource);
        if (commands == nullptr || source == nullptr || source->Owner != m_Owner ||
            source->Type != RHIComputeBindingType::StorageBuffer ||
            source->Upload != nullptr ||
            size == 0 || offset > source->Size || size > source->Size - offset)
        {
            return nullptr;
        }
        auto ticket = CreateRef<Readback>();
        ticket->Size = size;
        ticket->Staging = Allocate(*m_Device.Get(), BufferDescription(size, D3D12_RESOURCE_FLAG_NONE),
            D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        if (ticket->Staging == nullptr)
        {
            return nullptr;
        }
        m_ActiveFrame->Retain(source);
        m_ActiveFrame->OnCompletion([ticket](bool completed)
        {
            ticket->Status = completed ? RHIComputeReadbackStatus::Ready : RHIComputeReadbackStatus::Failed;
        });
        BeginBufferUse(*source);
        Transition(*commands, *source, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commands->CopyBufferRegion(ticket->Staging.Get(), 0, source->Native.Get(), offset, size);
        return ticket;
    }

private:
    void BeginBufferUse(Resource& resource)
    {
        if (resource.Type == RHIComputeBindingType::StorageBuffer &&
            std::find(m_UsedBuffers.begin(), m_UsedBuffers.end(), &resource) == m_UsedBuffers.end())
        {
            // BufferはExecuteCommandListsの境界でCOMMONへdecayします。
            // 同じFrame内では直前に記録したStateから遷移を続けます。
            resource.State = D3D12_RESOURCE_STATE_COMMON;
            m_UsedBuffers.push_back(&resource);
        }
    }

    bool IsValid() const { return m_Context.GetNativeDevice() == m_Device.Get(); }
    bool CanPrepare() const { return IsValid() && m_Context.GetActiveCommandList() == nullptr; }
    ID3D12GraphicsCommandList* GetCommands() const
    {
        return m_ActiveFrame != nullptr ? m_Context.GetComputeCommandList() : nullptr;
    }

    bool PrepareDescriptors(DispatchResources& work)
    {
        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        description.NumDescriptors = static_cast<UINT>(work.Bindings.size());
        description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(m_Device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&work.Heap))))
        {
            return false;
        }
        auto slot = work.Heap->GetCPUDescriptorHandleForHeapStart();
        const UINT stride = m_Device->GetDescriptorHandleIncrementSize(description.Type);
        for (const auto& resource : work.Bindings)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            if (resource->Type == RHIComputeBindingType::StorageBuffer)
            {
                uav.Format = DXGI_FORMAT_R32_TYPELESS;
                uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                uav.Buffer.NumElements = static_cast<UINT>(resource->Size / 4);
                uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            }
            else
            {
                uav.Format = DXGI_FORMAT_R32_FLOAT;
                uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                uav.Texture3D.WSize = resource->Native->GetDesc().DepthOrArraySize;
            }
            m_Device->CreateUnorderedAccessView(resource->Native.Get(), nullptr, &uav, slot);
            slot.ptr += stride;
        }
        return true;
    }

    DX12SceneContext& m_Context;
    ComPtr<ID3D12Device> m_Device;
    RHIComputeCapabilities m_Caps;
    Ref<ComputeOwner> m_Owner = CreateRef<ComputeOwner>();
    RHIExplicitComputeFrame* m_ActiveFrame = nullptr;
    std::vector<Ref<Resource>> m_StorageImages;
    std::vector<Resource*> m_UsedBuffers;
};
} // namespace

RHIComputeCapabilities DX12SceneRHIDevice::QueryComputeCapabilities() const
{
    return Capabilities(m_Context.GetNativeDevice());
}

ID3D12Resource* GetDX12ComputeTexture(const Ref<RHIComputeResource>& resource, ID3D12Device* device)
{
    const auto native = std::dynamic_pointer_cast<Resource>(resource);
    ComPtr<ID3D12Device> owner;
    if (native == nullptr || native->Type != RHIComputeBindingType::StorageImage3D ||
        FAILED(native->Native->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get() != device)
    {
        return nullptr;
    }
    return native->Native.Get();
}

Ref<RHIComputeCommandEncoder> DX12SceneRHIDevice::CreateComputeCommandEncoder()
{
    return QueryComputeCapabilities().Supported ? CreateRef<DX12Encoder>(m_Context) : nullptr;
}
} // namespace Raven
