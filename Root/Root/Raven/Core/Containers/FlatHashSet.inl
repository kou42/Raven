#pragma once

// FlatHashSetはテンプレートのため、実装をヘッダー末尾から取り込みます。
namespace Raven
{

template<typename Key, typename Hasher, typename KeyEqual>
iterator FlatHashSet<Key, Hasher, KeyEqual>::erase(const_iterator position)
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

template<typename Key, typename Hasher, typename KeyEqual>
[[nodiscard]] typename FlatHashSet<Key, Hasher, KeyEqual>::ProbeStatistics FlatHashSet<Key, Hasher, KeyEqual>::GetProbeStatistics() const
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

template<typename Key, typename Hasher, typename KeyEqual>
void FlatHashSet<Key, Hasher, KeyEqual>::cleanup_tombstones()
{
        if (m_TombstoneCount != 0)
        {
            Rehash(m_Capacity);
        }
    }

template<typename Key, typename Hasher, typename KeyEqual>
void FlatHashSet<Key, Hasher, KeyEqual>::reserve(std::size_t elementCount)
{
        const std::size_t requiredCapacity = CapacityForElementCount(elementCount);
        if (requiredCapacity <= m_Capacity)
        {
            return;
        }

        Rehash(requiredCapacity);
    }

template<typename Key, typename Hasher, typename KeyEqual>
[[nodiscard]] const Key* FlatHashSet<Key, Hasher, KeyEqual>::find(const Key& key) const
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

template<typename Key, typename Hasher, typename KeyEqual>
bool FlatHashSet<Key, Hasher, KeyEqual>::erase(const Key& key)
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

template<typename Key, typename Hasher, typename KeyEqual>
void FlatHashSet<Key, Hasher, KeyEqual>::clear()
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

template<typename Key, typename Hasher, typename KeyEqual>
[[nodiscard]] std::size_t FlatHashSet<Key, Hasher, KeyEqual>::NextPowerOfTwo(std::size_t value)
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

template<typename Key, typename Hasher, typename KeyEqual>
[[nodiscard]] std::size_t FlatHashSet<Key, Hasher, KeyEqual>::CapacityForElementCount(std::size_t elementCount)
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

template<typename Key, typename Hasher, typename KeyEqual>
[[nodiscard]] bool FlatHashSet<Key, Hasher, KeyEqual>::NeedsGrowForInsert() const
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

template<typename Key, typename Hasher, typename KeyEqual>
template<typename KeyValue>
std::pair<const Key*, bool> FlatHashSet<Key, Hasher, KeyEqual>::InsertImpl(KeyValue&& key)
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

template<typename Key, typename Hasher, typename KeyEqual>
template<typename KeyValue>
std::pair<const Key*, bool> FlatHashSet<Key, Hasher, KeyEqual>::InsertWithoutGrow(KeyValue&& key)
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

template<typename Key, typename Hasher, typename KeyEqual>
void FlatHashSet<Key, Hasher, KeyEqual>::Rehash(std::size_t requestedCapacity)
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

template<typename Key, typename Hasher, typename KeyEqual>
FlatHashSet<Key, Hasher, KeyEqual>::ReleaseSlots();
    }

    FlatHashSet(const FlatHashSet&) = delete;
    FlatHashSet& operator=(const FlatHashSet&) = delete;

    FlatHashSet(FlatHashSet&& other) noexcept
{
        MoveFrom(std::move(other));
    }

template<typename Key, typename Hasher, typename KeyEqual>
void FlatHashSet<Key, Hasher, KeyEqual>::MoveFrom(FlatHashSet&& other) noexcept
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

} // namespace Raven
