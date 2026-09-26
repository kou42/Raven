#pragma once

#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"

namespace Raven
{

class Scene;
class UIDrawList;
struct PipelineSpecification;
struct RHIShaderAssetSpecification;

// Explicit Scene実行のBackend非依存境界です。
// WindowとSceneは呼び出し側が所有し、RuntimeはGPU ResourceとFrame準備状態を所有します。
// Shutdownより前にScene側のMesh/Material参照を解放してください。
class IExplicitSceneRuntime
{
public:
    virtual ~IExplicitSceneRuntime() = default;

    // Shader Assetは選択Backend用のPathを指定します。Windowの所有権は移しません。
    // 初期化失敗時は各Runtimeが部分生成Resourceを解放します。
    virtual bool Init(Window& window,
        const PipelineSpecification& pipelineSpecification,
        const RHIShaderAssetSpecification& vertexShader,
        const RHIShaderAssetSpecification& fragmentShader) = 0;

    // Init成功後にのみFrame境界を借用できます。Shutdown後の参照は無効です。
    virtual RHISceneFrameLifecycle* GetFrameLifecycle() = 0;
    virtual bool PrepareScene(Scene& scene) = 0;
    // UIContext::EndFrame後のCPU DrawListを次のPrepareFrameへ渡します。
    // GPU Resource生成はAcquire前のPrepareFrame内で行います。
    virtual void SubmitUIFrame(const UIDrawList& drawList,
        uint32_t viewportWidth, uint32_t viewportHeight) = 0;
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
