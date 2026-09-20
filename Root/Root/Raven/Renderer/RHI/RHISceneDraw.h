#pragma once

#include <cstdint>

#include "Raven/Renderer/RHI/RHISceneCommandList.h"

namespace Raven
{

// 1 Drawの入力をBackend非依存に束ねます。
// Bufferの所有権はScene/Backend側に残し、Draw中の生存を呼び出し側が保証します。
struct RHISceneDrawItem
{
    RHISceneDrawItem(const Ref<RHIGraphicsPipeline>& pipeline,
        const RHISceneBuffer& vertexBuffer, const RHISceneBuffer& indexBuffer,
        uint32_t indexCount = 0)
        : Pipeline(pipeline), VertexBuffer(vertexBuffer),
          IndexBuffer(indexBuffer), IndexCount(indexCount)
    {
    }

    Ref<RHIGraphicsPipeline> Pipeline;
    const RHISceneBuffer& VertexBuffer;
    const RHISceneBuffer& IndexBuffer;
    uint32_t IndexCount = 0;
};

// Sceneの描画入口です。Frame境界とnative描画命令を所有しません。
// Vulkan/DX12/将来のMetalは各RHISceneCommandListでResource互換性を検証します。
class RHISceneDraw final
{
public:
    static bool Draw(RHISceneCommandList& commands, const RHISceneDrawItem& item)
    {
        if (item.Pipeline == nullptr ||
            item.VertexBuffer.IsValid() == false ||
            item.IndexBuffer.IsValid() == false ||
            item.VertexBuffer.IsIndexBuffer() == true ||
            item.IndexBuffer.IsIndexBuffer() == false ||
            item.VertexBuffer.GetVertexStride() == 0)
        {
            return false;
        }

        const uint32_t availableIndices = item.IndexBuffer.GetIndexCount();
        if (availableIndices == 0 ||
            (item.IndexCount != 0 && item.IndexCount > availableIndices))
        {
            return false;
        }

        // Pipelineが失敗した場合にDrawIndexedを発行しません。
        // Backend固有のDevice/RenderTarget互換性はBind/Drawの内部で検証します。
        if (commands.BindPipeline(item.Pipeline) == false)
        {
            return false;
        }

        return commands.DrawIndexed(
            item.VertexBuffer, item.IndexBuffer, item.IndexCount);
    }
};

} // namespace Raven
