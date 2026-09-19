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
} // namespace Raven
