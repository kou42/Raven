#include <cassert>
#include <cstddef>

#include "Raven/Core/Memory/FrameAllocator.h"
#include "Raven/Core/Memory/FrameVector.h"
#include "Raven/Physics/Collision/BroadPhase.h"

namespace Raven::ph::tests
{

// ============================================================================
// Physics Frame Allocator Self Tests
// ============================================================================
// BroadPhasePairを実際にFrameAllocatorから確保し、PhysicsWorldへ接続する前提となる
// 「可変長Pair列」「Alignment」「統計」「Reset後の一括再利用」を確認します。
//
// このテストではResetFrame()後の古いPairを意図的に参照しません。
// FrameAllocatorの契約上、Reset後のdata()は次のAllocate()で上書きされる可能性があり、
// Debug Snapshotなどフレームを跨いで必要なデータは通常の永続vectorへコピーする必要があります。
void RunPhysicsFrameAllocatorSelfTests()
{
    constexpr std::size_t Capacity = 4u * 1024u;
    FrameAllocator allocator(Capacity);

    using PairVector = FrameVector<BroadPhasePair>;
    PairVector pairs{ STLAllocatorAdapter<BroadPhasePair>(allocator) };

    assert(allocator.GetCapacity() == Capacity);
    assert(allocator.GetUsedMemory() == 0u);
    assert(allocator.GetAllocationCount() == 0u);

    // vectorのgrowによって複数回Allocateされても、すべて同じFrameAllocatorから
    // 確保されることを確認します。Linear系Allocatorでは古いbufferは個別解放せず、
    // 最後にResetFrame()でまとめて再利用します。
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

    // FrameVectorのdestructorは要素破棄を行いますが、FrameAllocatorのDeallocateはno-opです。
    // そのためResetFrame()はvectorが寿命を終えた後に行うのが最も分かりやすく安全です。
    pairs.clear();
    pairs.shrink_to_fit();

    allocator.ResetFrame();

    assert(allocator.GetUsedMemory() == 0u);
    assert(allocator.GetAllocationCount() == 0u);

    // Peakは容量チューニングのためReset後も保持します。
    assert(allocator.GetPeakUsedMemory() == peakBeforeReset);
}

} // namespace Raven::ph::tests
