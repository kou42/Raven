#include "Raven/Core/Containers/Tests/FlatHashSetSelfTests.h"

#include "Raven/Core/Containers/FlatHashSet.h"
#include "Raven/Core/Memory/FrameAllocator.h"

#include <cassert>
#include <cstddef>

namespace Raven::tests
{
namespace
{

// すべて同じHash値を返し、Linear Probingの衝突処理を確実に通すためのHasherです。
struct ConstantHasher
{
    std::size_t operator()(int) const noexcept
    {
        return 1;
    }
};

} // namespace

void RunFlatHashSetSelfTests()
{
    // ========================================================================
    // 1. reserve + insert + duplicate
    // ========================================================================
    FrameAllocator allocator(4096);
    FlatHashSet<int> values(allocator);

    values.reserve(16);
    const std::size_t allocationCountAfterReserve = allocator.GetAllocationCount();

    assert(values.capacity() >= 16);
    assert(values.empty());

    for (int value = 0; value < 16; ++value)
    {
        const auto result = values.insert(value);
        assert(result.second);
        assert(result.first != nullptr);
        assert(*result.first == value);
    }

    // reserve容量内のinsertでは追加allocationが発生しないことを確認します。
    // FrameAllocatorではこの性質がArena消費量を抑える上で特に重要です。
    assert(allocator.GetAllocationCount() == allocationCountAfterReserve);
    assert(values.size() == 16);

    const auto duplicate = values.insert(7);
    assert(duplicate.second == false);
    assert(values.size() == 16);

    // ========================================================================
    // 2. find / contains / erase / Tombstone再利用
    // ========================================================================
    assert(values.contains(0));
    assert(values.contains(15));
    assert(values.contains(100) == false);

    assert(values.erase(7));
    assert(values.contains(7) == false);
    assert(values.size() == 15);

    const auto reused = values.insert(7);
    assert(reused.second);
    assert(values.contains(7));
    assert(values.size() == 16);

    // ========================================================================
    // 3. Hash衝突時のLinear Probing
    // ========================================================================
    FlatHashSet<int, ConstantHasher> collisions(allocator);
    collisions.reserve(8);

    assert(collisions.insert(10).second);
    assert(collisions.insert(20).second);
    assert(collisions.insert(30).second);
    assert(collisions.contains(10));
    assert(collisions.contains(20));
    assert(collisions.contains(30));

    // Probe Chain途中の要素をeraseしてTombstoneにしても、その先のKeyをfindできる必要があります。
    assert(collisions.erase(20));
    assert(collisions.contains(10));
    assert(collisions.contains(30));

    // ========================================================================
    // 4. clear
    // ========================================================================
    collisions.clear();
    assert(collisions.empty());
    assert(collisions.contains(10) == false);
    assert(collisions.contains(30) == false);
}

} // namespace Raven::tests
