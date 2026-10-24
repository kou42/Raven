#pragma once

#include "Raven/Core/Memory/Allocator.h"

#include <cstddef>
#include <unordered_map>

namespace Raven
{

// 個別解放可能な長寿命メモリ向けAllocatorです。
// Reset()は生存中の全確保を解放します。確保済みポインタは以後使用できません。
// このクラスと返されたポインタの操作は外部で同期してください。
class HeapAllocator final : public Allocator
{
public:
    HeapAllocator() = default;
    ~HeapAllocator() override;

    HeapAllocator(const HeapAllocator&) = delete;
    HeapAllocator& operator=(const HeapAllocator&) = delete;
    HeapAllocator(HeapAllocator&&) = delete;
    HeapAllocator& operator=(HeapAllocator&&) = delete;

    void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
    void Deallocate(void* memory) override;
    void Reset() override;

    // Heapは固定容量を持たないため、Capacityはsize_tの最大値を返します。
    [[nodiscard]] std::size_t GetCapacity() const override;
    [[nodiscard]] std::size_t GetUsedMemory() const override;
    [[nodiscard]] std::size_t GetPeakUsedMemory() const override;
    [[nodiscard]] std::size_t GetAllocationCount() const override;

private:
    struct Allocation
    {
        std::size_t size;
        std::size_t alignment;
    };

    // 生存ポインタを記録し、解放時のサイズ集計と不正ポインタの検出に利用します。
    std::unordered_map<void*, Allocation> m_Allocations;
    std::size_t m_UsedMemory = 0;
    std::size_t m_PeakUsedMemory = 0;
};

} // namespace Raven
