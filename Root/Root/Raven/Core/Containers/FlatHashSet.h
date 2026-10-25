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

// ============================================================================
// FlatHashSet
// ============================================================================
// Open Addressing + Linear Probingで実装した、連続メモリ型のHash Setです。
// std::unordered_setのように要素ごとのNode allocationを行わず、Slot配列を1つの
// 連続領域として確保するため、FrameAllocator / LinearAllocatorと特に相性が良い構造です。
//
// 重要:
// - Allocatorは外部所有です。FlatHashSetより長生きする必要があります。
// - reserve()で十分な容量を先に確保すると、その後のinsert()は追加allocationなしで進められます。
// - FrameAllocator利用時にrehashすると旧Slot配列はResetまでArenaに残るため、Hot Pathでは
//   事前reserve()を強く推奨します。
// - erase()後はProbe Chainを切らないためTombstoneを残します。
// - Capacityは2の累乗に揃え、hash & (capacity - 1)でbucket indexを求めます。
//
// IteratorはOccupied Slotだけを走査します。rehash/clear後のIteratorは無効です。
template<typename Key,
    typename Hasher = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>>
class FlatHashSet
{
public:
    // SetのKeyは変更できません。iteratorもconst Key&を返してHash不変条件を守ります。
    class const_iterator
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Key;
        using difference_type = std::ptrdiff_t;
        using pointer = const Key*;
        using reference = const Key&;

        const_iterator() = default;

        reference operator*() const { return *m_Set->m_Slots[m_Index].GetKey(); }
        pointer operator->() const { return &operator*(); }

        const_iterator& operator++()
        {
            ++m_Index;
            SkipEmpty();
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator previous = *this;
            ++(*this);
            return previous;
        }

        friend bool operator==(const const_iterator& left, const const_iterator& right)
        {
            return left.m_Set == right.m_Set && left.m_Index == right.m_Index;
        }

        friend bool operator!=(const const_iterator& left, const const_iterator& right)
        {
            return (left == right) == false;
        }

    private:
        friend class FlatHashSet;

        const_iterator(const FlatHashSet* set, std::size_t index)
            : m_Set(set), m_Index(index)
        {
            SkipEmpty();
        }

        void SkipEmpty()
        {
            if (m_Set == nullptr)
            {
                return;
            }
            while (m_Index < m_Set->m_Capacity
                && m_Set->m_Slots[m_Index].State != SlotState::Occupied)
            {
                ++m_Index;
            }
        }

        const FlatHashSet* m_Set = nullptr;
        std::size_t m_Index = 0;
    };

    using iterator = const_iterator;

    [[nodiscard]] iterator begin() noexcept { return iterator(this, 0); }
    [[nodiscard]] iterator end() noexcept { return iterator(this, m_Capacity); }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(this, 0); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(this, m_Capacity); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    // erase後は次のOccupied Slotを返します。rehashを行わないため他のIteratorは維持されます。
    iterator erase(const_iterator position);


    explicit FlatHashSet(
        Allocator& allocator,
        const Hasher& hasher = Hasher{},
        const KeyEqual& keyEqual = KeyEqual{})
        : m_Allocator(&allocator)
        , m_Hasher(hasher)
        , m_KeyEqual(keyEqual)
    {
    }

    ~FlatHashSet()
    {
        ReleaseSlots();
    }

    FlatHashSet(const FlatHashSet&) = delete;
    FlatHashSet& operator=(const FlatHashSet&) = delete;

    FlatHashSet(FlatHashSet&& other) noexcept;


    FlatHashSet& operator=(FlatHashSet&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        ReleaseSlots();
        MoveFrom(std::move(other));
        return *this;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_Size; }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_Capacity; }
    [[nodiscard]] bool empty() const noexcept { return m_Size == 0; }
    [[nodiscard]] std::size_t tombstone_count() const noexcept { return m_TombstoneCount; }

    // Hash衝突の診断用。各Occupied Keyの理想BucketからのProbe距離を測定します。
    // Hasherが例外を投げる場合は本関数も例外を伝播します。
    struct ProbeStatistics
    {
        std::size_t TotalProbeLength = 0;
        std::size_t MaximumProbeLength = 0;
        std::size_t OccupiedCount = 0;
    };

    [[nodiscard]] ProbeStatistics GetProbeStatistics() const;


    // Tombstoneを同容量の再配置で除去します。FrameAllocatorでは旧領域がResetまで残ります。
    void cleanup_tombstones();


    // elementCount個をLoad Factor上限以内で保持できるSlot数を事前確保します。
    // 既存Capacityで十分な場合は何もしません。
    void reserve(std::size_t elementCount);


    std::pair<const Key*, bool> insert(const Key& key)
    {
        return InsertImpl(key);
    }

    std::pair<const Key*, bool> insert(Key&& key)
    {
        return InsertImpl(std::move(key));
    }

    [[nodiscard]] const Key* find(const Key& key) const;


    [[nodiscard]] bool contains(const Key& key) const
    {
        return find(key) != nullptr;
    }

    bool erase(const Key& key);


    void clear();


private:
    enum class SlotState : uint8_t
    {
        Empty,
        Occupied,
        Tombstone
    };

    struct Slot
    {
        SlotState State = SlotState::Empty;
        alignas(Key) unsigned char Storage[sizeof(Key)]{};

        Key* GetKey()
        {
            return std::launder(reinterpret_cast<Key*>(Storage));
        }

        const Key* GetKey() const
        {
            return std::launder(reinterpret_cast<const Key*>(Storage));
        }
    };

    static constexpr std::size_t MinimumCapacity = 8;
    static constexpr std::size_t MaxLoadNumerator = 7;
    static constexpr std::size_t MaxLoadDenominator = 10;

    [[nodiscard]] static std::size_t NextPowerOfTwo(std::size_t value);


    [[nodiscard]] static std::size_t CapacityForElementCount(std::size_t elementCount);


    [[nodiscard]] bool NeedsGrowForInsert() const;


    template<typename KeyValue>
    std::pair<const Key*, bool> InsertImpl(KeyValue&& key);


    template<typename KeyValue>
    std::pair<const Key*, bool> InsertWithoutGrow(KeyValue&& key);


    void Rehash(std::size_t requestedCapacity);


    void ReleaseSlots();

    void MoveFrom(FlatHashSet&& other) noexcept;


private:
    Allocator* m_Allocator = nullptr;
    Slot* m_Slots = nullptr;
    std::size_t m_Capacity = 0;
    std::size_t m_Size = 0;
    std::size_t m_TombstoneCount = 0;
    Hasher m_Hasher{};
    KeyEqual m_KeyEqual{};
};

} // namespace Raven

#include "Raven/Core/Containers/FlatHashSet.inl"
