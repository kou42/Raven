#include <cassert>
#include <cstddef>
#include <vector>

#include "Raven/Core/Memory/FrameAllocator.h"
#include "Raven/Core/Memory/FrameVector.h"
#include "Raven/Physics/Collision/BroadPhase.h"
#include "Raven/Physics/Solver/SolverTemporaryAllocationCounter.h"

namespace Raven::ph::tests
{

// ============================================================================
// Physics Frame Allocator Self Tests
// ============================================================================
// BroadPhaseとSolver Temporaryの両経路について、FrameAllocatorへの確保、統計、Resetを確認します。
void RunPhysicsFrameAllocatorSelfTests()
{
    constexpr std::size_t Capacity = 4u * 1024u;
    FrameAllocator allocator(Capacity);

    using PairVector = FrameVector<BroadPhasePair>;
    PairVector pairs{ STLAllocatorAdapter<BroadPhasePair>(allocator) };

    assert(allocator.GetCapacity() == Capacity);
    assert(allocator.GetUsedMemory() == 0u);
    assert(allocator.GetAllocationCount() == 0u);

    for (uint32_t i = 0u; i < 32u; ++i)
    {
        BroadPhasePair pair{};
        pairs.push_back(pair);
    }

    assert(pairs.size() == 32u);
    assert(allocator.GetUsedMemory() > 0u);
    assert(allocator.GetAllocationCount() > 0u);

    const std::size_t usedBeforeReset = allocator.GetUsedMemory();
    const std::size_t peakBeforeReset = allocator.GetPeakUsedMemory();
    const std::size_t allocationsBeforeReset = allocator.GetAllocationCount();

    assert(peakBeforeReset >= usedBeforeReset);
    assert(allocationsBeforeReset > 0u);

    pairs.clear();
    pairs.shrink_to_fit();

    allocator.ResetFrame();

    assert(allocator.GetUsedMemory() == 0u);
    assert(allocator.GetAllocationCount() == 0u);
    assert(allocator.GetPeakUsedMemory() == peakBeforeReset);

    // ========================================================================
    // Solver Temporary Allocator: Phase ② Before計測
    // ========================================================================
    // Backingを持たないStatisticsから作ったAllocatorは通常Heapを使用します。
    SolverTemporaryAllocationStatistics heapStatistics{};
    using TemporaryIntAllocator = SolverTemporaryAllocator<int>;

    {
        std::vector<int, TemporaryIntAllocator> temporaryValues{
            TemporaryIntAllocator(&heapStatistics)
        };

        temporaryValues.reserve(64u);
        for (int value = 0; value < 64; ++value)
        {
            temporaryValues.push_back(value);
        }

        assert(temporaryValues.size() == 64u);
        assert(heapStatistics.AllocationCount > 0u);
        assert(heapStatistics.AllocationBytes >= 64u * sizeof(int));
        assert(heapStatistics.ActiveBytes > 0u);
        assert(heapStatistics.PeakActiveBytes >= heapStatistics.ActiveBytes);
        assert(heapStatistics.GetBackingAllocator() == nullptr);
        assert(heapStatistics.GetBackingUsedMemory() == 0u);
    }

    assert(heapStatistics.DeallocationCount > 0u);
    assert(heapStatistics.ActiveBytes == 0u);
    assert(heapStatistics.AllocationBytes == heapStatistics.DeallocationBytes);

    // ========================================================================
    // Solver Temporary Allocator: Phase ③ FrameAllocator経路
    // ========================================================================
    // Phase ③ではBackingを各STL Containerへ個別に渡しません。
    // Solver所有Statisticsへ1回登録し、unordered_mapのrebindを含む全Allocatorが同じArenaを継承します。
    FrameAllocator solverFrameAllocator(Capacity);
    SolverTemporaryAllocationStatistics frameStatistics{ &solverFrameAllocator };

    {
        std::vector<int, TemporaryIntAllocator> temporaryValues{
            TemporaryIntAllocator(&frameStatistics)
        };

        temporaryValues.reserve(64u);
        for (int value = 0; value < 64; ++value)
        {
            temporaryValues.push_back(value);
        }

        assert(temporaryValues.size() == 64u);
        assert(frameStatistics.AllocationCount > 0u);
        assert(frameStatistics.AllocationBytes >= 64u * sizeof(int));
        assert(frameStatistics.GetBackingAllocator() == &solverFrameAllocator);
        assert(frameStatistics.GetBackingCapacity() == Capacity);
        assert(frameStatistics.GetBackingAllocationCount() > 0u);
        assert(frameStatistics.GetBackingUsedMemory() > 0u);
        assert(frameStatistics.GetBackingPeakUsedMemory() >= frameStatistics.GetBackingUsedMemory());
    }

    // FrameAllocator::Deallocate()はno-opですが、STL ContainerのLifetime終了はCounterへ記録されます。
    assert(frameStatistics.DeallocationCount > 0u);
    assert(frameStatistics.ActiveBytes == 0u);
    assert(frameStatistics.GetBackingUsedMemory() > 0u);

    // Statistics::Reset()がStep境界のArena Resetも担当します。
    // Containerがscopeを抜けた後に呼ぶことが重要です。
    const std::size_t solverFramePeak = frameStatistics.GetBackingPeakUsedMemory();
    frameStatistics.Reset();

    assert(frameStatistics.AllocationCount == 0u);
    assert(frameStatistics.AllocationBytes == 0u);
    assert(frameStatistics.GetBackingUsedMemory() == 0u);
    assert(frameStatistics.GetBackingAllocationCount() == 0u);
    assert(frameStatistics.GetBackingPeakUsedMemory() == solverFramePeak);
}

} // namespace Raven::ph::tests
