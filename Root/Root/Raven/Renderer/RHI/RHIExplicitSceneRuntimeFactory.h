#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHITypes.h"

namespace Raven
{

class IExplicitSceneRuntime;

// DX12 / VulkanのExplicit Scene Runtime生成をBackend選択から分離する共通Factoryです。
// ApplicationやScene側は具体Runtime型を知らず、IExplicitSceneRuntimeだけを所有します。
// OpenGLは現行Legacy経路を使用するため、ここでは生成対象に含めません。
class RHIExplicitSceneRuntimeFactory
{
public:
    static Scope<IExplicitSceneRuntime> Create(RHIBackend backend);
};

} // namespace Raven
