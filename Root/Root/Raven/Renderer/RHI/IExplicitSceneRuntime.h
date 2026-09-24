#pragma once

#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"

namespace Raven
{

class Scene;

// Explicit Scene実行のBackend非依存境界です。
// WindowとSceneは呼び出し側が所有し、RuntimeはGPU ResourceとFrame準備状態を所有します。
// Shutdownより前にScene側のMesh/Material参照を解放してください。
class IExplicitSceneRuntime
{
public:
    virtual ~IExplicitSceneRuntime() = default;

    // Init成功後にのみFrame境界を借用できます。Shutdown後の参照は無効です。
    virtual RHISceneFrameLifecycle* GetFrameLifecycle() = 0;
    virtual bool PrepareScene(Scene& scene) = 0;
    // PrepareはAcquireより前、DrawPreparedはBeginFrame成功後に呼びます。
    virtual bool PrepareFrame() = 0;
    virtual RHIFrameResult DrawPreparedFrame() = 0;
    // Acquire失敗・Resize・終了時に準備済みSnapshotを解放します。
    virtual void DiscardPreparedFrame() = 0;
    virtual bool Resize(uint32_t width, uint32_t height) = 0;
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual bool IsInitialized() const = 0;
    virtual void Shutdown() = 0;
};

} // namespace Raven
