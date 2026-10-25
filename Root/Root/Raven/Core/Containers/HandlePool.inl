#pragma once

namespace Raven
{

template<typename T>
T* HandlePool<T>::Slot::value() noexcept
{
    return std::launder(reinterpret_cast<T*>(&storage));
}

template<typename T>
const T* HandlePool<T>::Slot::value() const noexcept
{
    return std::launder(reinterpret_cast<const T*>(&storage));
}

template<typename T>
HandlePool<T>::~HandlePool() noexcept
{
    clear();
}

template<typename T>
template<typename... Args>
typename HandlePool<T>::Handle HandlePool<T>::emplace(Args&&... args)
{
    if (m_FreeHead != InvalidIndex)
    {
        // 構築が例外を投げてもFree Listを変更せず、Slotを再利用可能に保ちます。
        const std::uint32_t index = m_FreeHead;
        Slot& slot = *m_Slots[index];
        ::new (static_cast<void*>(&slot.storage)) T(std::forward<Args>(args)...);
        m_FreeHead = slot.nextFree;
        slot.nextFree = InvalidIndex;
        slot.occupied = true;
        ++m_Size;
        return Handle{index, slot.generation};
    }

    if (m_Slots.size() >= static_cast<size_type>(InvalidIndex))
    {
        throw std::length_error("HandlePool index capacity exceeded");
    }
    // vector再配置時にもSlotの実体は移動せず、既存のT*を無効化しません。
    auto slot = std::make_unique<Slot>();
    ::new (static_cast<void*>(&slot->storage)) T(std::forward<Args>(args)...);
    slot->occupied = true;
    try
    {
        m_Slots.push_back(std::move(slot));
    }
    catch (...)
    {
        // push_back失敗時には構築済みTを明示破棄します。
        if (slot != nullptr)
        {
            slot->value()->~T();
        }
        throw;
    }
    ++m_Size;
    return Handle{static_cast<std::uint32_t>(m_Slots.size() - 1), m_Slots.back()->generation};
}

template<typename T>
T* HandlePool<T>::get(Handle handle) noexcept
{
    if (handle.index >= m_Slots.size())
    {
        return nullptr;
    }
    Slot& slot = *m_Slots[handle.index];
    if (slot.occupied == false || slot.generation != handle.generation)
    {
        return nullptr;
    }
    return slot.value();
}

template<typename T>
const T* HandlePool<T>::get(Handle handle) const noexcept
{
    if (handle.index >= m_Slots.size())
    {
        return nullptr;
    }
    const Slot& slot = *m_Slots[handle.index];
    if (slot.occupied == false || slot.generation != handle.generation)
    {
        return nullptr;
    }
    return slot.value();
}

template<typename T>
bool HandlePool<T>::contains(Handle handle) const noexcept
{
    return get(handle) != nullptr;
}

template<typename T>
bool HandlePool<T>::erase(Handle handle) noexcept
{
    if (contains(handle) == false)
    {
        return false;
    }
    Slot& slot = *m_Slots[handle.index];
    slot.value()->~T();
    slot.occupied = false;
    --m_Size;
    // 世代番号のwrapで古いHandleが復活しないよう、上限到達Slotは永久退役させます。
    if (slot.generation == (std::numeric_limits<std::uint32_t>::max)())
    {
        slot.retired = true;
    }
    else
    {
        ++slot.generation;
        slot.nextFree = m_FreeHead;
        m_FreeHead = handle.index;
    }
    return true;
}

template<typename T>
void HandlePool<T>::clear() noexcept
{
    // clear後もgenerationを保持し、過去のHandleを再有効化しません。
    for (size_type index = 0; index < m_Slots.size(); ++index)
    {
        Slot& slot = *m_Slots[index];
        if (slot.occupied)
        {
            erase(Handle{static_cast<std::uint32_t>(index), slot.generation});
        }
    }
}

template<typename T>
typename HandlePool<T>::size_type HandlePool<T>::size() const noexcept
{
    return m_Size;
}

template<typename T>
bool HandlePool<T>::empty() const noexcept
{
    return m_Size == 0;
}

template<typename T>
typename HandlePool<T>::size_type HandlePool<T>::capacity() const noexcept
{
    return m_Slots.size();
}

} // namespace Raven
