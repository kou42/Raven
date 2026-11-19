#include "VulkanSceneRHIDevice.h"
#include "Raven/Renderer/RHI/RHIExplicitComputeFrame.h"

#include <algorithm>
#include <cstring>

namespace Raven
{
namespace
{

struct ComputeOwner {};

struct Buffer
{
    Ref<VulkanInstance> Owner;
    VkBuffer Handle = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    ~Buffer()
    {
        const auto device = Owner->GetDevice().GetHandle();
        vkDestroyBuffer(device, Handle, nullptr);
        vkFreeMemory(device, Memory, nullptr);
    }
};

struct Resource final : RHIComputeResource
{
    Ref<VulkanInstance> Instance;
    Ref<ComputeOwner> Owner;
    RHIComputeBindingType Type = RHIComputeBindingType::StorageBuffer;
    Ref<Buffer> Storage;
    std::size_t Size = 0;
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;

    ~Resource() override
    {
        const auto device = Instance->GetDevice().GetHandle();
        vkDestroyImageView(device, View, nullptr);
        vkDestroyImage(device, Image, nullptr);
        vkFreeMemory(device, Memory, nullptr);
    }
};

struct Pipeline final : RHIComputePipeline
{
    Ref<VulkanInstance> Instance;
    Ref<ComputeOwner> Owner;
    VkPipeline Handle = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
    std::vector<RHIComputeBindingLayout> Bindings;

    ~Pipeline() override
    {
        const auto device = Instance->GetDevice().GetHandle();
        vkDestroyPipeline(device, Handle, nullptr);
        vkDestroyPipelineLayout(device, Layout, nullptr);
        vkDestroyDescriptorSetLayout(device, SetLayout, nullptr);
    }
};

struct DispatchResources
{
    Ref<VulkanInstance> Instance;
    Ref<Pipeline> Shader;
    std::vector<Ref<Resource>> Resources;
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkDescriptorSet Set = VK_NULL_HANDLE;

    ~DispatchResources()
    {
        vkDestroyDescriptorPool(Instance->GetDevice().GetHandle(), Pool, nullptr);
    }
};

struct TimestampPool
{
    Ref<VulkanInstance> Instance;
    VkQueryPool Handle = VK_NULL_HANDLE;
    ~TimestampPool()
    {
        vkDestroyQueryPool(Instance->GetDevice().GetHandle(), Handle, nullptr);
    }
};

struct Readback final : RHIComputeReadback
{
    Ref<Buffer> Staging;
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
        const auto device = Staging->Owner->GetDevice().GetHandle();
        if (vkMapMemory(device, Staging->Memory, 0, Size, 0, &mapped) != VK_SUCCESS)
        {
            Status = RHIComputeReadbackStatus::Failed;
            return Status;
        }
        std::memcpy(copy.data(), mapped, Size);
        vkUnmapMemory(device, Staging->Memory);
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

constexpr VkImageUsageFlags VolumeUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
constexpr VkImageSubresourceRange VolumeRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

RHIComputeCapabilities Capabilities(const Ref<VulkanInstance>& instance)
{
    if (instance == nullptr || instance->IsValid() == false)
    {
        return {};
    }
    const auto& device = instance->GetDevice();
    const auto physical = device.GetPhysicalDeviceHandle();
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
    const auto family = device.GetGraphicsQueueFamilyIndex();
    if (family >= count || (queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
    {
        return {};
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    const auto& limits = properties.limits;
    RHIComputeCapabilities caps{};
    caps.Supported = true;
    for (std::size_t i = 0; i < 3; ++i)
    {
        caps.MaxGroups[i] = limits.maxComputeWorkGroupCount[i];
        caps.MaxLocalSize[i] = limits.maxComputeWorkGroupSize[i];
    }
    caps.MaxInvocations = limits.maxComputeWorkGroupInvocations;
    caps.MaxStorageBufferSlots = std::min(8u, limits.maxPerStageDescriptorStorageBuffers);
    caps.MaxStorageBufferBytes = limits.maxStorageBufferRange;
    VkImageFormatProperties format{};
    if (vkGetPhysicalDeviceImageFormatProperties(physical, VK_FORMAT_R32_SFLOAT, VK_IMAGE_TYPE_3D,
        VK_IMAGE_TILING_OPTIMAL, VolumeUsage, 0, &format) == VK_SUCCESS)
    {
        caps.MaxStorageImageSlots = std::min(8u, limits.maxPerStageDescriptorStorageImages);
        caps.MaxStorageTexture3DSize =
            std::min({format.maxExtent.width, format.maxExtent.height, format.maxExtent.depth});
    }
    return caps;
}

VkDeviceMemory Allocate(const VulkanDevice& device, const VkMemoryRequirements& requirements,
    VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(device.GetPhysicalDeviceHandle(), &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
    {
        if ((requirements.memoryTypeBits & (1u << i)) == 0 ||
            (properties.memoryTypes[i].propertyFlags & flags) != flags)
        {
            continue;
        }
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize = requirements.size;
        info.memoryTypeIndex = i;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (vkAllocateMemory(device.GetHandle(), &info, nullptr, &memory) == VK_SUCCESS)
        {
            return memory;
        }
    }
    return VK_NULL_HANDLE;
}

Ref<Buffer> CreateHostBuffer(const Ref<VulkanInstance>& instance, std::size_t size, VkBufferUsageFlags usage)
{
    auto buffer = CreateRef<Buffer>();
    buffer->Owner = instance;
    const auto& device = instance->GetDevice();
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device.GetHandle(), &info, nullptr, &buffer->Handle) != VK_SUCCESS)
    {
        return nullptr;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device.GetHandle(), buffer->Handle, &requirements);
    buffer->Memory = Allocate(device, requirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (buffer->Memory == VK_NULL_HANDLE ||
        vkBindBufferMemory(device.GetHandle(), buffer->Handle, buffer->Memory, 0) != VK_SUCCESS)
    {
        return nullptr;
    }
    return buffer;
}

VkDescriptorType DescriptorType(RHIComputeBindingType type)
{
    return type == RHIComputeBindingType::StorageBuffer
        ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
}

void Transition(VkCommandBuffer commands, Resource& resource, VkImageLayout layout)
{
    if (resource.Layout == layout)
    {
        return;
    }
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = resource.Layout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange = VolumeRange;
    barrier.image = resource.Image;
    VkPipelineStageFlags source = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (resource.Layout == VK_IMAGE_LAYOUT_GENERAL)
    {
        source = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    else if (resource.Layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        source = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    const bool storage = layout == VK_IMAGE_LAYOUT_GENERAL;
    barrier.dstAccessMask = storage
        ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commands, source,
        storage ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    resource.Layout = layout;
}

class VulkanEncoder final : public RHIComputeCommandEncoder,
    public std::enable_shared_from_this<VulkanEncoder>
{
public:
    explicit VulkanEncoder(VulkanSceneContext& context)
        : m_Context(context), m_Instance(context.GetComputeDeviceOwner()), m_Caps(Capabilities(m_Instance))
    {
    }

    bool MeasureGpuWork(const FrameRecorder& work, Ref<RHIComputeTimestamp>& timestamp) override
    {
        timestamp.reset();
        const auto commands = GetCommands();
        if (commands == VK_NULL_HANDLE)
        {
            return work(*this);
        }
        const auto& device = m_Instance->GetDevice();
        const auto physical = device.GetPhysicalDeviceHandle();
        const auto family = device.GetGraphicsQueueFamilyIndex();
        std::uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
        if (family >= count || queues[family].timestampValidBits == 0)
        {
            return work(*this);
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        const auto bits = queues[family].timestampValidBits;
        const auto mask = bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1;
        auto pool = CreateRef<TimestampPool>();
        pool->Instance = m_Instance;
        VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = 2;
        if (vkCreateQueryPool(Device(), &info, nullptr, &pool->Handle) != VK_SUCCESS)
        {
            return work(*this);
        }
        const auto result = CreateRef<RHICompletedComputeTimestamp>();
        const double period = properties.limits.timestampPeriod;
        // Fence 観測後に Query を回収する。WAIT flag は使わない。
        m_ActiveFrame->OnCompletion([pool, result, period, mask](bool completed) noexcept
        {
            std::uint64_t ticks[2]{};
            const bool available = completed && vkGetQueryPoolResults(pool->Instance->GetDevice().GetHandle(),
                pool->Handle, 0, 2, sizeof(ticks), ticks, sizeof(ticks[0]), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS;
            result->Complete(available, static_cast<double>((ticks[1] - ticks[0]) & mask) * period);
        });
        vkCmdResetQueryPool(commands, pool->Handle, 0, 2);
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool->Handle, 0);
        const bool success = work(*this);
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool->Handle, 1);
        if (success)
        {
            timestamp = result;
        }
        return success;
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
                return recorded;
            });
        return m_Context.QueueComputeWork(frame);
    }

    Ref<RHIComputePipeline> CreatePipeline(const RHIComputePipelineSpecification& spec) override
    {
        if (CanPrepare() == false || spec.VulkanSpirv.empty() || spec.VulkanEntryPoint.empty() ||
            Fits(spec.LocalSize, m_Caps.MaxLocalSize) == false ||
            std::uint64_t(spec.LocalSize[0]) * spec.LocalSize[1] * spec.LocalSize[2] > m_Caps.MaxInvocations)
        {
            return nullptr;
        }
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (const auto& binding : spec.Layout)
        {
            const auto limit = binding.Type == RHIComputeBindingType::StorageBuffer
                ? m_Caps.MaxStorageBufferSlots : m_Caps.MaxStorageImageSlots;
            if (binding.Slot >= limit || std::any_of(bindings.begin(), bindings.end(), [&](const auto& entry)
                { return entry.binding == binding.Slot; }))
            {
                return nullptr;
            }
            bindings.push_back({binding.Slot, DescriptorType(binding.Type), 1,
                VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        }
        auto pipeline = CreateRef<Pipeline>();
        pipeline->Instance = m_Instance;
        pipeline->Owner = m_Owner;
        pipeline->Bindings = spec.Layout;
        VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        setInfo.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(Device(), &setInfo, nullptr, &pipeline->SetLayout) != VK_SUCCESS)
        {
            return nullptr;
        }
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &pipeline->SetLayout;
        if (vkCreatePipelineLayout(Device(), &layout, nullptr, &pipeline->Layout) != VK_SUCCESS)
        {
            return nullptr;
        }
        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = spec.VulkanSpirv.size() * sizeof(uint32_t);
        moduleInfo.pCode = spec.VulkanSpirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(Device(), &moduleInfo, nullptr, &module) != VK_SUCCESS)
        {
            return nullptr;
        }
        VkComputePipelineCreateInfo native{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        native.layout = pipeline->Layout;
        native.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        native.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        native.stage.module = module;
        native.stage.pName = spec.VulkanEntryPoint.c_str();
        const auto result = vkCreateComputePipelines(Device(), VK_NULL_HANDLE, 1, &native, nullptr, &pipeline->Handle);
        vkDestroyShaderModule(Device(), module, nullptr);
        if (result != VK_SUCCESS)
        {
            return nullptr;
        }
        ++m_Statistics.Pipelines;
        return pipeline;
    }

    Ref<RHIComputeResource> CreateStorageBuffer(std::size_t size, const void* initialData) override
    {
        if (CanPrepare() == false || size == 0 || size > m_Caps.MaxStorageBufferBytes)
        {
            return nullptr;
        }
        auto resource = CreateRef<Resource>();
        resource->Instance = m_Instance;
        resource->Owner = m_Owner;
        resource->Size = size;
        resource->Storage = CreateHostBuffer(m_Instance, size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        if (resource->Storage == nullptr)
        {
            return nullptr;
        }
        if (initialData != nullptr)
        {
            void* mapped = nullptr;
            if (vkMapMemory(Device(), resource->Storage->Memory, 0, size, 0, &mapped) != VK_SUCCESS)
            {
                return nullptr;
            }
            std::memcpy(mapped, initialData, size);
            vkUnmapMemory(Device(), resource->Storage->Memory);
        }
        ++m_Statistics.Buffers;
        m_Statistics.BufferBytes += size;
        return resource;
    }

    Ref<RHIComputeResource> CreateStorageImage3D(const RHIComputeExtent& size) override
    {
        const auto limit = m_Caps.MaxStorageTexture3DSize;
        if (CanPrepare() == false || Fits(size, {limit, limit, limit}) == false)
        {
            return nullptr;
        }
        auto resource = CreateRef<Resource>();
        resource->Instance = m_Instance;
        resource->Owner = m_Owner;
        resource->Type = RHIComputeBindingType::StorageImage3D;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_3D;
        info.format = VK_FORMAT_R32_SFLOAT;
        info.extent = {size[0], size[1], size[2]};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VolumeUsage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(Device(), &info, nullptr, &resource->Image) != VK_SUCCESS)
        {
            return nullptr;
        }
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(Device(), resource->Image, &requirements);
        resource->Memory = Allocate(m_Instance->GetDevice(), requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (resource->Memory == VK_NULL_HANDLE ||
            vkBindImageMemory(Device(), resource->Image, resource->Memory, 0) != VK_SUCCESS)
        {
            return nullptr;
        }
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = resource->Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_3D;
        view.format = info.format;
        view.subresourceRange = VolumeRange;
        if (vkCreateImageView(Device(), &view, nullptr, &resource->View) != VK_SUCCESS)
        {
            return nullptr;
        }
        ++m_Statistics.Images;
        m_Statistics.ImageBytes += static_cast<std::uint64_t>(size[0]) * size[1] * size[2] * sizeof(float);
        return resource;
    }

    bool Dispatch(const Ref<RHIComputePipeline>& pipeline, std::span<const RHIComputeBinding> bindings,
        const RHIComputeExtent& groups) override
    {
        const auto commands = GetCommands();
        const auto native = std::dynamic_pointer_cast<Pipeline>(pipeline);
        if (commands == VK_NULL_HANDLE || native == nullptr || native->Owner != m_Owner ||
            bindings.size() != native->Bindings.size() || Fits(groups, m_Caps.MaxGroups) == false)
        {
            return false;
        }
        auto work = CreateRef<DispatchResources>();
        work->Instance = m_Instance;
        work->Shader = native;
        for (const auto& layout : native->Bindings)
        {
            const auto binding = std::find_if(bindings.begin(), bindings.end(),
                [&](const auto& entry) { return entry.Slot == layout.Slot; });
            if (binding == bindings.end())
            {
                return false;
            }
            auto resource = std::dynamic_pointer_cast<Resource>(binding->Resource);
            if (resource == nullptr || resource->Owner != m_Owner || resource->Type != layout.Type ||
                std::find(work->Resources.begin(), work->Resources.end(), resource) != work->Resources.end())
            {
                return false;
            }
            work->Resources.push_back(std::move(resource));
        }
        if (work->Resources.empty() == false)
        {
            const auto cached = m_LastDescriptors.lock();
            if (cached != nullptr && cached->Shader == native && cached->Resources == work->Resources)
            {
                // Set を更新せず、同一 Pipeline と Resource の組だけを共有する。
                work = cached;
                ++m_Statistics.DescriptorReuses;
            }
            else
            {
                if (PrepareDescriptors(*work) == false)
                {
                    return false;
                }
                m_LastDescriptors = work;
                ++m_Statistics.DescriptorAllocations;
            }
        }
        m_ActiveFrame->Retain(work);
        for (const auto& resource : work->Resources)
        {
            if (resource->Type == RHIComputeBindingType::StorageImage3D)
            {
                Transition(commands, *resource, VK_IMAGE_LAYOUT_GENERAL);
                if (std::find(m_StorageImages.begin(), m_StorageImages.end(), resource) == m_StorageImages.end())
                {
                    m_StorageImages.push_back(resource);
                }
            }
        }
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, native->Handle);
        if (work->Set != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, native->Layout,
                0, 1, &work->Set, 0, nullptr);
        }
        vkCmdDispatch(commands, groups[0], groups[1], groups[2]);
        ++m_Statistics.Dispatches;
        return true;
    }

    bool Barrier(RHIComputeConsumer consumer) override
    {
        const auto commands = GetCommands();
        if (commands == VK_NULL_HANDLE)
        {
            return false;
        }
        switch (consumer)
        {
        case RHIComputeConsumer::Storage:
        {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = barrier.srcAccessMask;
            vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            return true;
        }
        case RHIComputeConsumer::TextureSampling:
            for (const auto& resource : m_StorageImages)
            {
                Transition(commands, *resource, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            m_StorageImages.clear();
            return true;
        case RHIComputeConsumer::BufferReadback:
            return true;
        }
        return false;
    }

    Ref<RHIComputeReadback> RequestBufferReadback(const Ref<RHIComputeResource>& resource,
        std::size_t offset, std::size_t size) override
    {
        const auto commands = GetCommands();
        const auto source = std::dynamic_pointer_cast<Resource>(resource);
        if (commands == VK_NULL_HANDLE || source == nullptr || source->Owner != m_Owner ||
            source->Type != RHIComputeBindingType::StorageBuffer ||
            size == 0 || offset > source->Size || size > source->Size - offset)
        {
            return nullptr;
        }
        auto ticket = CreateRef<Readback>();
        ticket->Size = size;
        ticket->Staging = CreateHostBuffer(m_Instance, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        if (ticket->Staging == nullptr)
        {
            return nullptr;
        }
        m_ActiveFrame->Retain(source);
        m_ActiveFrame->OnCompletion([ticket](bool completed)
        {
            ticket->Status = completed ? RHIComputeReadbackStatus::Ready : RHIComputeReadbackStatus::Failed;
        });
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        const VkBufferCopy region{offset, 0, size};
        vkCmdCopyBuffer(commands, source->Storage->Handle, ticket->Staging->Handle, 1, &region);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        // 次のFrameで同じStorage Bufferへ書けるのは、この転送が読み終わった後です。
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        ++m_Statistics.Readbacks;
        m_Statistics.ReadbackBytes += size;
        return ticket;
    }

private:
    bool IsValid() const { return m_Context.GetComputeDeviceOwner() == m_Instance; }
    bool CanPrepare() const { return IsValid() && m_Context.GetActiveCommandBuffer() == VK_NULL_HANDLE; }
    VkDevice Device() const { return m_Instance->GetDevice().GetHandle(); }
    VkCommandBuffer GetCommands() const
    {
        return m_ActiveFrame != nullptr ? m_Context.GetComputeCommandBuffer() : VK_NULL_HANDLE;
    }

    bool PrepareDescriptors(DispatchResources& work)
    {
        uint32_t buffers = 0;
        for (const auto& resource : work.Resources)
        {
            buffers += resource->Type == RHIComputeBindingType::StorageBuffer;
        }
        const auto images = static_cast<uint32_t>(work.Resources.size()) - buffers;
        std::vector<VkDescriptorPoolSize> sizes;
        if (buffers != 0)
        {
            sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffers});
        }
        if (images != 0)
        {
            sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, images});
        }
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
        pool.pPoolSizes = sizes.data();
        if (vkCreateDescriptorPool(Device(), &pool, nullptr, &work.Pool) != VK_SUCCESS)
        {
            return false;
        }
        VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        set.descriptorPool = work.Pool;
        set.descriptorSetCount = 1;
        set.pSetLayouts = &work.Shader->SetLayout;
        if (vkAllocateDescriptorSets(Device(), &set, &work.Set) != VK_SUCCESS)
        {
            return false;
        }
        for (std::size_t i = 0; i < work.Resources.size(); ++i)
        {
            const auto& resource = work.Resources[i];
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = work.Set;
            write.dstBinding = work.Shader->Bindings[i].Slot;
            write.descriptorCount = 1;
            write.descriptorType = DescriptorType(resource->Type);
            VkDescriptorBufferInfo buffer{};
            VkDescriptorImageInfo image{};
            if (resource->Type == RHIComputeBindingType::StorageBuffer)
            {
                buffer = {resource->Storage->Handle, 0, resource->Size};
                write.pBufferInfo = &buffer;
            }
            else
            {
                image.imageView = resource->View;
                image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                write.pImageInfo = &image;
            }
            vkUpdateDescriptorSets(Device(), 1, &write, 0, nullptr);
        }
        return true;
    }

    std::weak_ptr<DispatchResources> m_LastDescriptors;
    VulkanSceneContext& m_Context;
    Ref<VulkanInstance> m_Instance;
    RHIComputeCapabilities m_Caps;
    Ref<ComputeOwner> m_Owner = CreateRef<ComputeOwner>();
    RHIExplicitComputeFrame* m_ActiveFrame = nullptr;
    std::vector<Ref<Resource>> m_StorageImages;
};
} // namespace

RHIComputeCapabilities VulkanSceneRHIDevice::QueryComputeCapabilities() const
{
    return Capabilities(m_Context.GetComputeDeviceOwner());
}

VkImageView GetVulkanComputeTexture(const Ref<RHIComputeResource>& resource, VkDevice device)
{
    const auto native = std::dynamic_pointer_cast<Resource>(resource);
    if (native == nullptr || native->Type != RHIComputeBindingType::StorageImage3D ||
        native->Instance->GetDevice().GetHandle() != device)
    {
        return VK_NULL_HANDLE;
    }
    return native->View;
}

Ref<RHIComputeCommandEncoder> VulkanSceneRHIDevice::CreateComputeCommandEncoder()
{
    return QueryComputeCapabilities().Supported ? CreateRef<VulkanEncoder>(m_Context) : nullptr;
}
} // namespace Raven
