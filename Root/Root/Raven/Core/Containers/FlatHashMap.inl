#pragma once
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
        clear();
        for (std::size_t i = 0; i < m_Capacity; ++i) { m_Slots[i].~Slot(); }
        m_Allocator->Deallocate(m_Slots);
        m_Slots = nullptr;
    }
    m_Capacity = 0;
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
    const std::size_t required = (count * 10 + 6) / 7;
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
void FlatHashMap<K,V,H,E>::Rehash(std::size_t capacity)
{
    // 移動前に全Hashを評価し、Hasher例外で既存データが失われることを防ぎます。
    static_assert(std::is_copy_constructible_v<value_type> ||
        std::is_nothrow_constructible_v<value_type, const K&, V&&>,
        "FlatHashMap requires copyable entries or noexcept movable values");
    if (capacity > (std::numeric_limits<std::size_t>::max)() / sizeof(Slot))
    { throw std::bad_alloc{}; }
    void* memory = m_Allocator->Allocate(capacity * sizeof(Slot), alignof(Slot));
    if (memory == nullptr) { throw std::bad_alloc{}; }
    Slot* next = static_cast<Slot*>(memory);
    for (std::size_t i = 0; i < capacity; ++i) { new (&next[i]) Slot{}; }
    try
    {
        // Copy可能な要素は旧テーブルを変更せず再配置します。
        // Move-only Valueでは先に全移動先を確定し、noexcept moveで構築します。
        std::vector<std::pair<std::size_t, std::size_t>> locations;
        locations.reserve(m_Size);
        for (std::size_t i = 0; i < m_Capacity; ++i)
        {
            if (m_Slots[i].State != SlotState::Occupied) { continue; }
            std::size_t index = m_Hasher(m_Slots[i].Get()->first) & (capacity - 1);
            while (next[index].State != SlotState::Empty)
            { index = (index + 1) & (capacity - 1); }
            next[index].State = SlotState::Tombstone;
            locations.emplace_back(i, index);
        }
        for (const auto& location : locations)
        {
            Slot& destination = next[location.second];
            value_type& source = *m_Slots[location.first].Get();
            if constexpr (std::is_copy_constructible_v<value_type>)
            { new (destination.Storage) value_type(source); }
            else
            { new (destination.Storage) value_type(source.first, std::move(source.second)); }
            destination.State = SlotState::Occupied;
        }
    }
    catch (...)
    {
        for (std::size_t i = 0; i < capacity; ++i)
        {
            if (next[i].State == SlotState::Occupied) { next[i].Get()->~value_type(); }
            next[i].~Slot();
        }
        m_Allocator->Deallocate(next);
        throw;
    }
    Release();
    m_Slots = next;
    m_Capacity = capacity;
    // Release()でsizeが0になるため、Occupied数を再集計します。
    for (std::size_t i = 0; i < capacity; ++i)
    { if (next[i].State == SlotState::Occupied) { ++m_Size; } }
    m_Tombstones = 0;
}

template<typename K, typename V, typename H, typename E>
template<typename KeyArg, typename ValueArg>
std::pair<V*, bool> FlatHashMap<K,V,H,E>::emplace(KeyArg&& key, ValueArg&& value)
{
    // 重複KeyではValueを移動せず、不要なrehashも実行しません。
    std::size_t existing = FindIndex(key);
    if (existing < m_Capacity) { return { &m_Slots[existing].Get()->second, false }; }
    if (m_Capacity == 0 || (m_Size + m_Tombstones + 1) * 10 > m_Capacity * 7)
    {
        if (m_Capacity == 0) { Rehash(MinimumCapacity); }
        else if ((m_Size + 1) * 10 <= m_Capacity * 7) { Rehash(m_Capacity); }
        else
        {
            if (m_Capacity > (std::numeric_limits<std::size_t>::max)() / 2)
            { throw std::bad_alloc{}; }
            Rehash(m_Capacity * 2);
        }
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
