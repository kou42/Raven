#include "Raven/Core/Memory/PoolAllocator.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <new>

namespace Raven
{

PoolAllocator::PoolAllocator(std::size_t objectSize, std::size_t objectAlignment, std::size_t slotCount)
{
    // 不正な設定と乗算・丸めoverflowを拒否し、空のPoolとして初期化します。
    if (objectSize == 0 || objectAlignment == 0 ||
        (objectAlignment & (objectAlignment - 1)) != 0 || slotCount == 0)
    {
        return;
    }

    m_Alignment = std::max(objectAlignment, alignof(std::max_align_t));
    if (objectSize > (std::numeric_limits<std::size_t>::max)() - (m_Alignment - 1))
    {
        return;
    }
    m_Stride = (objectSize + m_Alignment - 1) & ~(m_Alignment - 1);
    if (slotCount > (std::numeric_limits<std::size_t>::max)() / m_Stride)
    {
        return;
    }

    // aligned newを用い、全スロットが要求alignmentを満たすようにします。
    const std::size_t capacity = m_Stride * slotCount;
    m_Storage.reset(static_cast<std::byte*>(::operator new(capacity, std::align_val_t(m_Alignment))));
    m_Next = std::make_unique<std::size_t[]>(slotCount);
    m_InUse = std::make_unique<bool[]>(slotCount);
    m_Begin = m_Storage.get();
    m_ObjectSize = objectSize;
    m_SlotCount = slotCount;
    Reset();
}

void* PoolAllocator::Allocate(std::size_t size, std::size_t alignment)
{
    if (size == 0 || size > m_ObjectSize || alignment == 0 ||
        (alignment & (alignment - 1)) != 0 || alignment > m_Alignment ||
        m_FirstFree == InvalidIndex)
    {
        return nullptr;
    }

    const std::size_t index = m_FirstFree;
    m_FirstFree = m_Next[index];
    m_InUse[index] = true;
    ++m_AllocationCount;
    m_PeakUsedMemory = std::max(m_PeakUsedMemory, GetUsedMemory());
    return m_Begin + index * m_Stride;
}

void PoolAllocator::Deallocate(void* memory)
{
    if (memory == nullptr)
    {
        return;
    }

    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(memory);
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(m_Begin);
    if (m_Begin == nullptr || address < begin ||
        address - begin >= GetCapacity() ||
        (address - begin) % m_Stride != 0)
    {
        assert(false && "PoolAllocator: invalid pointer");
        return;
    }

    const std::size_t index = (address - begin) / m_Stride;
    assert(m_InUse[index] && "PoolAllocator: double free");
    if (m_InUse[index] == false)
    {
        return;
    }

    m_InUse[index] = false;
    m_Next[index] = m_FirstFree;
    m_FirstFree = index;
    --m_AllocationCount;
}

void PoolAllocator::Reset()
{
    for (std::size_t index = 0; index < m_SlotCount; ++index)
    {
        m_Next[index] = index + 1 < m_SlotCount ? index + 1 : InvalidIndex;
        m_InUse[index] = false;
    }
    m_FirstFree = m_SlotCount > 0 ? 0 : InvalidIndex;
    m_AllocationCount = 0;
}

std::size_t PoolAllocator::GetCapacity() const { return m_Stride * m_SlotCount; }
std::size_t PoolAllocator::GetUsedMemory() const { return m_Stride * m_AllocationCount; }
std::size_t PoolAllocator::GetPeakUsedMemory() const { return m_PeakUsedMemory; }
std::size_t PoolAllocator::GetAllocationCount() const { return m_AllocationCount; }
std::size_t PoolAllocator::GetSlotCount() const { return m_SlotCount; }
std::size_t PoolAllocator::GetAvailableSlotCount() const { return m_SlotCount - m_AllocationCount; }

} // namespace Raven
