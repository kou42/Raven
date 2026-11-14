#pragma once

#include <cstdint>

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
    virtual ~RHIResource();
    // 生存するC++ラッパー数です。native handle数・GPU解放完了数・byte数ではありません。
    static std::uint64_t GetLiveWrapperCount() noexcept;

protected:
    RHIResource();

    RHIResource(const RHIResource&) = delete;
    RHIResource& operator=(const RHIResource&) = delete;
};

} // namespace Raven
