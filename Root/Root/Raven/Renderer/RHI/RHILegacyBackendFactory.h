#pragma once

#include "Raven/Core/Base.h"
#include "Raven/Renderer/RHI/RHITypes.h"

namespace Raven
{

class RHIDevice;
class RHICommandList;

// ============================================================================
// RHILegacyBackendFactory
// ============================================================================
// RenderCommandがPlatform固有型を直接生成しないためのLegacy互換Factoryです。
// Explicit API (DX12 / Vulkan) はRHISceneFrameLifecycle / RHISceneCommandListを使用するため、
// Legacy RHICommandListへ無理に適合させず、対応していないBackendではnullptrを返します。
//
// この境界を置くことで、Renderer上位層はBackend選択だけを行い、具体型のincludeと生成責務を
// Platform実装側へ閉じ込められます。将来Legacy経路を削除する際もRenderCommand側の変更を最小化できます。
class RHILegacyBackendFactory
{
public:
    static Scope<RHIDevice> CreateDevice(RHIBackend backend);
    static Scope<RHICommandList> CreateCommandList(RHIBackend backend);
};

} // namespace Raven
