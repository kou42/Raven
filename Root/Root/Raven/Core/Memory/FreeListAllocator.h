#pragma once

#include "Raven/Core/Memory/Allocator.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Raven
{

// 固定Arena内で可変サイズの確保と個別解放を行います。
// 外部からの同時アクセスは想定しません。
class FreeListAllocator final : public Allocator
{
public:
    explicit FreeListAllocator(std::size_t capacity);
    ~FreeListAllocator() override = default;

    FreeListAllocator(const FreeListAllocator&) = delete;
    FreeListAllocator& operator=(const FreeListAllocator&) = delete;
    FreeListAllocator(FreeListAllocator&&) = delete;
    FreeListAllocator& operator=(FreeListAllocator&&) = delete;

    void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
    void Deallocate(void* memory) override;
    void Reset() override;

    [[nodiscard]] std::size_t GetCapacity() const override;
    [[nodiscard]] std::size_t GetUsedMemory() const override;
    [[nodiscard]] std::size_t GetPeakUsedMemory() const override;
    [[nodiscard]] std::size_t GetAllocationCount() const override;
    [[nodiscard]] std::size_t GetFreeBlockCount() const;
    [[nodiscard]] std::size_t GetLargestFreeBlock() const;

private:
    struct FreeBlock
    {
        std::size_t size;
        FreeBlock* next;
    };

    struct AllocationHeader
    {
        std::size_t size;
        std::size_t adjustment;
        std::uintptr_t marker;
    };

    static constexpr std::uintptr_t AllocationMarker = 0xF1EEA110u;
    static constexpr std::size_t MinimumBlockSize = sizeof(FreeBlock);

    std::unique_ptr<std::byte[]> m_Storage;
    std::byte* m_Begin = nullptr;
    std::size_t m_Capacity = 0;
    FreeBlock* m_FreeList = nullptr;
    std::size_t m_UsedMemory = 0;
    std::size_t m_PeakUsedMemory = 0;
    std::size_t m_AllocationCount = 0;
};

} // namespace Raven
