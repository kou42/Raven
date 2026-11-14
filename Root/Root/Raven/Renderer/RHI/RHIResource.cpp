#include "Raven/Renderer/RHI/RHIResource.h"
#include <atomic>

namespace Raven
{
namespace
{
// 診断発行から独立したRHI統計。constructor失敗時もbase destructorで差し引かれます。
// 所有権を追加せず、最後のC++ラッパー参照が消える時点だけを数えます。
std::atomic<std::uint64_t> s_LiveWrappers{ 0 };
}
RHIResource::RHIResource()
{
    s_LiveWrappers.fetch_add(1, std::memory_order_relaxed);
}
RHIResource::~RHIResource()
{
    s_LiveWrappers.fetch_sub(1, std::memory_order_relaxed);
}
std::uint64_t RHIResource::GetLiveWrapperCount() noexcept
{
    return s_LiveWrappers.load(std::memory_order_relaxed);
}
}
