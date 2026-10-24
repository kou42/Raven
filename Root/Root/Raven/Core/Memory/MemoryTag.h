#pragma once

#include <cstddef>

namespace Raven
{

// AllocationをSubsystem別に集計するための分類です。
enum class MemoryTag : std::size_t
{
    Core,
    Renderer,
    RHI,
    Physics,
    Animation,
    Audio,
    Resource,
    Scene,
    UI,
    Temporary,
    Count
};

} // namespace Raven
