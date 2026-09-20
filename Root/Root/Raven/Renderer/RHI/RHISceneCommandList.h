#pragma once

#include <cstdint>

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHIGraphicsPipeline.h"
#include "Raven/Renderer/RHI/RHISceneBuffer.h"

namespace Raven
{

// Scene用Graphics CommandListのBackend非依存の記録入口です。
// FrameのAcquire/Submit/Presentとnative CommandBuffer/Encoderの所有は
// RHISceneFrameLifecycleおよび各BackendのContextに委ねます。
// 既存RHICommandListはLegacy OpenGL描画経路として段階的に移行します。
class RHISceneCommandList
{
public:
    virtual ~RHISceneCommandList() = default;

    // Frame開始後・終了前のみ成功します。ViewportとScissorを合わせて設定します。
    virtual bool SetViewport(uint32_t x, uint32_t y,
        uint32_t width, uint32_t height) = 0;

    virtual bool BindPipeline(const Ref<RHIGraphicsPipeline>& pipeline) = 0;

    // indexCount == 0 はIndexBuffer全体を意味します。
    // 異なるBackend/DeviceのResourceやFrame外の記録は失敗として扱います。
    virtual bool DrawIndexed(const RHISceneBuffer& vertexBuffer,
        const RHISceneBuffer& indexBuffer, uint32_t indexCount = 0) = 0;
};

} // namespace Raven
