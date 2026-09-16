#pragma once

#include <cstddef>
#include <cstdint>

namespace Raven
{

// ============================================================================
// RHIBackend
// ============================================================================
// Renderer上位層からGraphics API固有のenumを排除するためのBackend識別子です。
// RendererAPI::APIとは段階移行中だけ併存し、RHI移行完了後はこちらへ統一します。
enum class RHIBackend
{
    None = 0,
    OpenGL,
    DirectX11,
    DirectX12,
    Vulkan
};

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
