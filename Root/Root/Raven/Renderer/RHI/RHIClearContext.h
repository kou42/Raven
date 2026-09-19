#pragma once

#include <cstdint>

namespace Raven
{
class Window;

// Clear DemoのFrame境界を統一します。GPU同期とFrame Slotの進行は各Backendが管理します。
enum class RHIFrameResult
{
    Success,
    ResizeRequired,
    FatalError
};

class RHIClearContext
{
public:
    virtual ~RHIClearContext() = default;
    virtual bool Init(Window& window) = 0;
    virtual RHIFrameResult DrawClearFrame(const float clearColor[4]) = 0;
    virtual bool Resize(uint32_t width, uint32_t height) = 0;
    virtual void Shutdown() = 0;
};
} // namespace Raven
