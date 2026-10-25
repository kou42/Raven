#pragma once
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace Raven
{
template<typename K, typename V, typename H, typename E>
FlatHashMap<K,V,H,E>::FlatHashMap(FlatHashMap&& other) noexcept
    : m_Allocator(other.m_Allocator), m_Hasher(std::move(other.m_Hasher)),
      m_Equal(std::move(other.m_Equal))
{ MoveFrom(std::move(other)); }

template<typename K, typename V, typename H, typename E>
FlatHashMap<K,V,H,E>& FlatHashMap<K,V,H,E>::operator=(FlatHashMap&& other) noexcept
{
    if (this != &other)
    {
        Release();
        m_Hasher = std::move(other.m_Hasher);
        m_Equal = std::move(other.m_Equal);
        m_Allocator = other.m_Allocator;
        MoveFrom(std::move(other));
    }
    return *this;
}

template<typename K, typename V, typename H, typename E>
void FlatHashMap<K,V,H,E>::MoveFrom(FlatHashMap&& other) noexcept
{
    m_Slots = std::exchange(other.m_Slots, nullptr);
    m_Capacity = std::exchange(other.m_Capacity, 0);
    m_Size = std::exchange(other.m_Size, 0);
    m_Tombstones = std::exchange(other.m_Tombstones, 0);
}

template<typename K, typename V, typename H, typename E>
void FlatHashMap<K,V,H,E>::Release()
{
    if (m_Slots != nullptr)
    {
        DestroyOccupiedSlots(m_Slots, m_Capacity);
        m_Allocator->Deallocate(m_Slots);
        m_Slots = nullptr;
    }
    m_Capacity = 0;
    m_Size = 0;
    m_Tombstones = 0;
}

template<typename K, typename V, typename H, typename E>
void FlatHashMap<K,V,H,E>::DestroyOccupiedSlots(Slot* slots, std::size_t capacity) noexcept
{
    // Slot自体はtrivialな状態Byteと未型付けStorageだけなので、Occupiedなpairだけを破棄します。
    // 解放直前にStateをEmptyへ戻す走査やSlot destructorの走査を重ねないことでrehashを軽量化します。
    static_assert(std::is_trivially_destructible_v<Slot>);
    for (std::size_t index = 0; index < capacity; ++index)
    {
        if (slots[index].State == SlotState::Occupied)
        {
            slots[index].Get()->~value_type();
        }
    }
}

template<typename K, typename V, typename H, typename E>
void FlatHashMap<K,V,H,E>::clear()
{
    for (std::size_t i = 0; i < m_Capacity; ++i)
    {
        if (m_Slots[i].State == SlotState::Occupied) { m_Slots[i].Get()->~value_type(); }
        m_Slots[i].State = SlotState::Empty;
    }
    m_Size = 0;
    m_Tombstones = 0;
}

template<typename K, typename V, typename H, typename E>
void FlatHashMap<K,V,H,E>::cleanup_tombstones()
{
    if (m_Tombstones != 0)
    {
        Rehash(m_Capacity);
    }
}

template<typename K, typename V, typename H, typename E>
typename FlatHashMap<K,V,H,E>::ProbeStatistics FlatHashMap<K,V,H,E>::GetProbeStatistics() const
{
    ProbeStatistics statistics{};
    if (m_Capacity == 0)
    {
        return statistics;
    }

    const std::size_t mask = m_Capacity - 1;
    for (std::size_t index = 0; index < m_Capacity; ++index)
    {
        if (m_Slots[index].State != SlotState::Occupied)
        {
            continue;
        }

        const std::size_t ideal = m_Hasher(m_Slots[index].Get()->first) & mask;
        const std::size_t distance = ((index - ideal) & mask) + 1;
        statistics.TotalProbeLength += distance;
        statistics.MaximumProbeLength = (std::max)(statistics.MaximumProbeLength, distance);
        ++statistics.OccupiedCount;
        if (distance > 1)
        {
            ++statistics.CollidedCount;
        }
    }
    return statistics;
}

template<typename K, typename V, typename H, typename E>
std::size_t FlatHashMap<K,V,H,E>::FindIndex(const K& key) const
{
    if (m_Capacity == 0) { return 0; }
    const std::size_t mask = m_Capacity - 1;
    std::size_t index = m_Hasher(key) & mask;
    for (std::size_t probe = 0; probe < m_Capacity; ++probe)
    {
        const Slot& slot = m_Slots[index];
        if (slot.State == SlotState::Empty) { break; }
        if (slot.State == SlotState::Occupied && m_Equal(slot.Get()->first, key)) { return index; }
        index = (index + 1) & mask;
    }
    return m_Capacity;
}

template<typename K, typename V, typename H, typename E>
V* FlatHashMap<K,V,H,E>::find(const K& key)
{
    const std::size_t index = FindIndex(key);
    return index < m_Capacity ? &m_Slots[index].Get()->second : nullptr;
}

template<typename K, typename V, typename H, typename E>
const V* FlatHashMap<K,V,H,E>::find(const K& key) const
{
    const std::size_t index = FindIndex(key);
    return index < m_Capacity ? &m_Slots[index].Get()->second : nullptr;
}

template<typename K, typename V, typename H, typename E>
void FlatHashMap<K,V,H,E>::reserve(std::size_t count)
{
    if (count > (std::numeric_limits<std::size_t>::max)() / 10)
    { throw std::bad_alloc{}; }
    const std::size_t required = count * 10 / 7 + ((count * 10 % 7) != 0 ? 1 : 0);
    std::size_t target = MinimumCapacity;
    while (target < required)
    {
        if (target > (std::numeric_limits<std::size_t>::max)() / 2)
        { throw std::bad_alloc{}; }
        target *= 2;
    }
    if (target > m_Capacity) { Rehash(target); }
}

template<typename K, typename V, typename H, typename E>
void FlatHashMap<K,V,H,E>::PrepareForInsert()
{
    if (m_Capacity == 0)
    {
        Rehash(MinimumCapacity);
    }
    else if (m_Size + 1 <=
        (m_Capacity / 10) * 7 + ((m_Capacity % 10) * 7) / 10)
    {
        // Occupied数には余裕がありTombstoneだけが閾値を超えた場合は、同容量で整理します。
        Rehash(m_Capacity);
    }
    else
    {
        if (m_Capacity > (std::numeric_limits<std::size_t>::max)() / 2)
        {
            throw std::bad_alloc{};
        }
        Rehash(m_Capacity * 2);
    }
}

template<typename K, typename V, typename H, typename E>
void FlatHashMap<K,V,H,E>::Rehash(std::size_t capacity)
{
    constexpr bool CanCopyEntry =
        std::is_copy_constructible_v<K> && std::is_copy_constructible_v<V>;
    constexpr bool CanSafelyMoveEntry =
        std::is_nothrow_copy_constructible_v<K> && std::is_nothrow_move_constructible_v<V>;
    static_assert(CanCopyEntry || CanSafelyMoveEntry,
        "FlatHashMap requires copyable entries or a nothrow-copyable key and nothrow-movable value");
    if (capacity > (std::numeric_limits<std::size_t>::max)() / sizeof(Slot))
    { throw std::bad_alloc{}; }
    void* memory = m_Allocator->Allocate(capacity * sizeof(Slot), alignof(Slot));
    if (memory == nullptr) { throw std::bad_alloc{}; }
    Slot* next = static_cast<Slot*>(memory);
    for (std::size_t i = 0; i < capacity; ++i)
    {
        // 連続したSlot全体の初期化はコンパイラが効率的にまとめられ、Stateだけをstride付きで
        // 書き込むよりReserved経路が安定して速いため、Storageを含むvalue initializationを維持します。
        new (&next[i]) Slot{};
    }
    try
    {
        if constexpr (CanCopyEntry)
        {
            // Copy可能なら旧要素を変更しないため、Hasher/Key/Value copyのどこで例外が発生しても
            // 構築済みの新要素だけを破棄して旧テーブルをそのまま維持できます。
            // この経路では移動先一覧の補助vectorと予約・再走査が不要です。
            for (std::size_t sourceIndex = 0; sourceIndex < m_Capacity; ++sourceIndex)
            {
                if (m_Slots[sourceIndex].State != SlotState::Occupied)
                {
                    continue;
                }
                value_type& source = *m_Slots[sourceIndex].Get();
                std::size_t destinationIndex = m_Hasher(source.first) & (capacity - 1);
                while (next[destinationIndex].State == SlotState::Occupied)
                {
                    destinationIndex = (destinationIndex + 1) & (capacity - 1);
                }
                Slot& destination = next[destinationIndex];
                new (destination.Storage) value_type(source);
                destination.State = SlotState::Occupied;
            }
        }
        else if constexpr (std::is_nothrow_invocable_r_v<std::size_t, const H&, const K&>)
        {
            // Hasher・Key copy・Value moveがすべてnoexceptなら、直接移動しても途中失敗しません。
            for (std::size_t sourceIndex = 0; sourceIndex < m_Capacity; ++sourceIndex)
            {
                if (m_Slots[sourceIndex].State != SlotState::Occupied)
                {
                    continue;
                }
                value_type& source = *m_Slots[sourceIndex].Get();
                std::size_t destinationIndex = m_Hasher(source.first) & (capacity - 1);
                while (next[destinationIndex].State == SlotState::Occupied)
                {
                    destinationIndex = (destinationIndex + 1) & (capacity - 1);
                }
                Slot& destination = next[destinationIndex];
                new (destination.Storage) value_type(source.first, std::move(source.second));
                destination.State = SlotState::Occupied;
            }
        }
        else
        {
            // move-only ValueかつHasherが例外を送出し得る場合だけ、全Hashを移動前に評価します。
            // 補助vector確保やHasherが失敗してもValueはまだ移動していないため旧テーブルを保護できます。
            std::vector<std::pair<std::size_t, std::size_t>> locations;
            locations.reserve(m_Size);
            for (std::size_t sourceIndex = 0; sourceIndex < m_Capacity; ++sourceIndex)
            {
                if (m_Slots[sourceIndex].State != SlotState::Occupied)
                {
                    continue;
                }
                std::size_t destinationIndex =
                    m_Hasher(m_Slots[sourceIndex].Get()->first) & (capacity - 1);
                while (next[destinationIndex].State != SlotState::Empty)
                {
                    destinationIndex = (destinationIndex + 1) & (capacity - 1);
                }
                next[destinationIndex].State = SlotState::Tombstone;
                locations.emplace_back(sourceIndex, destinationIndex);
            }
            for (const auto& location : locations)
            {
                Slot& destination = next[location.second];
                value_type& source = *m_Slots[location.first].Get();
                new (destination.Storage) value_type(source.first, std::move(source.second));
                destination.State = SlotState::Occupied;
            }
        }
    }
    catch (...)
    {
        DestroyOccupiedSlots(next, capacity);
        m_Allocator->Deallocate(next);
        throw;
    }

    const std::size_t previousSize = m_Size;
    if (m_Slots != nullptr)
    {
        DestroyOccupiedSlots(m_Slots, m_Capacity);
        m_Allocator->Deallocate(m_Slots);
    }
    m_Slots = next;
    m_Capacity = capacity;
    if (previousSize == 0)
    {
        // 初回reserve直後に続く大量insertでは、State走査で新テーブルをcacheへ載せる方が
        // 大規模入力で安定して速いことをBenchmarkで確認しています。
        m_Size = 0;
        for (std::size_t index = 0; index < capacity; ++index)
        {
            if (next[index].State == SlotState::Occupied)
            {
                ++m_Size;
            }
        }
    }
    else
    {
        // Growth時は再配置数が既知なので、新テーブル全体の再集計を省きます。
        m_Size = previousSize;
    }
    m_Tombstones = 0;
}

template<typename K, typename V, typename H, typename E>
template<typename KeyArg, typename ValueArg>
std::pair<V*, bool> FlatHashMap<K,V,H,E>::emplace(KeyArg&& key, ValueArg&& value)
{
    // 重複KeyではValueを移動せず、不要なrehashも実行しません。
    std::size_t existing = FindIndex(key);
    if (existing < m_Capacity) { return { &m_Slots[existing].Get()->second, false }; }
    if (m_Capacity == 0 || m_Size + m_Tombstones + 1 > (m_Capacity / 10) * 7 + ((m_Capacity % 10) * 7) / 10)
    {
        PrepareForInsert();
    }
    const std::size_t mask = m_Capacity - 1;
    std::size_t index = m_Hasher(key) & mask;
    std::size_t tombstone = m_Capacity;
    for (std::size_t probe = 0; probe < m_Capacity; ++probe)
    {
        Slot& slot = m_Slots[index];
        if (slot.State == SlotState::Tombstone && tombstone == m_Capacity) { tombstone = index; }
        if (slot.State == SlotState::Empty)
        {
            Slot& target = m_Slots[tombstone < m_Capacity ? tombstone : index];
            new (target.Storage) value_type(std::forward<KeyArg>(key), std::forward<ValueArg>(value));
            target.State = SlotState::Occupied;
            ++m_Size;
            if (tombstone < m_Capacity) { --m_Tombstones; }
            return { &target.Get()->second, true };
        }
        index = (index + 1) & mask;
    }
    throw std::bad_alloc{};
}

template<typename K, typename V, typename H, typename E>
bool FlatHashMap<K,V,H,E>::erase(const K& key)
{
    const std::size_t index = FindIndex(key);
    if (index == m_Capacity) { return false; }
    m_Slots[index].Get()->~value_type();
    m_Slots[index].State = SlotState::Tombstone;
    --m_Size;
    ++m_Tombstones;
    return true;
}

template<typename K, typename V, typename H, typename E>
typename FlatHashMap<K,V,H,E>::iterator FlatHashMap<K,V,H,E>::erase(const_iterator position)
{
    if (position.m_Map != this || position.m_Index >= m_Capacity) { return end(); }
    Slot& slot = m_Slots[position.m_Index];
    if (slot.State != SlotState::Occupied) { return end(); }
    slot.Get()->~value_type();
    slot.State = SlotState::Tombstone;
    --m_Size;
    ++m_Tombstones;
    return iterator(this, position.m_Index + 1);
}
}
