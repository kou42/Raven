#pragma once

#include <cstddef>
#include <cstdint>

namespace Raven
{

// ============================================================================
// RHIBackend
// ============================================================================
// Renderer上位層からGraphics API固有のenumを排除するためのBackend識別子です。
// Resource factoryとRenderCommandはこの識別子だけを参照し、Legacy RendererAPIへの
// Backend選択依存を段階的に解消します。
enum class RHIBackend
{
    None = 0,
    OpenGL,
    DirectX11,
    DirectX12,
    Vulkan
};

// 現在利用するGraphics BackendをRHI層で一元管理します。
// 現段階ではOpenGL固定ですが、将来Application設定や起動引数から選択する場合も
// RendererAPIを復活させず、このRHI側の選択点だけを差し替えます。
inline constexpr RHIBackend GetRHIBackend()
{
    return RHIBackend::OpenGL;
}

// GPU Bufferがどの用途で利用されるかをAPI非依存で表します。
// OpenGLのGL_ARRAY_BUFFER等へ直接対応させず、各Backendでnative usageへ変換します。
enum class RHIBufferUsage
{
    None = 0,
    Vertex,
    Index,
    Uniform,
    Storage
};

// CPUからの更新頻度を表します。
// Staticは生成後の更新が少ないResource、Dynamicはframe中も更新されるResourceを想定します。
enum class RHIMemoryUsage
{
    Static = 0,
    Dynamic
};

struct RHIBufferSpecification
{
    std::size_t Size = 0;
    RHIBufferUsage Usage = RHIBufferUsage::None;
    RHIMemoryUsage MemoryUsage = RHIMemoryUsage::Static;
    const char* DebugName = "Unnamed RHI Buffer";
};

} // namespace Raven
