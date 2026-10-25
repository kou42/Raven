#include "Raven/Core/Containers/Tests/FlatHashMapSelfTests.h"
#include "Raven/Core/Containers/FlatHashMap.h"
#include "Raven/Core/Memory/FrameAllocator.h"
#include <cassert>
#include <memory>
#include <limits>
#include <stdexcept>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

namespace Raven::tests
{
namespace
{
struct alignas(64) AlignedValue { int Value = 0; };
struct ThrowingHasher
{
    static inline bool ShouldThrow = false;
    std::size_t operator()(int key) const
    {
        if (ShouldThrow == true) { throw std::runtime_error("hash failure"); }
        return std::hash<int>{}(key);
    }
};
struct ConstantHasher
{
    std::size_t operator()(int) const { return 0; }
};
struct ThrowingEqual
{
    static inline bool ShouldThrow = false;
    bool operator()(int left, int right) const
    {
        if (ShouldThrow == true)
        {
            throw std::runtime_error("equality failure");
        }
        return left == right;
    }
};
struct ThrowingKey
{
    static inline int CopiesBeforeThrow = -1;
    int Value = 0;

    explicit ThrowingKey(int value = 0) : Value(value) {}
    ThrowingKey(const ThrowingKey& other) : Value(other.Value)
    {
        if (CopiesBeforeThrow == 0)
        {
            throw std::runtime_error("key copy failure");
        }
        if (CopiesBeforeThrow > 0)
        {
            --CopiesBeforeThrow;
        }
    }
    ThrowingKey(ThrowingKey&&) noexcept = default;
    ThrowingKey& operator=(const ThrowingKey&) = default;
    ThrowingKey& operator=(ThrowingKey&&) noexcept = default;
};
struct ThrowingKeyHasher
{
    std::size_t operator()(const ThrowingKey& key) const noexcept
    {
        return std::hash<int>{}(key.Value);
    }
};
struct ThrowingKeyEqual
{
    bool operator()(const ThrowingKey& left, const ThrowingKey& right) const noexcept
    {
        return left.Value == right.Value;
    }
};
struct ThrowingValue
{
    static inline int CopiesBeforeThrow = -1;
    int Value = 0;

    explicit ThrowingValue(int value = 0) : Value(value) {}
    ThrowingValue(const ThrowingValue& other) : Value(other.Value)
    {
        if (CopiesBeforeThrow == 0)
        {
            throw std::runtime_error("value copy failure");
        }
        if (CopiesBeforeThrow > 0)
        {
            --CopiesBeforeThrow;
        }
    }
    ThrowingValue(ThrowingValue&&) noexcept = default;
    ThrowingValue& operator=(const ThrowingValue&) = default;
    ThrowingValue& operator=(ThrowingValue&&) noexcept = default;
};
}
void RunFlatHashMapSelfTests()
{
    FrameAllocator allocator(1024 * 1024);
    FlatHashMap<int, std::string, ConstantHasher> map(allocator);
    assert(map.empty());
    assert(map.find(1) == nullptr);
    map.reserve(32);
    for (int i = 0; i < 32; ++i)
    {
        assert(map.emplace(i, std::to_string(i)).second);
    }
    assert(map.size() == 32);
    assert(map.emplace(4, "duplicate").second == false);
    assert(*map.find(4) == "4");
    assert(map.erase(3));
    assert(map.contains(31));
    assert(map.contains(3) == false);
    assert(map.emplace(33, "33").second);
    FlatHashMap<int, std::string, ConstantHasher>::value_type insertedValue{ 35, "35" };
    assert(map.insert(insertedValue).second);
    assert(map.insert({ 37, "37" }).second);
    std::size_t visited = 0;
    for (auto& pair : map)
    {
        ++visited;
        pair.second += "!";
    }
    assert(visited == map.size());
    const auto& readonly = map;
    assert(*readonly.find(4) == "4!");
    std::size_t constVisited = 0;
    for (const auto& pair : readonly)
    {
        assert(pair.second.empty() == false);
        ++constVisited;
    }
    assert(constVisited == readonly.size());
    const auto probeStatistics = readonly.GetProbeStatistics();
    assert(probeStatistics.OccupiedCount == readonly.size());
    assert(probeStatistics.MaximumProbeLength > 1);
    assert(probeStatistics.CollidedCount > 0);
    for (auto it = map.begin(); it != map.end();)
    {
        if (it->first % 2 == 0) { it = map.erase(it); }
        else { ++it; }
    }
    map.reserve(128);
    assert(map.contains(31));
    assert(map.contains(4) == false);
    map.clear();
    assert(map.empty());
    assert(map.begin() == map.end());
    assert(map.emplace(41, "41").second);

    FlatHashMap<int, std::string, ConstantHasher> moved(std::move(map));
    assert(map.empty());
    assert(moved.contains(41));
    FlatHashMap<int, std::string, ConstantHasher> moveAssigned(allocator);
    assert(moveAssigned.emplace(99, "discarded").second);
    moveAssigned = std::move(moved);
    assert(moved.empty());
    assert(moveAssigned.contains(41));
    assert(moveAssigned.contains(99) == false);

    FlatHashMap<int, int, ConstantHasher> tombstoneMap(allocator);
    tombstoneMap.reserve(16);
    for (int index = 0; index < 16; ++index)
    {
        assert(tombstoneMap.emplace(index, index * 2).second);
    }
    for (int index = 0; index < 16; index += 2)
    {
        assert(tombstoneMap.erase(index));
    }
    assert(tombstoneMap.tombstone_count() == 8);
    tombstoneMap.cleanup_tombstones();
    assert(tombstoneMap.tombstone_count() == 0);
    assert(tombstoneMap.size() == 8);
    for (int index = 1; index < 16; index += 2)
    {
        assert(*tombstoneMap.find(index) == index * 2);
    }

    FlatHashMap<int, std::unique_ptr<int>> moveOnly(allocator);
    assert(moveOnly.emplace(1, std::make_unique<int>(42)).second);
    moveOnly.reserve(64);
    assert(**moveOnly.find(1) == 42);

    // move-only ValueではHasher例外を全要素の移動前に評価し、旧Valueを保護します。
    FlatHashMap<int, std::unique_ptr<int>, ThrowingHasher> moveOnlyThrowing(allocator);
    assert(moveOnlyThrowing.emplace(1, std::make_unique<int>(10)).second);
    assert(moveOnlyThrowing.emplace(2, std::make_unique<int>(20)).second);
    ThrowingHasher::ShouldThrow = true;
    bool caught = false;
    try { moveOnlyThrowing.reserve(128); }
    catch (const std::runtime_error&) { caught = true; }
    ThrowingHasher::ShouldThrow = false;
    assert(caught);
    assert(**moveOnlyThrowing.find(1) == 10);
    assert(**moveOnlyThrowing.find(2) == 20);

    // over-aligned Valueの配置保証と、reserve失敗時の旧データ維持を検証します。
    FlatHashMap<int, AlignedValue> aligned(allocator);
    assert(aligned.emplace(7, AlignedValue{19}).second);
    assert(reinterpret_cast<std::uintptr_t>(aligned.find(7)) % alignof(AlignedValue) == 0);

    FlatHashMap<int, std::string, ThrowingHasher> throwing(allocator);
    assert(throwing.emplace(1, "one").second);
    const std::size_t previousCapacity = throwing.capacity();
    ThrowingHasher::ShouldThrow = true;
    caught = false;
    try { throwing.reserve(128); }
    catch (const std::runtime_error&) { caught = true; }
    ThrowingHasher::ShouldThrow = false;
    assert(caught);
    assert(throwing.capacity() == previousCapacity);
    assert(*throwing.find(1) == "one");

    FlatHashMap<int, int, ConstantHasher, ThrowingEqual> throwingEqual(allocator);
    assert(throwingEqual.emplace(1, 10).second);
    ThrowingEqual::ShouldThrow = true;
    caught = false;
    try { throwingEqual.emplace(2, 20); }
    catch (const std::runtime_error&) { caught = true; }
    ThrowingEqual::ShouldThrow = false;
    assert(caught);
    assert(throwingEqual.size() == 1);
    assert(*throwingEqual.find(1) == 10);

    // Value copyが途中で例外を送出しても、移動元の要素と容量を維持します。
    FlatHashMap<int, ThrowingValue> throwingValue(allocator);
    assert(throwingValue.emplace(1, ThrowingValue{ 10 }).second);
    assert(throwingValue.emplace(2, ThrowingValue{ 20 }).second);
    const std::size_t valueCapacity = throwingValue.capacity();
    ThrowingValue::CopiesBeforeThrow = 1;
    caught = false;
    try { throwingValue.reserve(128); }
    catch (const std::runtime_error&) { caught = true; }
    ThrowingValue::CopiesBeforeThrow = -1;
    assert(caught);
    assert(throwingValue.capacity() == valueCapacity);
    assert(throwingValue.size() == 2);
    assert(throwingValue.find(1)->Value == 10);
    assert(throwingValue.find(2)->Value == 20);

    FlatHashMap<ThrowingKey, int, ThrowingKeyHasher, ThrowingKeyEqual> throwingKey(allocator);
    assert(throwingKey.emplace(ThrowingKey{ 1 }, 10).second);
    assert(throwingKey.emplace(ThrowingKey{ 2 }, 20).second);
    const std::size_t keyCapacity = throwingKey.capacity();
    ThrowingKey::CopiesBeforeThrow = 1;
    caught = false;
    try { throwingKey.reserve(128); }
    catch (const std::runtime_error&) { caught = true; }
    ThrowingKey::CopiesBeforeThrow = -1;
    assert(caught);
    assert(throwingKey.capacity() == keyCapacity);
    assert(*throwingKey.find(ThrowingKey{ 1 }) == 10);
    assert(*throwingKey.find(ThrowingKey{ 2 }) == 20);

    // 空Slotへの構築失敗ではSizeと検索結果を変化させません。
    FlatHashMap<int, ThrowingValue> constructionFailure(allocator);
    ThrowingValue source{ 30 };
    ThrowingValue::CopiesBeforeThrow = 0;
    caught = false;
    try { constructionFailure.emplace(3, source); }
    catch (const std::runtime_error&) { caught = true; }
    ThrowingValue::CopiesBeforeThrow = -1;
    assert(caught);
    assert(constructionFailure.empty());
    assert(constructionFailure.find(3) == nullptr);

    bool overflowCaught = false;
    try { aligned.reserve((std::numeric_limits<std::size_t>::max)()); }
    catch (const std::bad_alloc&) { overflowCaught = true; }
    assert(overflowCaught);
    assert(aligned.contains(7));

    // 同じ決定的操作列を標準Mapにも適用し、Growth・削除・再挿入の複合回帰を確認します。
    FrameAllocator randomAllocator(1024 * 1024);
    FlatHashMap<int, int> randomMap(randomAllocator);
    std::unordered_map<int, int> standardMap;
    std::uint32_t randomState = 0x9e3779b9u;
    for (std::size_t operation = 0; operation < 4096; ++operation)
    {
        randomState = randomState * 1664525u + 1013904223u;
        const int key = static_cast<int>((randomState >> 8) % 257u);
        const int value = static_cast<int>(operation);
        switch (randomState & 3u)
        {
        case 0:
        case 1:
        {
            const auto ravenResult = randomMap.emplace(key, value);
            const auto standardResult = standardMap.emplace(key, value);
            assert(ravenResult.second == standardResult.second);
            assert(*ravenResult.first == standardResult.first->second);
            break;
        }
        case 2:
            assert(randomMap.erase(key) == (standardMap.erase(key) != 0));
            break;
        case 3:
        {
            const int* ravenValue = randomMap.find(key);
            const auto standardValue = standardMap.find(key);
            assert((ravenValue == nullptr) == (standardValue == standardMap.end()));
            if (ravenValue != nullptr)
            {
                assert(*ravenValue == standardValue->second);
            }
            break;
        }
        }
    }
    assert(randomMap.size() == standardMap.size());
    for (const auto& pair : standardMap)
    {
        assert(randomMap.contains(pair.first));
        assert(*randomMap.find(pair.first) == pair.second);
    }
}
}
