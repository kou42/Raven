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
// Window生成時に選択したBackendを、後続のShader/Pipeline/Buffer等のFactoryが参照します。
// ApplicationのMain Windowと異なるBackendへ実行中に切り替える用途ではありません。
inline RHIBackend& GetRHIBackendStorage()
{
    static RHIBackend backend = RHIBackend::OpenGL;
    return backend;
}

inline RHIBackend GetRHIBackend()
{
    return GetRHIBackendStorage();
}

inline void SetRHIBackend(RHIBackend backend)
{
    GetRHIBackendStorage() = backend;
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

struct RHIViewport
{
    // Backend非依存のFrame/Overlay Viewportです。原点は負座標も許容します。
    int32_t X = 0;
    int32_t Y = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
};

struct RHIBufferSpecification
{
    std::size_t Size = 0;
    RHIBufferUsage Usage = RHIBufferUsage::None;
    RHIMemoryUsage MemoryUsage = RHIMemoryUsage::Static;
    const char* DebugName = "Unnamed RHI Buffer";
};

} // namespace Raven
