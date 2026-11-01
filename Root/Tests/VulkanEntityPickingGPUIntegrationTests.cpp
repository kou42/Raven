// Display/SwapChainを作らず、実Vulkan DriverでPicking AttachmentとReadbackを検証します。
// 通常アプリとは別のmainです。ビルド手順はRoot/Tests/README.mdを参照してください。
#include "Raven/Platform/Vulkan/VulkanDevice.h"
#include "Raven/Platform/Vulkan/VulkanGraphicsPipeline.h"
#include "Raven/Platform/Vulkan/VulkanOffscreenRenderTarget.h"
#include "Raven/Platform/Vulkan/VulkanPhysicalDevice.h"
#include "Raven/Platform/Vulkan/VulkanSceneBuffer.h"
#include "Raven/Platform/Vulkan/VulkanSceneRenderTarget.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
uint32_t ValidationErrors = 0u;

void Check(bool condition, const char* message)
{
    if (condition == false)
    {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL OnValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0u)
    {
        ++ValidationErrors;
        std::cerr << "[VALIDATION] " << data->pMessage << '\n';
    }
    return VK_FALSE;
}

Raven::RHIShaderBinary LoadShader(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    Check(stream.is_open(), "open SPIR-V shader");
    Raven::RHIShaderBinary shader;
    shader.Format = Raven::RHIShaderBinaryFormat::SPIRV;
    shader.EntryPoint = "main";
    shader.Code.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return shader;
}

VkRenderPass CreateColorPass(VkDevice device)
{
    // Offscreen Targetの入力となる通常Color/Depth Pass。Picking Pass自身は製品コードを使用します。
    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1] = attachments[0];
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    const VkAttachmentReference color = { 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    const VkAttachmentReference depth = { 1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1u;
    subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0u;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1u;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1u;
    info.pDependencies = &dependency;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    Check(vkCreateRenderPass(device, &info, nullptr, &renderPass) == VK_SUCCESS, "create color render pass");
    return renderPass;
}
} // namespace

int main()
{
    uint32_t layerCount = 0u;
    Check(vkEnumerateInstanceLayerProperties(&layerCount, nullptr) == VK_SUCCESS, "enumerate Vulkan layers");
    std::vector<VkLayerProperties> layers(layerCount);
    Check(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()) == VK_SUCCESS, "read Vulkan layers");
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    bool hasValidation = false;
    for (const auto& layer : layers)
    {
        hasValidation = hasValidation || std::strcmp(layer.layerName, validationLayer) == 0;
    }
    const char* requireValidation = std::getenv("RAVEN_VULKAN_REQUIRE_VALIDATION");
    Check(requireValidation == nullptr || std::strcmp(requireValidation, "1") != 0 || hasValidation,
        "required Vulkan validation layer is available");
    VkDebugUtilsMessengerCreateInfoEXT debug{};
    debug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = OnValidation;
    const char* extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    // VulkanDeviceがVK_KHR_swapchainを有効化するため、親extensionも有効にします。
    instanceInfo.enabledExtensionCount = 1u;
    instanceInfo.ppEnabledExtensionNames = extensions;
    if (hasValidation == true)
    {
        instanceInfo.enabledLayerCount = 1u;
        instanceInfo.ppEnabledLayerNames = &validationLayer;
        instanceInfo.enabledExtensionCount = 2u;
        instanceInfo.pNext = &debug;
    }
    VkInstance instance = VK_NULL_HANDLE;
    Check(vkCreateInstance(&instanceInfo, nullptr, &instance) == VK_SUCCESS, "create headless Vulkan instance");
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    if (hasValidation == true)
    {
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        Check(createMessenger != nullptr && createMessenger(instance, &debug, nullptr, &messenger) == VK_SUCCESS,
            "enable validation diagnostics");
    }
    Raven::VulkanPhysicalDevice physicalDevices;
    Check(physicalDevices.Enumerate(instance), "enumerate graphics device");
    Raven::VulkanDevice device;
    Check(physicalDevices.FindFirstGraphicsDevice() != nullptr &&
        device.Init(*physicalDevices.FindFirstGraphicsDevice()), "create logical device");
    std::cout << "Vulkan device: " << physicalDevices.FindFirstGraphicsDevice()->Properties.deviceName << '\n';
    const VkDevice native = device.GetHandle();
    const VkRenderPass colorPass = CreateColorPass(native);
    const VkRenderPass pickingPass = Raven::VulkanSceneRenderTarget::CreateEntityPickingRenderPass(native);
    Check(pickingPass != VK_NULL_HANDLE, "create production picking render pass");
    Raven::RHIOffscreenRenderTargetSpecification specification;
    specification.Width = 8u;
    specification.Height = 8u;
    Raven::VulkanOffscreenRenderTarget gameTarget;
    Check(gameTarget.Init(device, colorPass, VK_FORMAT_R8G8B8A8_UNORM, specification, VK_NULL_HANDLE, 0u),
        "create Game View target");
    uint64_t sequence = 0u;
    Check(gameTarget.SupportsEntityPicking() == false &&
        gameTarget.RequestEntityPicking(0u, 0u, sequence) == false, "Game View excludes picking resources");
    gameTarget.Shutdown();
    specification.EnableEntityPicking = true;
    Raven::VulkanOffscreenRenderTarget target;
    Check(target.Init(device, colorPass, VK_FORMAT_R8G8B8A8_UNORM, specification, VK_NULL_HANDLE, 0u) == false,
        "reject picking target without picking pass");
    Check(target.IsValid() == false, "failed target initialization releases partial resources");
    Check(target.Init(device, colorPass, VK_FORMAT_R8G8B8A8_UNORM, specification, pickingPass, 2u),
        "create Scene View target");
    Check(target.RequestEntityPicking(8u, 0u, sequence) == false, "reject out of bounds pixel");

    Raven::RHIGraphicsPipelineSpecification pipelineInfo;
    pipelineInfo.VertexShader = LoadShader("Raven/Assets/Shaders/Vulkan/SceneTriangle.vert.spv");
    pipelineInfo.FragmentShader = LoadShader("Raven/Assets/Shaders/Vulkan/EntityPicking.frag.spv");
    pipelineInfo.ColorFormat = Raven::RHIColorFormat::R32Sint;
    pipelineInfo.DepthFormat = Raven::RHIDepthFormat::D32Float;
    pipelineInfo.DepthTest = true;
    pipelineInfo.DepthWrite = true;
    pipelineInfo.DepthCompare = Raven::DepthCompareOperator::LessEqual;
    pipelineInfo.Cull = Raven::CullMode::None;
    pipelineInfo.VertexBindings = { {0u, 11u * sizeof(float)} };
    pipelineInfo.VertexAttributes = {
        {0u, 0u, Raven::ShaderDataType::Float3, 0u},
        {1u, 0u, Raven::ShaderDataType::Float3, 3u * sizeof(float)},
        {2u, 0u, Raven::ShaderDataType::Float2, 6u * sizeof(float)}
    };
    Raven::VulkanGraphicsPipeline pipeline;
    Check(pipeline.Init(native, pickingPass, VK_FORMAT_R32_SINT, pipelineInfo), "create integer picking pipeline");
    const float vertices[33] = {
        -1.0f,-1.0f,0.5f, 0,0,0, 0,0, 0,0,0,
         3.0f,-1.0f,0.5f, 0,0,0, 0,0, 0,0,0,
        -1.0f, 3.0f,0.5f, 0,0,0, 0,0, 0,0,0
    };
    Raven::VulkanSceneBuffer vertexBuffer;
    Check(vertexBuffer.InitVertex(device, vertices, sizeof(vertices), 11u * sizeof(float)), "create triangle vertices");
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = device.GetGraphicsQueueFamilyIndex();
    VkCommandPool pool = VK_NULL_HANDLE;
    Check(vkCreateCommandPool(native, &poolInfo, nullptr, &pool) == VK_SUCCESS, "create test command pool");
    std::array<VkCommandBuffer, 2> commands{};
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = static_cast<uint32_t>(commands.size());
    Check(vkAllocateCommandBuffers(native, &allocate, commands.data()) == VK_SUCCESS, "allocate frame slots");
    std::array<VkFence, 2> fences{};
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    for (auto& fence : fences)
    {
        Check(vkCreateFence(native, &fenceInfo, nullptr, &fence) == VK_SUCCESS, "create frame fence");
    }
    const auto submit = [&](uint32_t slot, uint32_t x, uint32_t y, bool drawGeometry, bool overlap)
    {
        Check(vkResetCommandBuffer(commands[slot], 0u) == VK_SUCCESS, "reset slot command buffer");
        Check(target.RequestEntityPicking(x, y, sequence), "request pixel");
        Raven::RHIEntityPickingRequest request{};
        Check(target.TakePendingEntityPickingRequest(request), "take asynchronous request");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        Check(vkBeginCommandBuffer(commands[slot], &begin) == VK_SUCCESS, "begin command buffer");
        Check(target.Begin(commands[slot], specification.ClearColor.data()), "begin color pass");
        // 非対称なTopLeft pixelだけを近いDepthで塞ぎ、Y二重反転とDepth再利用を検出します。
        VkClearAttachment occluder{};
        occluder.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        occluder.clearValue.depthStencil = { 0.4f, 0u };
        VkClearRect rectangle{};
        rectangle.rect = { {2, 2}, {1u, 1u} };
        rectangle.layerCount = 1u;
        vkCmdClearAttachments(commands[slot], 1u, &occluder, 1u, &rectangle);
        Check(target.BeginEntityPickingPass(commands[slot], request), "begin production picking pass");
        VkViewport viewport = {0.0f, 0.0f, 8.0f, 8.0f, 0.0f, 1.0f};
        VkRect2D scissor = {{0, 0}, {8u, 8u}};
        vkCmdSetViewport(commands[slot], 0u, 1u, &viewport);
        vkCmdSetScissor(commands[slot], 0u, 1u, &scissor);
        if (drawGeometry == true)
        {
            vkCmdBindPipeline(commands[slot], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());
            const VkBuffer vertex = vertexBuffer.GetHandle();
            const VkDeviceSize offset = 0u;
            vkCmdBindVertexBuffers(commands[slot], 0u, 1u, &vertex, &offset);
            std::array<float, 16> transform = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            const auto draw = [&](int32_t id)
            {
                const auto stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                vkCmdPushConstants(commands[slot], pipeline.GetLayout(), stages, 0u, sizeof(transform), transform.data());
                vkCmdPushConstants(commands[slot], pipeline.GetLayout(), stages, 64u, sizeof(id), &id);
                vkCmdDraw(commands[slot], 3u, 1u, 0u, 0u);
            };
            draw(42);
            if (overlap == true)
            {
                transform[14] = -0.25f;
                draw(77);
            }
        }
        Check(target.EndEntityPickingPass(commands[slot], slot, request), "copy requested pixel to slot readback");
        Check(target.End(commands[slot]), "restore color sampling layout");
        Check(vkEndCommandBuffer(commands[slot]) == VK_SUCCESS, "end command buffer");
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1u;
        submitInfo.pCommandBuffers = &commands[slot];
        Check(vkQueueSubmit(device.GetGraphicsQueue(), 1u, &submitInfo, fences[slot]) == VK_SUCCESS, "submit slot");
        return request.Sequence;
    };
    const auto complete = [&](uint32_t slot)
    {
        Check(vkWaitForFences(native, 1u, &fences[slot], VK_TRUE, UINT64_MAX) == VK_SUCCESS, "wait test slot fence");
        Check(target.ResolveEntityPickingReadback(slot), "resolve only after fence");
        Check(vkResetFences(native, 1u, &fences[slot]) == VK_SUCCESS, "reset slot fence");
    };
    const auto expect = [&](uint64_t expectedSequence, int32_t expectedID)
    {
        uint64_t actualSequence = 0u;
        int32_t actualID = 0;
        Check(target.ConsumeEntityPickingResult(actualSequence, actualID), "consume result");
        Check(actualSequence == expectedSequence && actualID == expectedID, "sequence and entity match");
    };
    uint64_t token = submit(0u, 4u, 4u, false, false);
    int32_t id = 0;
    Check(target.ConsumeEntityPickingResult(sequence, id) == false, "no result before fence publication");
    complete(0u);
    expect(token, -1);
    token = submit(0u, 2u, 2u, true, false);
    complete(0u);
    expect(token, -1);
    token = submit(0u, 2u, 5u, true, false);
    complete(0u);
    expect(token, 42);
    token = submit(0u, 4u, 4u, true, true);
    complete(0u);
    expect(token, 77);
    submit(0u, 4u, 4u, false, false);
    token = submit(1u, 4u, 4u, true, true);
    complete(0u);
    Check(target.ConsumeEntityPickingResult(sequence, id) == false, "old in-flight click discarded");
    complete(1u);
    expect(token, 77);
    token = submit(0u, 4u, 4u, true, false);
    complete(0u);
    // ResizeのTarget交換で完了済みでも未消費の結果を持ち越しません。
    target.Shutdown();
    specification.Width = 16u;
    specification.Height = 12u;
    Check(target.Init(device, colorPass, VK_FORMAT_R8G8B8A8_UNORM, specification, pickingPass, 2u), "recreate resized target");
    Check(target.ConsumeEntityPickingResult(sequence, id) == false, "resize resets result mailbox");
    target.Shutdown();
    pipeline.Shutdown();
    vertexBuffer.Shutdown();
    for (VkFence fence : fences)
    {
        vkDestroyFence(native, fence, nullptr);
    }
    vkDestroyCommandPool(native, pool, nullptr);
    vkDestroyRenderPass(native, pickingPass, nullptr);
    vkDestroyRenderPass(native, colorPass, nullptr);
    device.Shutdown();
    if (messenger != VK_NULL_HANDLE)
    {
        const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        destroyMessenger(instance, messenger, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
    Check(ValidationErrors == 0u, "Vulkan validation error count is zero");
    std::cout << "[PASS] Vulkan entity picking GPU integration (validation "
        << (hasValidation == true ? "enabled" : "unavailable") << ")\n";
    return 0;
}
