#pragma once

#include "Raven/Core/Memory/Allocator.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Raven
{

// 固定サイズのスロットをO(1)で確保・解放するAllocatorです。
// コンストラクタで全領域を確保し、Allocate時のheap確保は発生しません。
// スロット数とサイズは生成後に変更できず、スレッドセーフではありません。
class PoolAllocator final : public Allocator
{
public:
    PoolAllocator(std::size_t objectSize, std::size_t objectAlignment, std::size_t slotCount);
    ~PoolAllocator() override = default;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&) = delete;
    PoolAllocator& operator=(PoolAllocator&&) = delete;

    void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
    void Deallocate(void* memory) override;
    void Reset() override;

    [[nodiscard]] std::size_t GetCapacity() const override;
    [[nodiscard]] std::size_t GetUsedMemory() const override;
    [[nodiscard]] std::size_t GetPeakUsedMemory() const override;
    [[nodiscard]] std::size_t GetAllocationCount() const override;
    [[nodiscard]] std::size_t GetSlotCount() const;
    [[nodiscard]] std::size_t GetAvailableSlotCount() const;

private:
    static constexpr std::size_t InvalidIndex = static_cast<std::size_t>(-1);

    std::unique_ptr<std::byte[]> m_Storage;
    std::unique_ptr<std::size_t[]> m_Next;
    std::unique_ptr<bool[]> m_InUse;
    std::byte* m_Begin = nullptr;
    std::size_t m_ObjectSize = 0;
    std::size_t m_Alignment = 0;
    std::size_t m_Stride = 0;
    std::size_t m_SlotCount = 0;
    std::size_t m_FirstFree = InvalidIndex;
    std::size_t m_AllocationCount = 0;
    std::size_t m_PeakUsedMemory = 0;
};

} // namespace Raven
