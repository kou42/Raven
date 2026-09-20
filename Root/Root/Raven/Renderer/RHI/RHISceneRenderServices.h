#pragma once

#include "Raven/Renderer/RHI/RHISceneCommandList.h"
#include "Raven/Renderer/RHI/RHISceneFrameLifecycle.h"
#include "Raven/Renderer/RHI/RHISceneResourceFactory.h"

namespace Raven
{

// Sceneが利用するFrame境界・描画命令・Resource生成の組を表します。
// BackendのContext/Deviceは所有せず、各参照の生存期間は実装側が保証します。
// Frame開始前に取得したCommandListも、記録可能なのはBeginFrame～EndFrameの間だけです。
class RHISceneRenderServices
{
public:
    virtual ~RHISceneRenderServices() = default;

    virtual RHISceneFrameLifecycle& GetFrameLifecycle() = 0;
    virtual RHISceneCommandList& GetCommandList() = 0;
    virtual RHISceneResourceFactory& GetResourceFactory() = 0;
};

} // namespace Raven
