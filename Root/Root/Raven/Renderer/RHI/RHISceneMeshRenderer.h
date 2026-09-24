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
    // Frameの開始・終了・Presentを共通Lifecycle経由で実行します。
    // BeginFrameのResizeRequiredは描画せず返し、呼び出し元がResizeを判断します。
    // 描画中の失敗時はFrameが未完了のため、呼び出し元がContextをShutdownします。
    static RHIFrameResult DrawFrame(RHISceneFrameLifecycle& frame,
        RHISceneCommandList& commands,
        const Ref<RHIGraphicsPipeline>& opaquePipeline,
        const Ref<RHIGraphicsPipeline>& transparentPipeline,
        const std::vector<RHISceneDrawItem>& items)
    {
        // 入力が不正な場合はGPU Frameを開始しません。
        if (opaquePipeline == nullptr || transparentPipeline == nullptr)
        {
            return RHIFrameResult::FatalError;
        }
        const RHIFrameResult begin = frame.BeginFrame();
        if (begin != RHIFrameResult::Success)
        {
            return begin;
        }
        return DrawActiveFrame(frame, commands, opaquePipeline, transparentPipeline, items);
    }

    // ApplicationがBeginFrameを所有する経路では、二重Acquireを避けてこちらを呼びます。
    // このメソッドはActive Frameへの描画とEnd/Presentを担当し、Frame開始は行いません。
    // Texture Descriptor等の準備はBeginFrameより前に完了させてください。
    static RHIFrameResult DrawActiveFrame(RHISceneFrameLifecycle& frame,
        RHISceneCommandList& commands,
        const Ref<RHIGraphicsPipeline>& opaquePipeline,
        const Ref<RHIGraphicsPipeline>& transparentPipeline,
        const std::vector<RHISceneDrawItem>& items)
    {
        if (opaquePipeline == nullptr || transparentPipeline == nullptr ||
            Draw(commands, opaquePipeline, transparentPipeline, items) == false)
        {
            return RHIFrameResult::FatalError;
        }
        return FinishActiveFrame(frame);
    }

    // 描画命令の記録後だけ呼びます。Submitに失敗したFrameはPresentしません。
    // Applicationが受け取るResizeRequired/FatalErrorを変更せず伝えます。
    static RHIFrameResult FinishActiveFrame(RHISceneFrameLifecycle& frame)
    {
        const RHIFrameResult end = frame.EndFrame();
        if (end != RHIFrameResult::Success)
        {
            return end;
        }
        return frame.Present();
    }

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
