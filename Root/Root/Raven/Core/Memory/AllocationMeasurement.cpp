#include "Raven/Core/Memory/AllocationMeasurement.h"

#include <cstdlib>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace Raven
{
namespace
{
thread_local AllocationMeasurement* ActiveMeasurement = nullptr;
}

bool IsAllocationMeasurementEnabled() noexcept
{
#if defined(RAVEN_ENABLE_ALLOCATION_MEASUREMENT) && RAVEN_ENABLE_ALLOCATION_MEASUREMENT
    return true;
#else
    return false;
#endif
}

void RecordMeasuredAllocation(std::size_t bytes) noexcept
{
    // Tracker用のMapやmutexを使わず、newの内部から再帰的に確保しないようにします。
    if (IsAllocationMeasurementEnabled() == true && ActiveMeasurement != nullptr)
    {
        ++ActiveMeasurement->Count;
        ActiveMeasurement->Bytes += bytes;
    }
}

AllocationMeasurementScope::AllocationMeasurementScope(AllocationMeasurement& measurement) noexcept
    : m_Previous(ActiveMeasurement)
{
    measurement.Available = IsAllocationMeasurementEnabled();
    ActiveMeasurement = &measurement;
}

AllocationMeasurementScope::~AllocationMeasurementScope()
{
    ActiveMeasurement = m_Previous;
}
} // namespace Raven

#if defined(RAVEN_ENABLE_ALLOCATION_MEASUREMENT) && RAVEN_ENABLE_ALLOCATION_MEASUREMENT
namespace
{
void* AllocateMeasured(std::size_t bytes, std::size_t alignment)
{
    // 0 byte newも非nullを返す契約とnew_handlerの再試行を維持します。
    const std::size_t size = bytes == 0u ? 1u : bytes;
    for (;;)
    {
        void* memory = nullptr;
        if (alignment == 0u)
        {
            memory = std::malloc(size);
        }
        else
        {
#if defined(_WIN32)
            memory = _aligned_malloc(size, alignment);
#else
            if (posix_memalign(&memory, alignment, size) != 0)
            {
                memory = nullptr;
            }
#endif
        }
        if (memory != nullptr)
        {
            Raven::RecordMeasuredAllocation(bytes);
            return memory;
        }
        const std::new_handler handler = std::get_new_handler();
        if (handler == nullptr)
        {
            throw std::bad_alloc();
        }
        handler();
    }
}

void FreeAligned(void* memory) noexcept
{
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}
} // namespace

// Global operatorの置換は明示的な計測用ビルドに限定します。
// すべての対応するdeleteを同じ確保元へ戻し、Aligned/Sized/Nothrow契約を揃えます。
void* operator new(std::size_t size) { return AllocateMeasured(size, 0u); }
void* operator new[](std::size_t size) { return AllocateMeasured(size, 0u); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void* operator new(std::size_t size, std::align_val_t alignment)
{
    return AllocateMeasured(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return AllocateMeasured(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* memory, std::align_val_t) noexcept { FreeAligned(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { FreeAligned(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { FreeAligned(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { FreeAligned(memory); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try { return ::operator new(size, alignment); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try { return ::operator new[](size, alignment); } catch (...) { return nullptr; }
}
void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept { FreeAligned(memory); }
void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept { FreeAligned(memory); }
#endif
