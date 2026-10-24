#pragma once

#include "Raven/Core/Memory/Allocator.h"
#include "Raven/Core/Memory/MemoryTag.h"

#include <array>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Raven
{

struct MemoryStatistics
{
    std::size_t currentBytes = 0;
    std::size_t peakBytes = 0;
    std::size_t allocationCount = 0;
};

struct MemoryAllocationRecord
{
    void* memory = nullptr;
    std::size_t size = 0;
    std::size_t alignment = 0;
    MemoryTag tag = MemoryTag::Core;
    const char* allocatorType = nullptr;
    const char* file = nullptr;
    int line = 0;
};

// 中央診断用の追跡層です。必要なAllocatorにだけ明示的に接続します。
// 追跡情報の保持には標準heapを使用し、再帰的なTracker呼び出しを防ぎます。
class MemoryTracker final
{
public:
    static MemoryTracker& Get();

    void RecordAllocation(const MemoryAllocationRecord& record);
    void RecordDeallocation(void* memory);
    [[nodiscard]] MemoryStatistics GetStatistics(MemoryTag tag) const;
    [[nodiscard]] MemoryStatistics GetTotalStatistics() const;
    [[nodiscard]] std::vector<MemoryAllocationRecord> GetLiveAllocations() const;
    [[nodiscard]] std::size_t GetLeakCount() const;
    void Reset();

private:
    MemoryTracker() = default;

    mutable std::mutex m_Mutex;
    std::unordered_map<void*, MemoryAllocationRecord> m_Records;
    std::array<MemoryStatistics, static_cast<std::size_t>(MemoryTag::Count)> m_ByTag{};
    MemoryStatistics m_Total{};
};

// 既存Allocatorを変更せずに追跡を付加するDecoratorです。
// DeallocateとReset時の統計更新はTracker内の生存レコードに基づきます。
class TrackedAllocator final : public Allocator
{
public:
    TrackedAllocator(Allocator& backing, MemoryTag tag, const char* allocatorType);
    ~TrackedAllocator() override;

    TrackedAllocator(const TrackedAllocator&) = delete;
    TrackedAllocator& operator=(const TrackedAllocator&) = delete;

    void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
    void* AllocateAt(std::size_t size, std::size_t alignment, const char* file, int line);
    void Deallocate(void* memory) override;
    void Reset() override;

    [[nodiscard]] std::size_t GetCapacity() const override;
    [[nodiscard]] std::size_t GetUsedMemory() const override;
    [[nodiscard]] std::size_t GetPeakUsedMemory() const override;
    [[nodiscard]] std::size_t GetAllocationCount() const override;

private:
    Allocator& m_Backing;
    MemoryTag m_Tag;
    const char* m_AllocatorType;
    std::vector<void*> m_LivePointers;
};

} // namespace Raven
