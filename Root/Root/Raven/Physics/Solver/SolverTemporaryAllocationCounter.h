#pragma once

#include "Raven/Core/Memory/Allocator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>

namespace Raven
{
namespace ph
{

// ============================================================================
// Solver Temporary Allocation Statistics
// ============================================================================
// Solver内部で「そのStep中だけ必要なSTLコンテナ」が要求した一時メモリを
// Before / After比較するための統計です。
//
// 重要:
// - AllocationCount / AllocationBytes はSTL allocator::allocate()が実際に呼ばれた回数とbytesです。
// - DeallocationCount / DeallocationBytes はallocator::deallocate()側を同様に数えます。
// - ActiveBytes は現在生存中の一時確保量です。
// - PeakActiveBytes は1 Reset区間内で同時に生存した最大量です。
//
// std::vector::capacity()から推定する方式では、unordered_mapのNode確保やBucket確保を
// 正確に追えません。そのため、STL Allocator境界で要求bytesを直接計測します。
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
        // 起こさないよう0へclampします。通常はallocate/deallocateの対応により十分な値があります。
        ActiveBytes = (ActiveBytes >= byteCount)
            ? ActiveBytes - byteCount
            : 0u;
    }
};

// ============================================================================
// Solver Temporary STL Allocator Adapter
// ============================================================================
// このクラス自身はRaven::Allocatorを継承しません。
// std::vector / std::unordered_map等が要求するSTL Allocator契約を担当し、
// 実際のメモリ確保元としてRaven::Allocatorを「利用」します。
//
// 責務を次のように分離します。
//
//   STL Container
//       -> SolverTemporaryAllocator<T>   : STL契約 + Solver Counter
//       -> Raven::Allocator              : Engine共通Allocator Interface
//       -> FrameAllocator / LinearAllocator
//
// Before計測ではbackingAllocator == nullptrとして通常std::allocatorを使用します。
// Phase ③では同じコンテナ型のままFrameAllocatorを渡し、計測方法を変えずに比較します。
//
// 既存STLAllocatorAdapterと役割は近いですが、こちらはSolver最適化期間中の
// Before / After計測を同じAllocator型で維持するため、Counter機能だけを追加した計測用Adapterです。
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

    explicit SolverTemporaryAllocator(
        SolverTemporaryAllocationStatistics* statistics,
        Allocator* backingAllocator = nullptr) noexcept
        : m_Statistics(statistics)
        , m_BackingAllocator(backingAllocator)
    {
    }

    template<typename U>
    SolverTemporaryAllocator(const SolverTemporaryAllocator<U>& other) noexcept
        : m_Statistics(other.GetStatistics())
        , m_BackingAllocator(other.GetBackingAllocator())
    {
    }

    [[nodiscard]] T* allocate(std::size_t count)
    {
        if (count == 0u)
        {
            return nullptr;
        }

        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
        {
            throw std::bad_array_new_length{};
        }

        const std::size_t bytes = count * sizeof(T);
        T* memory = nullptr;

        if (m_BackingAllocator != nullptr)
        {
            void* rawMemory = m_BackingAllocator->Allocate(bytes, alignof(T));
            if (rawMemory == nullptr)
            {
                throw std::bad_alloc{};
            }

            memory = static_cast<T*>(rawMemory);
        }
        else
        {
            // Phase ② Before計測では既存実装と同じ通常Heapを使います。
            memory = std::allocator<T>{}.allocate(count);
        }

        if (m_Statistics != nullptr)
        {
            m_Statistics->RecordAllocation(bytes);
        }

        return memory;
    }

    void deallocate(T* memory, std::size_t count) noexcept
    {
        if (memory == nullptr)
        {
            return;
        }

        const std::size_t bytes = count * sizeof(T);
        if (m_Statistics != nullptr)
        {
            m_Statistics->RecordDeallocation(bytes);
        }

        if (m_BackingAllocator != nullptr)
        {
            // Frame / Linear AllocatorではDeallocate()はno-opです。
            // 個別解放の有無はBacking Allocator側の責務とし、STL Adapter側では統一して委譲します。
            m_BackingAllocator->Deallocate(memory);
        }
        else
        {
            std::allocator<T>{}.deallocate(memory, count);
        }
    }

    [[nodiscard]] SolverTemporaryAllocationStatistics* GetStatistics() const noexcept
    {
        return m_Statistics;
    }

    [[nodiscard]] Allocator* GetBackingAllocator() const noexcept
    {
        return m_BackingAllocator;
    }

    template<typename U>
    bool operator==(const SolverTemporaryAllocator<U>& rhs) const noexcept
    {
        return m_Statistics == rhs.GetStatistics()
            && m_BackingAllocator == rhs.GetBackingAllocator();
    }

    template<typename U>
    bool operator!=(const SolverTemporaryAllocator<U>& rhs) const noexcept
    {
        return (*this == rhs) == false;
    }

private:
    template<typename>
    friend class SolverTemporaryAllocator;

    SolverTemporaryAllocationStatistics* m_Statistics = nullptr;
    Allocator* m_BackingAllocator = nullptr;
};

} // namespace ph
} // namespace Raven
