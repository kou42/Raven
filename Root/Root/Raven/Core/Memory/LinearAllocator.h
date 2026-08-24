#pragma once

#include "Raven/Core/Memory/Allocator.h"

#include <cstddef>

namespace Raven
{

// 連続したメモリ領域を先頭から順に切り出すAllocatorです。
// 個別解放は行わず、Reset() でまとめて再利用します。
//
// 想定用途:
// - PhysicsのBroadPhase候補
// - Contact / Constraintの一時配列
// - Animation / Renderのフレーム内一時データ
//
// 確保処理は基本的に「alignment調整 + offset加算」だけなので、
// 汎用heap allocatorより非常に軽量です。
class LinearAllocator final : public Allocator
{
public:
    LinearAllocator(void* memory, std::size_t capacity);

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;
    LinearAllocator(LinearAllocator&&) = delete;
    LinearAllocator& operator=(LinearAllocator&&) = delete;

    void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
    void Deallocate(void* memory) override;
    void Reset() override;

    [[nodiscard]] std::size_t GetCapacity() const override;
    [[nodiscard]] std::size_t GetUsedMemory() const override;
    [[nodiscard]] std::size_t GetPeakUsedMemory() const override;
    [[nodiscard]] std::size_t GetAllocationCount() const override;

private:
    [[nodiscard]] static bool IsPowerOfTwo(std::size_t value);
    [[nodiscard]] std::size_t CalculateAlignmentAdjustment(std::size_t alignment) const;

private:
    std::byte* m_Memory = nullptr;
    std::size_t m_Capacity = 0;
    std::size_t m_Offset = 0;

    // Reset()後も保持するHigh Water Markです。
    // FrameAllocator容量を実測値から調整できるようにします。
    std::size_t m_PeakUsedMemory = 0;
    std::size_t m_AllocationCount = 0;
};

}
