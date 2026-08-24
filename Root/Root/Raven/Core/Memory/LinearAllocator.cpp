#include "Raven/Core/Memory/LinearAllocator.h"

#include <cstdint>
#include <limits>

namespace Raven
{

LinearAllocator::LinearAllocator(void* memory, std::size_t capacity)
    : m_Memory(static_cast<std::byte*>(memory))
    , m_Capacity(capacity)
{
}

void* LinearAllocator::Allocate(std::size_t size, std::size_t alignment)
{
    // 0 byte確保は呼び出し側のバグを隠しやすく、返すアドレスにも意味がないため
    // 明示的に失敗として扱います。
    if (size == 0)
    {
        return nullptr;
    }

    // alignmentは2の累乗である必要があります。
    // 例: 1, 2, 4, 8, 16, 32 ...
    if (IsPowerOfTwo(alignment) == false)
    {
        return nullptr;
    }

    if (m_Memory == nullptr)
    {
        return nullptr;
    }

    const std::size_t adjustment = CalculateAlignmentAdjustment(alignment);

    // size + adjustment の加算overflowを先に防ぎます。
    // overflowすると「容量内に見える」危険な判定になるため重要です。
    if (size > (std::numeric_limits<std::size_t>::max)() - adjustment)
    {
        return nullptr;
    }

    const std::size_t requiredSize = size + adjustment;

    // m_Offset + requiredSize を直接比較するとoverflowの可能性があるため、
    // 残容量との比較に変換しています。
    if (m_Offset > m_Capacity)
    {
        return nullptr;
    }

    const std::size_t remainingSize = m_Capacity - m_Offset;
    if (requiredSize > remainingSize)
    {
        return nullptr;
    }

    std::byte* alignedMemory = m_Memory + m_Offset + adjustment;
    m_Offset += requiredSize;
    ++m_AllocationCount;

    return alignedMemory;
}

void LinearAllocator::Deallocate(void* memory)
{
    // LinearAllocatorでは個別解放を行いません。
    // 確保順序に関係なくReset()で一括解放することが高速性の理由です。
    // 引数はAllocator共通インターフェースを満たすために受け取ります。
    (void)memory;
}

void LinearAllocator::Reset()
{
    // メモリ内容そのものを0クリアする必要はありません。
    // offsetだけを先頭へ戻すことで、次回Allocate()から同じ領域を再利用します。
    m_Offset = 0;
    m_AllocationCount = 0;
}

std::size_t LinearAllocator::GetCapacity() const
{
    return m_Capacity;
}

std::size_t LinearAllocator::GetUsedMemory() const
{
    return m_Offset;
}

std::size_t LinearAllocator::GetAllocationCount() const
{
    return m_AllocationCount;
}

bool LinearAllocator::IsPowerOfTwo(std::size_t value)
{
    if (value == 0)
    {
        return false;
    }

    return (value & (value - 1)) == 0;
}

std::size_t LinearAllocator::CalculateAlignmentAdjustment(std::size_t alignment) const
{
    const std::uintptr_t currentAddress = reinterpret_cast<std::uintptr_t>(m_Memory + m_Offset);
    const std::size_t mask = alignment - 1;
    const std::size_t misalignment = static_cast<std::size_t>(currentAddress & mask);

    if (misalignment == 0)
    {
        return 0;
    }

    return alignment - misalignment;
}

}
