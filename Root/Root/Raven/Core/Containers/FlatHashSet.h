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
    iterator erase(const_iterator position)
    {
        if (position.m_Set != this || position.m_Index >= m_Capacity)
        {
            return end();
        }

        Slot& slot = m_Slots[position.m_Index];
        if (slot.State != SlotState::Occupied)
        {
            return end();
        }

        slot.GetKey()->~Key();
        slot.State = SlotState::Tombstone;
        --m_Size;
        ++m_TombstoneCount;
        return iterator(this, position.m_Index + 1);
    }

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

    FlatHashSet(FlatHashSet&& other) noexcept
    {
        MoveFrom(std::move(other));
    }

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

    [[nodiscard]] ProbeStatistics GetProbeStatistics() const
    {
        ProbeStatistics stats{};
        if (m_Capacity == 0)
        {
            return stats;
        }
        for (std::size_t i = 0; i < m_Capacity; ++i)
        {
            if (m_Slots[i].State != SlotState::Occupied)
            {
                continue;
            }
            const std::size_t ideal = m_Hasher(*m_Slots[i].GetKey()) & (m_Capacity - 1);
            const std::size_t distance = ((i - ideal) & (m_Capacity - 1)) + 1;
            stats.TotalProbeLength += distance;
            if (distance > stats.MaximumProbeLength)
            {
                stats.MaximumProbeLength = distance;
            }
            ++stats.OccupiedCount;
        }
        return stats;
    }

    // Tombstoneを同容量の再配置で除去します。FrameAllocatorでは旧領域がResetまで残ります。
    void cleanup_tombstones()
    {
        if (m_TombstoneCount != 0)
        {
            Rehash(m_Capacity);
        }
    }

    // elementCount個をLoad Factor上限以内で保持できるSlot数を事前確保します。
    // 既存Capacityで十分な場合は何もしません。
    void reserve(std::size_t elementCount)
    {
        const std::size_t requiredCapacity = CapacityForElementCount(elementCount);
        if (requiredCapacity <= m_Capacity)
        {
            return;
        }

        Rehash(requiredCapacity);
    }

    std::pair<const Key*, bool> insert(const Key& key)
    {
        return InsertImpl(key);
    }

    std::pair<const Key*, bool> insert(Key&& key)
    {
        return InsertImpl(std::move(key));
    }

    [[nodiscard]] const Key* find(const Key& key) const
    {
        if (m_Capacity == 0)
        {
            return nullptr;
        }

        const std::size_t mask = m_Capacity - 1;
        std::size_t index = m_Hasher(key) & mask;

        for (std::size_t probeCount = 0; probeCount < m_Capacity; ++probeCount)
        {
            const Slot& slot = m_Slots[index];
            if (slot.State == SlotState::Empty)
            {
                // Tombstoneではなく完全なEmptyへ到達した時点で、このProbe Chainには
                // 対象Keyが存在しないことが確定します。
                return nullptr;
            }

            if (slot.State == SlotState::Occupied
                && m_KeyEqual(*slot.GetKey(), key))
            {
                return slot.GetKey();
            }

            index = (index + 1) & mask;
        }

        return nullptr;
    }

    [[nodiscard]] bool contains(const Key& key) const
    {
        return find(key) != nullptr;
    }

    bool erase(const Key& key)
    {
        if (m_Capacity == 0)
        {
            return false;
        }

        const std::size_t mask = m_Capacity - 1;
        std::size_t index = m_Hasher(key) & mask;

        for (std::size_t probeCount = 0; probeCount < m_Capacity; ++probeCount)
        {
            Slot& slot = m_Slots[index];
            if (slot.State == SlotState::Empty)
            {
                return false;
            }

            if (slot.State == SlotState::Occupied
                && m_KeyEqual(*slot.GetKey(), key))
            {
                slot.GetKey()->~Key();
                slot.State = SlotState::Tombstone;
                --m_Size;
                ++m_TombstoneCount;
                return true;
            }

            index = (index + 1) & mask;
        }

        return false;
    }

    void clear()
    {
        if (m_Slots == nullptr)
        {
            return;
        }

        for (std::size_t i = 0; i < m_Capacity; ++i)
        {
            Slot& slot = m_Slots[i];
            if (slot.State == SlotState::Occupied)
            {
                slot.GetKey()->~Key();
            }
            slot.State = SlotState::Empty;
        }

        m_Size = 0;
        m_TombstoneCount = 0;
    }

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

    [[nodiscard]] static std::size_t NextPowerOfTwo(std::size_t value)
    {
        std::size_t result = MinimumCapacity;
        while (result < value)
        {
            if (result > static_cast<std::size_t>(-1) / 2)
            {
                return 0;
            }
            result *= 2;
        }
        return result;
    }

    [[nodiscard]] static std::size_t CapacityForElementCount(std::size_t elementCount)
    {
        if (elementCount == 0)
        {
            return MinimumCapacity;
        }

        // ceil(elementCount / 0.7) を整数演算で求めます。
        if (elementCount > (static_cast<std::size_t>(-1) - MaxLoadNumerator + 1)
            / MaxLoadDenominator)
        {
            throw std::bad_alloc{};
        }

        const std::size_t numerator = elementCount * MaxLoadDenominator;
        const std::size_t required =
            (numerator + MaxLoadNumerator - 1) / MaxLoadNumerator;
        return NextPowerOfTwo(required);
    }

    [[nodiscard]] bool NeedsGrowForInsert() const
    {
        if (m_Capacity == 0)
        {
            return true;
        }

        // TombstoneもProbe距離悪化要因になるため、実Occupied数だけでなく
        // Occupied + TombstoneをLoad Factor判定へ含めます。
        const std::size_t threshold = (m_Capacity / MaxLoadDenominator) * MaxLoadNumerator
            + ((m_Capacity % MaxLoadDenominator) * MaxLoadNumerator) / MaxLoadDenominator;
        return m_Size + m_TombstoneCount >= threshold;
    }

    template<typename KeyValue>
    std::pair<const Key*, bool> InsertImpl(KeyValue&& key)
    {
        if (NeedsGrowForInsert())
        {
            // Tombstoneだけが閾値超過の原因なら、容量を増やさず同容量で再配置します。
            // 毎回のeraseでは再配置せず、次のinsert時まで遅延させます。
            if (m_Capacity != 0 && m_TombstoneCount != 0
                && m_Size < (m_Capacity / MaxLoadDenominator) * MaxLoadNumerator)
            {
                Rehash(m_Capacity);
            }
            else
            {
            if (m_Capacity > static_cast<std::size_t>(-1) / 2)
            {
                throw std::bad_alloc{};
            }

            const std::size_t newCapacity =
                m_Capacity == 0 ? MinimumCapacity : m_Capacity * 2;
            Rehash(newCapacity);
            }
        }

        return InsertWithoutGrow(std::forward<KeyValue>(key));
    }

    template<typename KeyValue>
    std::pair<const Key*, bool> InsertWithoutGrow(KeyValue&& key)
    {
        const std::size_t mask = m_Capacity - 1;
        std::size_t index = m_Hasher(key) & mask;
        std::size_t firstTombstone = m_Capacity;

        for (std::size_t probeCount = 0; probeCount < m_Capacity; ++probeCount)
        {
            Slot& slot = m_Slots[index];

            if (slot.State == SlotState::Occupied)
            {
                if (m_KeyEqual(*slot.GetKey(), key))
                {
                    return { slot.GetKey(), false };
                }
            }
            else if (slot.State == SlotState::Tombstone)
            {
                if (firstTombstone == m_Capacity)
                {
                    firstTombstone = index;
                }
            }
            else
            {
                const std::size_t insertIndex =
                    firstTombstone != m_Capacity ? firstTombstone : index;
                Slot& destination = m_Slots[insertIndex];

                new (destination.Storage) Key(std::forward<KeyValue>(key));
                destination.State = SlotState::Occupied;
                ++m_Size;

                if (firstTombstone != m_Capacity)
                {
                    --m_TombstoneCount;
                }

                return { destination.GetKey(), true };
            }

            index = (index + 1) & mask;
        }

        if (m_Capacity > static_cast<std::size_t>(-1) / 2)
        {
            throw std::bad_alloc{};
        }

        // Load Factor制限により通常ここへは到達しませんが、Tombstone配置などで
        // Slotを確保できなかった場合は容量を倍増して再試行します。
        Rehash(m_Capacity * 2);
        return InsertWithoutGrow(std::forward<KeyValue>(key));
    }

    void Rehash(std::size_t requestedCapacity)
    {
        // Copy可能なKeyは旧テーブルを変更せず複製します。
        // Move-only Keyはnoexcept moveを必須とし、Hasherの実行を移動前に完了させます。
        // これにより例外時にも旧テーブルを保持できます。
        static_assert(std::is_copy_constructible_v<Key> || std::is_nothrow_move_constructible_v<Key>,
            "FlatHashSet rehash requires a copyable Key or a noexcept movable Key");

        const std::size_t newCapacity = NextPowerOfTwo(requestedCapacity);
        if (newCapacity == 0 || m_Allocator == nullptr
            || newCapacity > static_cast<std::size_t>(-1) / sizeof(Slot))
        {
            throw std::bad_alloc{};
        }

        void* rawMemory = m_Allocator->Allocate(newCapacity * sizeof(Slot), alignof(Slot));
        if (rawMemory == nullptr)
        {
            throw std::bad_alloc{};
        }

        Slot* newSlots = static_cast<Slot*>(rawMemory);
        for (std::size_t i = 0; i < newCapacity; ++i)
        {
            new (&newSlots[i]) Slot{};
        }

        // 移動中のHasher例外を避けるため、全Keyの移動先を先に決定します。
        // Destination配列の確保もKeyを変更する前に済ませます。
        std::size_t* destinations = nullptr;
        try
        {
            if (m_Size != 0)
            {
                if (m_Size > static_cast<std::size_t>(-1) / sizeof(std::size_t))
                {
                    throw std::bad_alloc{};
                }
                destinations = static_cast<std::size_t*>(
                    ::operator new(m_Size * sizeof(std::size_t)));
            }

            std::size_t count = 0;
            for (std::size_t i = 0; i < m_Capacity; ++i)
            {
                if (m_Slots[i].State != SlotState::Occupied)
                {
                    continue;
                }
                std::size_t index = m_Hasher(*m_Slots[i].GetKey()) & (newCapacity - 1);
                while (newSlots[index].State != SlotState::Empty)
                {
                    index = (index + 1) & (newCapacity - 1);
                }
                // 予約済みSlotを一時的にTombstoneとしてマークします。
                newSlots[index].State = SlotState::Tombstone;
                destinations[count++] = index;
            }

            // Copyを優先することで、Copy中の例外でも旧Keyを保全します。
            // Copy不可のKeyでは事前計算後にnoexcept moveを使用します。
            count = 0;
            for (std::size_t i = 0; i < m_Capacity; ++i)
            {
                if (m_Slots[i].State != SlotState::Occupied)
                {
                    continue;
                }
                Slot& destination = newSlots[destinations[count++]];
                if constexpr (std::is_copy_constructible_v<Key>)
                {
                    new (destination.Storage) Key(*m_Slots[i].GetKey());
                }
                else
                {
                    new (destination.Storage) Key(std::move(*m_Slots[i].GetKey()));
                }
                destination.State = SlotState::Occupied;
            }
        }
        catch (...)
        {
            for (std::size_t i = 0; i < newCapacity; ++i)
            {
                if (newSlots[i].State == SlotState::Occupied)
                {
                    newSlots[i].GetKey()->~Key();
                }
                newSlots[i].~Slot();
            }
            m_Allocator->Deallocate(newSlots);
            ::operator delete(destinations);
            throw;
        }
        ::operator delete(destinations);

        // ここまで成功した場合のみ新テーブルを公開します。
        Slot* oldSlots = m_Slots;
        const std::size_t oldCapacity = m_Capacity;
        m_Slots = newSlots;
        m_Capacity = newCapacity;
        m_TombstoneCount = 0;

        if (oldSlots != nullptr)
        {
            for (std::size_t i = 0; i < oldCapacity; ++i)
            {
                if (oldSlots[i].State == SlotState::Occupied)
                {
                    oldSlots[i].GetKey()->~Key();
                }
                oldSlots[i].~Slot();
            }
            m_Allocator->Deallocate(oldSlots);
        }
    }

    void ReleaseSlots()
    {
        if (m_Slots == nullptr)
        {
            return;
        }

        for (std::size_t i = 0; i < m_Capacity; ++i)
        {
            Slot& slot = m_Slots[i];
            if (slot.State == SlotState::Occupied)
            {
                slot.GetKey()->~Key();
            }
            slot.~Slot();
        }

        if (m_Allocator != nullptr)
        {
            m_Allocator->Deallocate(m_Slots);
        }

        m_Slots = nullptr;
        m_Capacity = 0;
        m_Size = 0;
        m_TombstoneCount = 0;
    }

    void MoveFrom(FlatHashSet&& other) noexcept
    {
        m_Allocator = other.m_Allocator;
        m_Slots = other.m_Slots;
        m_Capacity = other.m_Capacity;
        m_Size = other.m_Size;
        m_TombstoneCount = other.m_TombstoneCount;
        m_Hasher = std::move(other.m_Hasher);
        m_KeyEqual = std::move(other.m_KeyEqual);

        other.m_Allocator = nullptr;
        other.m_Slots = nullptr;
        other.m_Capacity = 0;
        other.m_Size = 0;
        other.m_TombstoneCount = 0;
    }

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
