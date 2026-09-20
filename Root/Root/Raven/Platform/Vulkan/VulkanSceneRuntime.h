#pragma once

#include "Raven/Renderer/RHI/RHISceneRuntime.h"
#include "VulkanSceneRenderServices.h"

namespace Raven
{

// Scene Contextを所有し、CommandList/Factoryは同じContextを借用します。
// 宣言順によりServicesをContextより先に破棄します。
class VulkanSceneRuntime final : public RHISceneRuntime
{
public:
    VulkanSceneRuntime()
        : m_Services(m_Context)
    {
    }

    ~VulkanSceneRuntime() override
    {
        Shutdown();
    }

    VulkanSceneRuntime(const VulkanSceneRuntime&) = delete;
    VulkanSceneRuntime& operator=(const VulkanSceneRuntime&) = delete;

    // 未対応Backendへ暗黙fallbackせず、初期化失敗時はnullptrを返します。
    static Scope<RHISceneRuntime> Create(Window& window)
    {
        if (window.GetBackend() != RHIBackend::Vulkan)
        {
            return nullptr;
        }

        auto runtime = CreateScope<VulkanSceneRuntime>();
        if (runtime->m_Context.Init(window) == false)
        {
            return nullptr;
        }
        return runtime;
    }

    RHIBackend GetBackend() const override
    {
        return RHIBackend::Vulkan;
    }

    RHISceneRenderServices& GetServices() override
    {
        return m_Services;
    }

    uint32_t GetWidth() const override
    {
        return m_Context.GetExtent().width;
    }

    uint32_t GetHeight() const override
    {
        return m_Context.GetExtent().height;
    }

    bool WaitIdle() override
    {
        if (m_Context.GetDevice().IsValid() == false)
        {
            return false;
        }
        return m_Context.GetDevice().WaitIdle();
    }

    void Shutdown() override
    {
        // Resource所有者がWaitIdle後にBufferを破棄してから呼ぶこと。
        // Context側でもPipelineとFrame Resourceの解放前にGPU完了を待ちます。
        m_Context.Shutdown();
    }

private:
    VulkanSceneContext m_Context;
    VulkanSceneRenderServices m_Services;
};

} // namespace Raven
