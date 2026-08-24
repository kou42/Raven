#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

namespace Raven
{
namespace ph
{

// ============================================================================
// Solver Temporary Allocation Statistics
// ============================================================================
// Solver内部で「そのStep中だけ必要なSTLコンテナ」が通常Heapへ要求したメモリ量を
// Before / After比較するための統計です。
//
// 重要:
// - AllocationCount / AllocationBytes は allocator::allocate() が実際に呼ばれた回数とbytesです。
// - DeallocationCount / DeallocationBytes は allocator::deallocate() 側を同様に数えます。
// - ActiveBytes は現在生存中の一時確保量です。
// - PeakActiveBytes は1 Reset区間内で同時に生存した最大量です。
//
// std::vector::capacity()から推定する方式では、unordered_mapのNode確保やBucket確保を
// 正確に追えません。そのため、STL Allocatorの境界で要求を直接計測します。
//
// 次段階ではSolverTemporaryAllocatorのBacking AllocatorだけをFrameAllocatorへ切り替えます。
// Counter APIを維持したままBefore / Afterを比較できるため、計測方法の差によるブレを避けられます。
struct SolverTemporaryAllocationStatistics
{
    uint64_t AllocationCount = 0u;
    uint64_t AllocationBytes = 0u;
    uint64_t DeallocationCount = 0u;
    uint64_t DeallocationBytes = 0u;
    uint64_t ActiveBytes = 0u;
    uint64_t PeakActiveBytes = 0u;

    void Reset()
    {
        AllocationCount = 0u;
        AllocationBytes = 0u;
        DeallocationCount = 0u;
        DeallocationBytes = 0u;
        ActiveBytes = 0u;
        PeakActiveBytes = 0u;
    }

    void RecordAllocation(std::size_t bytes)
    {
        const uint64_t byteCount = static_cast<uint64_t>(bytes);
        ++AllocationCount;
        AllocationBytes += byteCount;
        ActiveBytes += byteCount;
        PeakActiveBytes = std::max(PeakActiveBytes, ActiveBytes);
    }

    void RecordDeallocation(std::size_t bytes)
    {
        const uint64_t byteCount = static_cast<uint64_t>(bytes);
        ++DeallocationCount;
        DeallocationBytes += byteCount;

        // Counter自体が計測対象Solverを壊してはいけないため、異常時でもunsigned underflowを
        // 起こさないよう0へclampします。通常はallocate/deallocateの対応により常に十分な値があります。
        ActiveBytes = (ActiveBytes >= byteCount)
            ? ActiveBytes - byteCount
            : 0u;
    }
};

// ============================================================================
// Solver Temporary Allocator
// ============================================================================
// 現段階ではBackingとしてstd::allocatorを使用し、「通常Heapへ何回・何bytes要求したか」を
// 可視化するためだけのAllocatorです。
//
// Phase ③ FrameAllocator適用ではallocate()/deallocate()のBackingだけを差し替えます。
// STLコンテナ側の型とCounterはそのまま維持できるため、Before / After比較条件を揃えられます。
template<typename T>
class SolverTemporaryAllocator
{
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template<typename U>
    struct rebind
    {
        using other = SolverTemporaryAllocator<U>;
    };

    SolverTemporaryAllocator() noexcept = default;

    explicit SolverTemporaryAllocator(SolverTemporaryAllocationStatistics* statistics) noexcept
        : m_Statistics(statistics)
    {
    }

    template<typename U>
    SolverTemporaryAllocator(const SolverTemporaryAllocator<U>& other) noexcept
        : m_Statistics(other.GetStatistics())
    {
    }

    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
        {
            throw std::bad_array_new_length{};
        }

        T* memory = std::allocator<T>{}.allocate(count);
        if (m_Statistics != nullptr)
        {
            m_Statistics->RecordAllocation(count * sizeof(T));
        }

        return memory;
    }

    void deallocate(T* memory, std::size_t count) noexcept
    {
        if (m_Statistics != nullptr)
        {
            m_Statistics->RecordDeallocation(count * sizeof(T));
        }

        std::allocator<T>{}.deallocate(memory, count);
    }

    [[nodiscard]] SolverTemporaryAllocationStatistics* GetStatistics() const noexcept
    {
        return m_Statistics;
    }

    template<typename U>
    bool operator==(const SolverTemporaryAllocator<U>& rhs) const noexcept
    {
        return m_Statistics == rhs.GetStatistics();
    }

    template<typename U>
    bool operator!=(const SolverTemporaryAllocator<U>& rhs) const noexcept
    {
        return (*this == rhs) == false;
    }

private:
    SolverTemporaryAllocationStatistics* m_Statistics = nullptr;
};

} // namespace ph
} // namespace Raven
