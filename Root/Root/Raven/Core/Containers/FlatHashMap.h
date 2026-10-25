#pragma once

#include "Raven/Core/Memory/Allocator.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>

namespace Raven
{
#if defined(_MSC_VER)
#define RAVEN_FLAT_HASH_MAP_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define RAVEN_FLAT_HASH_MAP_NOINLINE __attribute__((noinline))
#else
#define RAVEN_FLAT_HASH_MAP_NOINLINE
#endif

// Keyを変更するとHashテーブルの不変条件が崩れるため、pairのfirstはconstにします。
// Allocatorは外部所有であり、Mapより長生きする必要があります。
template<typename Key, typename Value, typename Hasher = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>>
class FlatHashMap
{
public:
    using value_type = std::pair<const Key, Value>;

private:
    enum class SlotState : std::uint8_t { Empty, Occupied, Tombstone };
    struct Slot
    {
        SlotState State = SlotState::Empty;
        alignas(value_type) unsigned char Storage[sizeof(value_type)];
        value_type* Get() { return std::launder(reinterpret_cast<value_type*>(Storage)); }
        const value_type* Get() const { return std::launder(reinterpret_cast<const value_type*>(Storage)); }
    };

public:
    template<bool IsConst>
    class Iterator
    {
        friend class FlatHashMap;
        template<bool> friend class Iterator;
        using Map = std::conditional_t<IsConst, const FlatHashMap, FlatHashMap>;
        Map* m_Map = nullptr;
        std::size_t m_Index = 0;
        Iterator(Map* map, std::size_t index) : m_Map(map), m_Index(index) { Skip(); }
        void Skip()
        {
            if (m_Map == nullptr) { return; }
            while (m_Index < m_Map->m_Capacity &&
                m_Map->m_Slots[m_Index].State != SlotState::Occupied) { ++m_Index; }
        }
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = typename FlatHashMap::value_type;
        using difference_type = std::ptrdiff_t;
        using pointer = std::conditional_t<IsConst, const value_type*, value_type*>;
        using reference = std::conditional_t<IsConst, const value_type&, value_type&>;
        Iterator() = default;
        template<bool B = IsConst, typename = std::enable_if_t<B>>
        Iterator(const Iterator<false>& other) : m_Map(other.m_Map), m_Index(other.m_Index) {}
        reference operator*() const { return *m_Map->m_Slots[m_Index].Get(); }
        pointer operator->() const { return &operator*(); }
        Iterator& operator++() { ++m_Index; Skip(); return *this; }
        Iterator operator++(int) { Iterator previous = *this; ++(*this); return previous; }
        template<bool Other>
        bool operator==(const Iterator<Other>& other) const
        { return m_Map == other.m_Map && m_Index == other.m_Index; }
        template<bool Other>
        bool operator!=(const Iterator<Other>& other) const { return (*this == other) == false; }
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    explicit FlatHashMap(Allocator& allocator, const Hasher& hasher = Hasher{},
        const KeyEqual& equal = KeyEqual{})
        : m_Allocator(&allocator), m_Hasher(hasher), m_Equal(equal) {}
    ~FlatHashMap() { Release(); }
    FlatHashMap(const FlatHashMap&) = delete;
    FlatHashMap& operator=(const FlatHashMap&) = delete;
    FlatHashMap(FlatHashMap&& other) noexcept;
    FlatHashMap& operator=(FlatHashMap&& other) noexcept;

    std::size_t size() const noexcept { return m_Size; }
    std::size_t capacity() const noexcept { return m_Capacity; }
    bool empty() const noexcept { return m_Size == 0; }
    std::size_t tombstone_count() const noexcept { return m_Tombstones; }
    iterator begin() noexcept { return iterator(this, 0); }
    iterator end() noexcept { return iterator(this, m_Capacity); }
    const_iterator begin() const noexcept { return const_iterator(this, 0); }
    const_iterator end() const noexcept { return const_iterator(this, m_Capacity); }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }

    Value* find(const Key& key);
    const Value* find(const Key& key) const;
    bool contains(const Key& key) const { return find(key) != nullptr; }
    template<typename K, typename V>
    std::pair<Value*, bool> emplace(K&& key, V&& value);
    std::pair<Value*, bool> insert(const value_type& value)
    { return emplace(value.first, value.second); }
    std::pair<Value*, bool> insert(value_type&& value)
    { return emplace(value.first, std::move(value.second)); }
    bool erase(const Key& key);
    iterator erase(const_iterator position);
    void clear();
    // Tombstoneを同容量の再配置で除去します。FrameAllocatorでは旧領域がResetまで残ります。
    void cleanup_tombstones();
    void reserve(std::size_t count);

    // 診断時だけ全Occupied Slotを走査し、通常のinsert/findには計測負荷を加えません。
    struct ProbeStatistics
    {
        std::size_t TotalProbeLength = 0;
        std::size_t MaximumProbeLength = 0;
        std::size_t OccupiedCount = 0;
        std::size_t CollidedCount = 0;
    };

    ProbeStatistics GetProbeStatistics() const;

private:
    static constexpr std::size_t MinimumCapacity = 8;
    std::size_t FindIndex(const Key& key) const;
    // Growthの低頻度slow pathをinline展開せず、通常のemplace命令列とI-cacheを小さく保ちます。
    RAVEN_FLAT_HASH_MAP_NOINLINE void PrepareForInsert();
    void Rehash(std::size_t capacity);
    static void DestroyOccupiedSlots(Slot* slots, std::size_t capacity) noexcept;
    void Release();
    void MoveFrom(FlatHashMap&& other) noexcept;
    Allocator* m_Allocator = nullptr;
    Slot* m_Slots = nullptr;
    std::size_t m_Capacity = 0;
    std::size_t m_Size = 0;
    std::size_t m_Tombstones = 0;
    Hasher m_Hasher{};
    KeyEqual m_Equal{};
};
#undef RAVEN_FLAT_HASH_MAP_NOINLINE
}
#include "Raven/Core/Containers/FlatHashMap.inl"
