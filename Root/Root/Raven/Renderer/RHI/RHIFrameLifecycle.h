#pragma once

#include "Raven/Renderer/RHI/RHIClearContext.h"

namespace Raven
{
// Clear Demoから一般的なFrame境界を切り出すための最小契約。
// 描画命令そのものはRHICommandListの責務とし、GPU同期・PresentはBackendが所有します。
// BeginFrameがSuccess以外を返した場合は後続の段階を呼びません。
class RHIFrameLifecycle
{
public:
    virtual ~RHIFrameLifecycle() = default;

    virtual RHIFrameResult BeginFrame() = 0;
    virtual RHIFrameResult ClearFrame(const float clearColor[4]) = 0;
    virtual RHIFrameResult EndFrame() = 0;
    virtual RHIFrameResult Present() = 0;
};

// Clear Demo以外の呼び出し元も同じ順序・失敗時の打ち切り規則を利用できるよう、
// Frame進行だけを共通化します。描画命令やWindowの所有権は移動しません。
inline RHIFrameResult RunRHIClearFrame(
    RHIFrameLifecycle& lifecycle, const float clearColor[4])
{
    if (clearColor == nullptr)
    {
        return RHIFrameResult::FatalError;
    }

    RHIFrameResult result = lifecycle.BeginFrame();
    if (result != RHIFrameResult::Success)
    {
        return result;
    }

    result = lifecycle.ClearFrame(clearColor);
    if (result != RHIFrameResult::Success)
    {
        return result;
    }

    result = lifecycle.EndFrame();
    if (result != RHIFrameResult::Success)
    {
        return result;
    }

    return lifecycle.Present();
}
} // namespace Raven
