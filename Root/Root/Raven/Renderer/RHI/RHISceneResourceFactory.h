#pragma once

#include <cstdint>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHIGraphicsPipeline.h"
#include "Raven/Renderer/RHI/RHISceneBuffer.h"

namespace Raven
{

// Scene用Resourceの生成入口。既存RHIDeviceのLegacy OpenGL Resource生成とは分離します。
// GPU Device/RenderTargetの所有権はBackendのScene Contextに残します。
// 戻り値のBufferはContextのShutdown前、GPUの使用完了後に破棄してください。
class RHISceneResourceFactory
{
public:
    virtual ~RHISceneResourceFactory() = default;

    virtual Scope<RHISceneBuffer> CreateVertexBuffer(
        const void* data, uint32_t byteSize, uint32_t stride) = 0;

    // 現行Scene Drawは32bit Indexを使用します。
    virtual Scope<RHISceneBuffer> CreateIndexBuffer(
        const uint32_t* indices, uint32_t count) = 0;

    // RenderTargetと一致するColorFormatをBackend側で設定します。
    // Resize後はPipelineを再生成してください。
    virtual Ref<RHIGraphicsPipeline> CreateGraphicsPipeline(
        const RHIGraphicsPipelineSpecification& specification) = 0;
};

} // namespace Raven
