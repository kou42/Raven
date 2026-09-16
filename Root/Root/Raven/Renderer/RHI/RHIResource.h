#pragma once

namespace Raven
{

// ============================================================================
// RHIResource
// ============================================================================
// Buffer / Texture / Shader等、GPU Resourceの共通基底です。
// native handleやGraphics API固有型はここへ公開せず、Backend実装内部へ閉じ込めます。
class RHIResource
{
public:
    virtual ~RHIResource() = default;

protected:
    RHIResource() = default;

    RHIResource(const RHIResource&) = delete;
    RHIResource& operator=(const RHIResource&) = delete;
};

} // namespace Raven
