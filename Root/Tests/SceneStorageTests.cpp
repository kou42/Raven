// GPU/Windowを必要としないSG0 Sparse Set回帰テスト。
#include "Raven/Scene/ComponentStorage.h"

#include <cassert>
#include <string>
#include <vector>

namespace
{
struct ThrowingComponent
{
    static bool ThrowOnConstruct;
    static bool ThrowOnAssign;
    std::string Value;

    explicit ThrowingComponent(const char* value) : Value(value)
    {
        if (ThrowOnConstruct == true)
        {
            throw std::runtime_error("construction failure");
        }
    }
    ThrowingComponent(const ThrowingComponent&) = default;
    ThrowingComponent(ThrowingComponent&&) noexcept = default;
    ThrowingComponent& operator=(ThrowingComponent&& other)
    {
        if (ThrowOnAssign == true)
        {
            throw std::runtime_error("assignment failure");
        }
        Value = std::move(other.Value);
        return *this;
    }
};
bool ThrowingComponent::ThrowOnConstruct = false;
bool ThrowingComponent::ThrowOnAssign = false;
}

int main()
{
    Raven::ComponentStorage<std::string> storage;
    storage.Emplace(7, "seven");
    storage.Emplace(21, "twenty-one");
    storage.Emplace(42, "forty-two");
    assert(storage.Remove(21) == true);
    assert(storage.Size() == 2u);
    assert(storage.Has(21) == false);
    assert(storage.GetEntityByIndex(1) == 42u);
    assert(storage.Get(42) == "forty-two");
    assert(storage.Remove(21) == false);
    std::size_t count = 0;
    for (const std::string& value : storage)
    {
        assert(value.empty() == false);
        ++count;
    }
    assert(count == 2u);
    const auto& constStorage = storage;
    assert(*constStorage.cbegin() == "seven");
    count = 0;
    for (const std::string& value : constStorage)
    {
        assert(value.empty() == false);
        ++count;
    }
    assert(count == 2u);
    assert(constStorage.cbegin() != constStorage.cend());
    storage.Reserve(128);
    storage.Emplace(21, "reused");
    assert(storage.Get(21) == "reused");
    storage.Clear();
    assert(storage.Empty() == true);
    assert(storage.Has(7) == false);
    assert(storage.Has(42) == false);
    storage.Emplace(42, "after clear");
    assert(storage.Get(42) == "after clear");

    // move-onlyな所有Componentを再配置・削除しても所有権を保ちます。
    Raven::ComponentStorage<std::unique_ptr<int>> owned;
    owned.Emplace(1, std::make_unique<int>(10));
    owned.Emplace(2, std::make_unique<int>(20));
    owned.Reserve(128);
    assert(owned.Remove(1) == true);
    assert(*owned.Get(2) == 20);
    owned.Clear();
    assert(owned.Empty() == true);

    Raven::ComponentStorage<ThrowingComponent> throwing;
    throwing.Emplace(7, "first");
    throwing.Emplace(42, "last");
    ThrowingComponent::ThrowOnConstruct = true;
    bool caught = false;
    try
    {
        throwing.Emplace(99, "failed");
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }
    assert(caught == true);
    assert(throwing.Size() == 2u);
    assert(throwing.Has(99) == false);
    assert(throwing.Get(7).Value == "first");
    ThrowingComponent::ThrowOnConstruct = false;
    ThrowingComponent::ThrowOnAssign = true;
    caught = false;
    try
    {
        throwing.Remove(7);
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }
    assert(caught == true);
    assert(throwing.Size() == 2u);
    assert(throwing.Has(7) == true);
    assert(throwing.Has(42) == true);
    assert(throwing.GetEntityByIndex(0) == 7u);
    assert(throwing.GetEntityByIndex(1) == 42u);
    assert(throwing.Get(42).Value == "last");
    ThrowingComponent::ThrowOnAssign = false;
    assert(throwing.Remove(7) == true);
    assert(throwing.Get(42).Value == "last");
    assert(throwing.Remove(42) == true);
    assert(throwing.Empty() == true);
}
