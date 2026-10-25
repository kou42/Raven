#include "Raven/Core/Containers/Tests/FlatHashSetSelfTests.h"

#include "Raven/Core/Containers/FlatHashSet.h"
#include "Raven/Core/Memory/FrameAllocator.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

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

// Slot自体のalignmentに従ってAllocatorが確保することを検証します。
struct alignas(64) AlignedKey
{
    int Value = 0;
    bool operator==(const AlignedKey& other) const noexcept { return Value == other.Value; }
};

struct AlignedHasher
{
    std::size_t operator()(const AlignedKey& key) const noexcept
    {
        return static_cast<std::size_t>(key.Value);
    }
};

// コピー中に例外を発生させ、Rehashのロールバックを検証します。
struct ThrowingKey
{
    int Value = 0;
    static inline int CopiesUntilThrow = -1;

    ThrowingKey() = default;
    explicit ThrowingKey(int value) : Value(value) {}
    ThrowingKey(const ThrowingKey& other) : Value(other.Value)
    {
        if (CopiesUntilThrow == 0)
        {
            throw std::runtime_error("intentional copy failure");
        }
        if (CopiesUntilThrow > 0)
        {
            --CopiesUntilThrow;
        }
    }
    ThrowingKey(ThrowingKey&& other) noexcept(false) : Value(other.Value) {}
    bool operator==(const ThrowingKey& other) const noexcept { return Value == other.Value; }
};

struct ThrowingHasher
{
    static inline int HashesUntilThrow = -1;

    std::size_t operator()(const ThrowingKey& key) const
    {
        if (HashesUntilThrow == 0)
        {
            throw std::runtime_error("intentional hash failure");
        }
        if (HashesUntilThrow > 0)
        {
            --HashesUntilThrow;
        }
        return static_cast<std::size_t>(key.Value);
    }
};

// Rehash時にCopyできないKeyでも、noexcept moveなら安全に再配置できることを検証します。
struct MoveOnlyKey
{
    int Value = 0;

    MoveOnlyKey() = default;
    explicit MoveOnlyKey(int value) : Value(value) {}
    MoveOnlyKey(const MoveOnlyKey&) = delete;
    MoveOnlyKey& operator=(const MoveOnlyKey&) = delete;
    MoveOnlyKey(MoveOnlyKey&& other) noexcept : Value(other.Value)
    {
        other.Value = -1;
    }
    MoveOnlyKey& operator=(MoveOnlyKey&& other) noexcept
    {
        if (this != &other)
        {
            Value = other.Value;
            other.Value = -1;
        }
        return *this;
    }
    bool operator==(const MoveOnlyKey& other) const noexcept { return Value == other.Value; }
};

struct MoveOnlyHasher
{
    std::size_t operator()(const MoveOnlyKey& key) const noexcept
    {
        return static_cast<std::size_t>(key.Value);
    }
};

} // namespace

void RunFlatHashSetSelfTests()
{
    // ========================================================================
    // 1. reserve + insert + duplicate
    // ========================================================================
    // 再配置とover-aligned Keyの追加テスト分を含め、Arena容量に余裕を持たせます。
    FrameAllocator allocator(65536);
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
    // 4. Iterator / const Iterator / erase(iterator)
    // ========================================================================
    // Tombstoneを飛ばし、すべてのOccupied Keyを一度だけ列挙することを確認します。
    std::size_t visited = 0;
    int sum = 0;
    for (const int key : values)
    {
        ++visited;
        sum += key;
    }
    assert(visited == values.size());
    assert(sum == 120);

    const auto& constValues = values;
    std::size_t constVisited = 0;
    for (auto it = constValues.cbegin(); it != constValues.cend(); ++it)
    {
        ++constVisited;
    }
    assert(constVisited == values.size());

    for (auto it = values.begin(); it != values.end();)
    {
        if ((*it % 2) == 0)
        {
            it = values.erase(it);
        }
        else
        {
            ++it;
        }
    }
    assert(values.size() == 8);
    for (int key = 0; key < 16; ++key)
    {
        assert(values.contains(key) == ((key % 2) != 0));
    }
    assert(values.erase(values.end()) == values.end());

    // ========================================================================
    // 5. Tombstone整理 / Probe統計 / Adversarial Hash
    // ========================================================================
    assert(collisions.tombstone_count() == 1);
    const auto beforeCleanup = collisions.GetProbeStatistics();
    assert(beforeCleanup.OccupiedCount == 2);
    assert(beforeCleanup.MaximumProbeLength >= 3);
    collisions.cleanup_tombstones();
    assert(collisions.tombstone_count() == 0);
    assert(collisions.contains(10));
    assert(collisions.contains(30));
    const auto afterCleanup = collisions.GetProbeStatistics();
    assert(afterCleanup.OccupiedCount == 2);
    assert(afterCleanup.MaximumProbeLength <= beforeCleanup.MaximumProbeLength);

    // ConstantHasherに多数の異なるKeyを与えて、最悪衝突時の探索を確認します。
    FlatHashSet<int, ConstantHasher> adversarial(allocator);
    adversarial.reserve(32);
    for (int key = 0; key < 32; ++key)
    {
        assert(adversarial.insert(key).second);
    }
    assert(adversarial.GetProbeStatistics().MaximumProbeLength == 32);
    for (int key = 0; key < 32; ++key)
    {
        assert(adversarial.contains(key));
    }

    // ========================================================================
    // 6. Over-aligned Key
    // ========================================================================
    FlatHashSet<AlignedKey, AlignedHasher> aligned(allocator);
    aligned.reserve(2);
    const auto alignedResult = aligned.insert(AlignedKey{42});
    assert(alignedResult.second);
    assert(reinterpret_cast<std::uintptr_t>(alignedResult.first) % alignof(AlignedKey) == 0);
    assert(aligned.contains(AlignedKey{42}));

    // ========================================================================
    // 7. Rehash例外時の強い保証
    // ========================================================================
    FlatHashSet<ThrowingKey, ThrowingHasher> throwing(allocator);
    throwing.reserve(2);
    assert(throwing.insert(ThrowingKey{1}).second);
    assert(throwing.insert(ThrowingKey{2}).second);
    const std::size_t oldCapacity = throwing.capacity();

    // Hasherが移動先計算中に失敗しても、公開中のSlot配列には触れません。
    ThrowingHasher::HashesUntilThrow = 1;
    bool hashFailureCaught = false;
    try
    {
        throwing.reserve(32);
    }
    catch (const std::runtime_error&)
    {
        hashFailureCaught = true;
    }
    ThrowingHasher::HashesUntilThrow = -1;
    assert(hashFailureCaught);
    assert(throwing.capacity() == oldCapacity);
    assert(throwing.size() == 2);
    assert(throwing.contains(ThrowingKey{1}));
    assert(throwing.contains(ThrowingKey{2}));

    // Copy途中の失敗でも、構築済みの新Keyだけを破棄して旧テーブルを維持します。
    ThrowingKey::CopiesUntilThrow = 1;
    bool caught = false;
    try
    {
        throwing.reserve(32);
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }
    ThrowingKey::CopiesUntilThrow = -1;
    assert(caught);
    assert(throwing.capacity() == oldCapacity);
    assert(throwing.size() == 2);
    assert(throwing.contains(ThrowingKey{1}));
    assert(throwing.contains(ThrowingKey{2}));
    throwing.reserve(32);
    assert(throwing.contains(ThrowingKey{1}));
    assert(throwing.contains(ThrowingKey{2}));

    // ========================================================================
    // 8. Move-only Key / 大量の削除・再挿入
    // ========================================================================
    FrameAllocator moveOnlyAllocator(262144);
    FlatHashSet<MoveOnlyKey, MoveOnlyHasher> moveOnly(moveOnlyAllocator);
    for (int key = 0; key < 64; ++key)
    {
        assert(moveOnly.insert(MoveOnlyKey{key}).second);
    }
    moveOnly.reserve(256);
    assert(moveOnly.size() == 64);
    for (int key = 0; key < 64; ++key)
    {
        assert(moveOnly.contains(MoveOnlyKey{key}));
    }

    FrameAllocator stressAllocator(2 * 1024 * 1024);
    FlatHashSet<int> stress(stressAllocator);
    stress.reserve(2048);
    for (int key = 0; key < 2048; ++key)
    {
        assert(stress.insert(key).second);
    }
    for (int cycle = 0; cycle < 16; ++cycle)
    {
        for (int key = 0; key < 2048; key += 2)
        {
            assert(stress.erase(key));
        }
        for (int key = 0; key < 2048; key += 2)
        {
            assert(stress.insert(key).second);
        }
        assert(stress.size() == 2048);
        assert(stress.tombstone_count() == 0);
    }

    // ========================================================================
    // 9. 空コンテナ / 容量境界 / reserveオーバーフロー
    // ========================================================================
    FrameAllocator boundaryAllocator(4096);
    FlatHashSet<int> boundary(boundaryAllocator);
    assert(boundary.empty());
    assert(boundary.begin() == boundary.end());
    assert(boundary.cbegin() == boundary.cend());
    assert(boundary.find(1) == nullptr);
    assert(boundary.erase(1) == false);
    boundary.reserve(0);
    assert(boundary.capacity() >= 8);

    bool overflowCaught = false;
    try
    {
        boundary.reserve((std::numeric_limits<std::size_t>::max)());
    }
    catch (const std::bad_alloc&)
    {
        overflowCaught = true;
    }
    assert(overflowCaught);
    assert(boundary.empty());
    assert(boundary.capacity() >= 8);

    // ========================================================================
    // 10. clear
    // ========================================================================
    collisions.clear();
    assert(collisions.empty());
    assert(collisions.contains(10) == false);
    assert(collisions.contains(30) == false);
}

} // namespace Raven::tests
