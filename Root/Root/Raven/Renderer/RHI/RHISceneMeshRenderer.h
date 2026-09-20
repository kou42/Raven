#pragma once

#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Raven
{

// Sceneの描画データをAPI非依存の値として受け取ります。
// Buffer/Textureの所有権はRefで保持し、RendererはFrameを開始・終了しません。
struct RHISceneDrawItem
{
    Ref<RHIBuffer> VertexBuffer;
    Ref<RHIBuffer> IndexBuffer;
    uint32_t IndexCount = 0;
    RHIMaterialProperties Material;
    std::array<float, 16> ClipTransform{};
    // View空間Z。右手系で-Zを見るCameraでは小さい値ほど遠方です。
    float ViewDepth = 0.0f;
};

class RHISceneMeshRenderer final
{
public:
    // Opaqueを登録順、Transparentを遠方から描画します。
    // Maskedはalpha cutoff対応Shaderを導入するまで明示的に拒否します。
    // 失敗時は呼び出し側がFrameを破棄・終了する責務を持ちます。
    static bool Draw(RHISceneCommandList& commands,
        const Ref<RHIGraphicsPipeline>& opaquePipeline,
        const Ref<RHIGraphicsPipeline>& transparentPipeline,
        const std::vector<RHISceneDrawItem>& items)
    {
        if (opaquePipeline == nullptr || transparentPipeline == nullptr)
        {
            return false;
        }
        std::vector<std::size_t> transparentIndices;
        transparentIndices.reserve(items.size());
        // GPU命令を記録する前に不正な描画入力を検出します。
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            const RHISceneDrawItem& item = items[index];
            if (item.VertexBuffer == nullptr || item.IndexBuffer == nullptr ||
                item.Material.Texture == nullptr ||
                item.Material.SurfaceType == MaterialSurfaceType::Masked)
            {
                return false;
            }
            if (item.Material.SurfaceType == MaterialSurfaceType::Transparent)
            {
                transparentIndices.push_back(index);
            }
        }
        std::stable_sort(transparentIndices.begin(), transparentIndices.end(),
            [&items](std::size_t left, std::size_t right)
            {
                return items[left].ViewDepth < items[right].ViewDepth;
            });

        for (uint32_t pass = 0; pass < 2; ++pass)
        {
            const bool transparentPass = pass == 1;
            const Ref<RHIGraphicsPipeline>& pipeline = transparentPass == true ?
                transparentPipeline : opaquePipeline;
            if (commands.BindPipeline(pipeline) == false)
            {
                return false;
            }
            const std::size_t count = transparentPass == true ?
                transparentIndices.size() : items.size();
            for (std::size_t draw = 0; draw < count; ++draw)
            {
                const RHISceneDrawItem& item = transparentPass == true ?
                    items[transparentIndices[draw]] : items[draw];
                if ((item.Material.SurfaceType == MaterialSurfaceType::Transparent) != transparentPass)
                {
                    continue;
                }
                if (commands.BindMaterial(item.Material) == false ||
                    commands.SetClipTransform(item.ClipTransform) == false ||
                    commands.DrawIndexed(item.VertexBuffer, item.IndexBuffer,
                        item.IndexCount) == false)
                {
                    return false;
                }
            }
        }
        return true;
    }
};

} // namespace Raven
