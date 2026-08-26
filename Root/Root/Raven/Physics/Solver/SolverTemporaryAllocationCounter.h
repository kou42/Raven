#pragma once

#include "Raven/Core/CPUProfiler.h"
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
// - Backing Allocatorを設定した場合、Reset()時にArenaも同時にResetします。
//
// std::vector::capacity()から推定する方式では、unordered_mapのNode確保やBucket確保を
// 正確に追えません。そのため、STL Allocator境界で要求bytesを直接計測します。
struct SolverTemporaryAllocationStatistics
{
    SolverTemporaryAllocationStatistics() noexcept = default;

    explicit SolverTemporaryAllocationStatistics(Allocator* backingAllocator) noexcept
        : m_BackingAllocator(backingAllocator)
    {
    }

    uint64_t AllocationCount = 0u;
    uint64_t AllocationBytes = 0u;
    uint64_t DeallocationCount = 0u;
    uint64_t DeallocationBytes = 0u;
    uint64_t ActiveBytes = 0u;
    uint64_t PeakActiveBytes = 0u;

    // Phase ③ではSoftBodySolverが所有するFrameAllocatorをここへ登録します。
    // SolverTemporaryAllocator<T>は明示的なBackingが渡されなかった場合、この値を継承します。
    // これにより既存の各STLコンテナ生成コードを変えずにHeap / FrameAllocatorを切り替えられます。
    void SetBackingAllocator(Allocator* allocator) noexcept
    {
        m_BackingAllocator = allocator;
    }

    [[nodiscard]] Allocator* GetBackingAllocator() const noexcept
    {
        return m_BackingAllocator;
    }

    [[nodiscard]] std::size_t GetBackingCapacity() const noexcept
    {
        if (m_BackingAllocator == nullptr)
        {
            return 0u;
        }

        return m_BackingAllocator->GetCapacity();
    }

    [[nodiscard]] std::size_t GetBackingUsedMemory() const noexcept
    {
        if (m_BackingAllocator == nullptr)
        {
            return 0u;
        }

        return m_BackingAllocator->GetUsedMemory();
    }

    [[nodiscard]] std::size_t GetBackingPeakUsedMemory() const noexcept
    {
        if (m_BackingAllocator == nullptr)
        {
            return 0u;
        }

        return m_BackingAllocator->GetPeakUsedMemory();
    }

    [[nodiscard]] std::size_t GetBackingAllocationCount() const noexcept
    {
        if (m_BackingAllocator == nullptr)
        {
            return 0u;
        }

        return m_BackingAllocator->GetAllocationCount();
    }

    // ========================================================================
    // Phase ④ Before / After Profiler Counters
    // ========================================================================
    // Reset()直前には、前Stepで使用した全Step-local Containerが既に破棄されています。
    // そのためこの時点でCounterをまとめてProfilerへ送れば、allocate/deallocateのHot Pathへ
    // mutex・文字列処理を持ち込まずにBefore / Afterを比較できます。
    //
    // AllocationCountは「STLからAllocatorへ来た要求回数」であり、Heap allocation回数そのものではありません。
    // FrameAllocator適用後もvector growやunordered_map node生成ではallocate()要求が発生するため、
    // HeapAllocationCount / FrameAllocationCountを別Counterとして出し、実確保元を明確に分離します。
    void SubmitProfilerCounters() const
    {
        CPUProfiler& profiler = CPUProfiler::Get();
        if (profiler.IsEnabled() == false)
        {
            return;
        }

        // 未使用Stepでは0値Counterを大量に追加しません。
        // SoftBody停止中や自己衝突無効時のProfiler表示を不要に埋めないための条件です。
        if (AllocationCount == 0u
            && AllocationBytes == 0u
            && GetBackingUsedMemory() == 0u)
        {
            return;
        }

        const bool frameAllocatorEnabled = m_BackingAllocator != nullptr;
        const double allocationCount = static_cast<double>(AllocationCount);
        const double allocationBytes = static_cast<double>(AllocationBytes);
        const double deallocationCount = static_cast<double>(DeallocationCount);
        const double deallocationBytes = static_cast<double>(DeallocationBytes);
        const double peakActiveBytes = static_cast<double>(PeakActiveBytes);

        const double backingCapacityBytes = static_cast<double>(GetBackingCapacity());
        const double backingStepUsedBytes = static_cast<double>(GetBackingUsedMemory());
        const double backingLifetimePeakBytes = static_cast<double>(GetBackingPeakUsedMemory());
        const double backingAllocationCount = static_cast<double>(GetBackingAllocationCount());

        // Linear / FrameAllocatorはStep中に個別解放しないため、Reset直前のUsedMemoryが
        // そのStepでArenaから消費した最大量と一致します。LifetimePeakは過去Stepを含む最大値です。
        const double frameUtilization = backingCapacityBytes > 0.0
            ? backingStepUsedBytes / backingCapacityBytes
            : 0.0;

        const double heapAllocationCount = frameAllocatorEnabled
            ? 0.0
            : allocationCount;
        const double heapAllocationBytes = frameAllocatorEnabled
            ? 0.0
            : allocationBytes;
        const double frameAllocationCount = frameAllocatorEnabled
            ? backingAllocationCount
            : 0.0;

        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.RequestAllocationCount",
            allocationCount);
        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.RequestAllocationBytes",
            allocationBytes);
        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.RequestDeallocationCount",
            deallocationCount);
        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.RequestDeallocationBytes",
            deallocationBytes);
        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.PeakActiveBytes",
            peakActiveBytes);

        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.HeapAllocationCount",
            heapAllocationCount);
        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.HeapAllocationBytes",
            heapAllocationBytes);

        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.FrameAllocatorEnabled",
            frameAllocatorEnabled ? 1.0 : 0.0);
        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.FrameAllocationCount",
            frameAllocationCount);
        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.FrameStepUsedBytes",
            backingStepUsedBytes);
        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.FrameLifetimePeakBytes",
            backingLifetimePeakBytes);
        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.FrameCapacityBytes",
            backingCapacityBytes);
        profiler.AddCounter(
            "Physics.Solver.TemporaryAllocation.FrameUtilization",
            frameUtilization);
    }

    void Reset()
    {
        // Phase ④の計測値はArenaをResetする前に送ります。
        // FrameAllocator::Reset()後ではUsedMemoryが0へ戻るため、Stepごとの実Arena消費量を失ってしまいます。
        SubmitProfilerCounters();

        // Frame/Linear系Allocatorでは前Stepの一時領域を次Step開始時に一括再利用します。
        // Reset()はStep開始時に呼ばれるため、前StepのローカルContainerは既に破棄済みです。
        // したがってContainer生存中にArenaを巻き戻す危険はありません。
        if (m_BackingAllocator != nullptr)
        {
            m_BackingAllocator->Reset();
        }

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

private:
    // 所有権は持ちません。SoftBodySolverなど呼び出し側がBacking Allocatorを所有します。
    Allocator* m_BackingAllocator = nullptr;
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
// Before計測ではBacking Allocatorを設定せず通常std::allocatorを使用します。
// Phase ③ではStatistics側へFrameAllocatorを登録し、同じコンテナ型・同じCounterのまま
// 実確保元だけをArenaへ差し替えます。
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
        // 個々のContainer生成時にBackingを毎回渡さなくても、Statisticsへ登録された
        // Solver共通Arenaを自動継承します。明示Backingがある場合はそちらを優先します。
        if (m_BackingAllocator == nullptr && m_Statistics != nullptr)
        {
            m_BackingAllocator = m_Statistics->GetBackingAllocator();
        }
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
                // FrameAllocator容量超過を通常Heapへ黙ってfallbackさせると、After計測で
                // 「Heap allocation 0」を保証できなくなります。容量不足は明示的に失敗させ、
                // Peak計測を基にArena容量を調整する方針とします。
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
