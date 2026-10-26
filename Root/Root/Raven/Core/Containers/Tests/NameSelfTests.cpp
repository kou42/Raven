#include "Raven/Core/Containers/Tests/NameSelfTests.h"

#include "Raven/Core/Containers/Name.h"
#include "Raven/Core/Memory/HeapAllocator.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <thread>
#include <type_traits>

namespace Raven::tests
{
namespace
{

std::uint64_t ConstantNameHash(StringView) noexcept
{
    return 1;
}

} // namespace

void RunNameSelfTests()
{
    static_assert(std::is_trivially_copyable_v<Name>);

    HeapAllocator allocator;
    {
        NameTable names(allocator);
        const Name none = names.Intern(StringView());
        assert(none.IsNone());
        assert(none.view().empty());
        assert(names.Contains(none));

        const Name idle = names.Intern(StringView("Idle"));
        const Name sameIdle = names.Intern(StringView("Idle"));
        const Name run = names.Intern(StringView("Run"));
        assert(idle == sameIdle);
        assert(idle != run);
        assert(idle.IsNone() == false);
        assert(idle.view() == StringView("Idle"));
        assert(names.Resolve(run) == StringView("Run"));
        assert(names.size() == 2);
        assert(names.GetAllocator() == &allocator);
        assert(NameHasher{}(idle) == NameHasher{}(sameIdle));

        FlatHashMap<Name, int, NameHasher> nameValues(allocator);
        assert(nameValues.emplace(idle, 10).second);
        assert(nameValues.emplace(run, 20).second);
        assert(nameValues.emplace(sameIdle, 30).second == false);
        assert(*nameValues.find(idle) == 10);

        // Entryを個別確保しているため、Table growth後も取得済みViewのaddressは維持されます。
        const StringView stableIdleView = idle.view();
        const char* stableIdleAddress = stableIdleView.data();
        for (std::size_t index = 0; index < 2048; ++index)
        {
            const std::string text = "GeneratedName" + std::to_string(index);
            static_cast<void>(names.Intern(StringView(text)));
        }
        assert(idle.view().data() == stableIdleAddress);
        assert(stableIdleView == StringView("Idle"));

        // 異なるTableのHandleはindexが一致しても同一視しません。
        NameTable otherNames(allocator);
        const Name otherIdle = otherNames.Intern(StringView("Idle"));
        assert(idle != otherIdle);
        assert(names.Contains(otherIdle) == false);

        // Hash一致だけでは同一視せず、chain内の元文字列比較で衝突を解決します。
        NameTable collisions(allocator, &ConstantNameHash);
        const Name collisionA = collisions.Intern(StringView("CollisionA"));
        const Name collisionB = collisions.Intern(StringView("CollisionB"));
        const Name collisionAAgain = collisions.Intern(StringView("CollisionA"));
        assert(collisionA != collisionB);
        assert(collisionA == collisionAAgain);
        assert(collisions.size() == 2);

        // NameTable内部のmutexがAllocatorとHash chainの更新をまとめて保護することを確認します。
        constexpr std::size_t ThreadCount = 4;
        constexpr std::size_t SharedNameCount = 128;
        std::array<std::thread, ThreadCount> workers;
        std::array<Name, ThreadCount> lastNames{};
        NameTable concurrentNames(allocator);
        for (std::size_t workerIndex = 0; workerIndex < ThreadCount; ++workerIndex)
        {
            workers[workerIndex] = std::thread([&concurrentNames, &lastNames, workerIndex]()
            {
                for (std::size_t nameIndex = 0; nameIndex < SharedNameCount; ++nameIndex)
                {
                    const std::string text = "SharedName" + std::to_string(nameIndex);
                    lastNames[workerIndex] = concurrentNames.Intern(StringView(text));
                }
            });
        }
        for (std::thread& worker : workers)
        {
            worker.join();
        }
        assert(concurrentNames.size() == SharedNameCount);
        for (std::size_t workerIndex = 1; workerIndex < ThreadCount; ++workerIndex)
        {
            assert(lastNames[workerIndex] == lastNames[0]);
        }
    }
    assert(allocator.GetAllocationCount() == 0);
}

} // namespace Raven::tests
