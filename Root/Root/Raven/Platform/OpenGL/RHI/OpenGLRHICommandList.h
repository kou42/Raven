#pragma once

#include "Raven/Renderer/RHI/RHICommandList.h"

namespace Raven
{

class OpenGLRHICommandList final : public RHICommandList
{
public:
    ~OpenGLRHICommandList() override = default;

    void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
    void SetClearColor(float r, float g, float b, float a) override;
    void Clear() override;
};

} // namespace Raven
