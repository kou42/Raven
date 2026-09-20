#pragma once

#include "Raven/Renderer/RHI/RHISceneRenderServices.h"
#include "VulkanSceneCommandList.h"
#include "VulkanSceneResourceFactory.h"

namespace Raven
{

// 既存VulkanSceneContextのFrame/Device/RenderTargetを共有するScene描画入口です。
// Contextを借用し、CommandListとFactoryだけを保持します。
// Contextより先に破棄し、ContextのInit/Shutdownは呼び出し側で管理します。
class VulkanSceneRenderServices final : public RHISceneRenderServices
{
public:
    explicit VulkanSceneRenderServices(VulkanSceneContext& context)
        : m_Context(context),
          m_CommandList(context),
          m_ResourceFactory(context)
    {
    }

    VulkanSceneRenderServices(const VulkanSceneRenderServices&) = delete;
    VulkanSceneRenderServices& operator=(const VulkanSceneRenderServices&) = delete;

    RHISceneFrameLifecycle& GetFrameLifecycle() override
    {
        return m_Context;
    }

    RHISceneCommandList& GetCommandList() override
    {
        return m_CommandList;
    }

    RHISceneResourceFactory& GetResourceFactory() override
    {
        return m_ResourceFactory;
    }

private:
    VulkanSceneContext& m_Context;
    VulkanSceneCommandList m_CommandList;
    VulkanSceneResourceFactory m_ResourceFactory;
};

} // namespace Raven
