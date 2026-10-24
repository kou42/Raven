#include "Raven/Core/Memory/HeapAllocator.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <new>

namespace Raven
{

HeapAllocator::~HeapAllocator()
{
    Reset();
}

void* HeapAllocator::Allocate(std::size_t size, std::size_t alignment)
{
    if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
    {
        return nullptr;
    }

    if (size > (std::numeric_limits<std::size_t>::max)() - m_UsedMemory)
    {
        return nullptr;
    }

    // C++17のaligned newは最低限のalignmentも扱えるよう、通常newと使い分けます。
    const bool overAligned = alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__;
    void* memory = nullptr;
    try
    {
        if (overAligned)
        {
            memory = ::operator new(size, std::align_val_t(alignment));
        }
        else
        {
            memory = ::operator new(size);
        }
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }

    try
    {
        m_Allocations.emplace(memory, Allocation{ size, overAligned ? alignment : 0 });
    }
    catch (...)
    {
        if (overAligned)
        {
            ::operator delete(memory, std::align_val_t(alignment));
        }
        else
        {
            ::operator delete(memory);
        }
        throw;
    }

    m_UsedMemory += size;
    m_PeakUsedMemory = std::max(m_PeakUsedMemory, m_UsedMemory);
    return memory;
}

void HeapAllocator::Deallocate(void* memory)
{
    if (memory == nullptr)
    {
        return;
    }

    const auto it = m_Allocations.find(memory);
    // Debug時は二重解放・未知のポインタを検出します。
    assert(it != m_Allocations.end() && "HeapAllocator: invalid pointer or double free");
    if (it == m_Allocations.end())
    {
        return;
    }

    const Allocation allocation = it->second;
    m_UsedMemory -= allocation.size;
    m_Allocations.erase(it);

    if (allocation.alignment != 0)
    {
        ::operator delete(memory, std::align_val_t(allocation.alignment));
    }
    else
    {
        ::operator delete(memory);
    }
}

void HeapAllocator::Reset()
{
    // 生存する確保を全解放します。Peakは過去最大値として維持します。
    while (m_Allocations.empty() == false)
    {
        Deallocate(m_Allocations.begin()->first);
    }
}

std::size_t HeapAllocator::GetCapacity() const
{
    return (std::numeric_limits<std::size_t>::max)();
}

std::size_t HeapAllocator::GetUsedMemory() const
{
    return m_UsedMemory;
}

std::size_t HeapAllocator::GetPeakUsedMemory() const
{
    return m_PeakUsedMemory;
}

std::size_t HeapAllocator::GetAllocationCount() const
{
    return m_Allocations.size();
}

} // namespace Raven
