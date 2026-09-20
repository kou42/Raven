#pragma once

#include <cstdint>

#include "Raven/Renderer/RHI/RHISceneRenderServices.h"
#include "Raven/Renderer/RHI/RHITypes.h"
#include "Raven/Core/Window.h"

namespace Raven
{

// Scene Context/Deviceを所有し、同一Contextに紐付くServicesを公開する入口です。
// Windowは呼び出し側が所有します。Scene GPU ResourceはWaitIdle後、Shutdown前に破棄してください。
// 通常ApplicationのOpenGL Legacy経路とは独立した段階的移行用契約です。
class RHISceneRuntime
{
public:
    virtual ~RHISceneRuntime() = default;

    // WindowのBackendを唯一の選択基準としてRuntimeを生成します。
    // 未対応Backendではnullptrを返し、OpenGLへの暗黙fallbackを行いません。
    // Windowの所有権は移しません。
    static Scope<RHISceneRuntime> Create(Window& window);

    virtual RHIBackend GetBackend() const = 0;
    virtual RHISceneRenderServices& GetServices() = 0;
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;

    // GPUがResourceを参照し終えるまで待機します。
    virtual bool WaitIdle() = 0;
    virtual void Shutdown() = 0;
};

} // namespace Raven
