#include "Raven/Core/Containers/Tests/FlatHashMapSelfTests.h"
#include "Raven/Core/Containers/FlatHashMap.h"
#include "Raven/Core/Memory/FrameAllocator.h"
#include <cassert>
#include <memory>
#include <string>
#include <utility>

namespace Raven::tests
{
namespace
{
struct ConstantHasher
{
    std::size_t operator()(int) const { return 0; }
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
    std::size_t visited = 0;
    for (auto& pair : map)
    {
        ++visited;
        pair.second += "!";
    }
    assert(visited == map.size());
    const auto& readonly = map;
    assert(*readonly.find(4) == "4!");
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

    FlatHashMap<int, std::unique_ptr<int>> moveOnly(allocator);
    assert(moveOnly.emplace(1, std::make_unique<int>(42)).second);
    moveOnly.reserve(64);
    assert(**moveOnly.find(1) == 42);
}
}
