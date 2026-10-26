#pragma once

namespace Raven
{

template<typename T, std::size_t N>
SmallVector<T, N>::SmallVector(Allocator& allocator) noexcept
        : m_Allocator(&allocator), m_Data(InlineData())
{
}

template<typename T, std::size_t N>
SmallVector<T, N>::~SmallVector()
{
    clear();
    ReleaseOverflow();
}

template<typename T, std::size_t N>
SmallVector<T, N>::SmallVector(SmallVector&& other)
        : m_Allocator(other.m_Allocator), m_Data(InlineData())
{
    if (other.UsingInline() == false)
    {
        // heap領域は所有権を移すだけで、要素の移動コストを発生させません。
        m_Data = other.m_Data;
        m_Size = other.m_Size;
        m_Capacity = other.m_Capacity;
        other.m_Data = other.InlineData();
        other.m_Size = 0;
        other.m_Capacity = N;
    }
    else
    {
        // 失敗時もotherを破壊せず、構築済み要素だけを巻き戻します。
        std::size_t built = 0;
        try
        {
            for (; built < other.m_Size; ++built)
            {
                ::new (static_cast<void*>(m_Data + built))
                    T(std::move_if_noexcept(other.m_Data[built]));
            }
        }
        catch (...)
        {
            DestroyRange(m_Data, built);
            throw;
        }
        m_Size = built;
        other.clear();
    }
}

template<typename T, std::size_t N>
typename SmallVector<T, N>::size_type SmallVector<T, N>::size() const noexcept
{ return m_Size; }

template<typename T, std::size_t N>
typename SmallVector<T, N>::size_type SmallVector<T, N>::capacity() const noexcept
{ return m_Capacity; }

template<typename T, std::size_t N>
bool SmallVector<T, N>::empty() const noexcept
{ return m_Size == 0; }

template<typename T, std::size_t N>
bool SmallVector<T, N>::UsingInlineStorage() const noexcept
{ return UsingInline(); }

template<typename T, std::size_t N>
T* SmallVector<T, N>::data() noexcept
{ return m_Data; }

template<typename T, std::size_t N>
const T* SmallVector<T, N>::data() const noexcept
{ return m_Data; }

template<typename T, std::size_t N>
typename SmallVector<T, N>::iterator SmallVector<T, N>::begin() noexcept
{ return m_Data; }

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_iterator SmallVector<T, N>::begin() const noexcept
{ return m_Data; }

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_iterator SmallVector<T, N>::cbegin() const noexcept
{ return begin(); }

template<typename T, std::size_t N>
typename SmallVector<T, N>::iterator SmallVector<T, N>::end() noexcept
{ return m_Data + m_Size; }

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_iterator SmallVector<T, N>::end() const noexcept
{ return m_Data + m_Size; }

template<typename T, std::size_t N>
typename SmallVector<T, N>::const_iterator SmallVector<T, N>::cend() const noexcept
{ return end(); }

template<typename T, std::size_t N>
T& SmallVector<T, N>::operator[](size_type index) noexcept
{
    assert(index < m_Size);
    return m_Data[index];
}

template<typename T, std::size_t N>
const T& SmallVector<T, N>::operator[](size_type index) const noexcept
{
    assert(index < m_Size);
    return m_Data[index];
}

template<typename T, std::size_t N>
T& SmallVector<T, N>::front() noexcept
{ assert(m_Size > 0); return m_Data[0]; }

template<typename T, std::size_t N>
const T& SmallVector<T, N>::front() const noexcept
{ assert(m_Size > 0); return m_Data[0]; }

template<typename T, std::size_t N>
T& SmallVector<T, N>::back() noexcept
{ assert(m_Size > 0); return m_Data[m_Size - 1]; }

template<typename T, std::size_t N>
const T& SmallVector<T, N>::back() const noexcept
{ assert(m_Size > 0); return m_Data[m_Size - 1]; }

template<typename T, std::size_t N>
template<typename... Args>
T& SmallVector<T, N>::emplace_back(Args&&... args)
{
    if (m_Size == m_Capacity)
    {
        // 先に新要素を一時構築し、既存要素の移設中に参照が無効化されるのを防ぎます。
        // move-onlyでthrowする型はstatic_assertで除外しています。
        T pending(std::forward<Args>(args)...);
        Grow(m_Size + 1);
        ::new (static_cast<void*>(m_Data + m_Size)) T(std::move_if_noexcept(pending));
    }
    else
    {
        ::new (static_cast<void*>(m_Data + m_Size)) T(std::forward<Args>(args)...);
    }
    return m_Data[m_Size++];
}

template<typename T, std::size_t N>
void SmallVector<T, N>::push_back(const T& value)
{ emplace_back(value); }

template<typename T, std::size_t N>
void SmallVector<T, N>::push_back(T&& value)
{ emplace_back(std::move(value)); }

template<typename T, std::size_t N>
void SmallVector<T, N>::pop_back()
{
    assert(m_Size > 0);
    --m_Size;
    m_Data[m_Size].~T();
}

template<typename T, std::size_t N>
void SmallVector<T, N>::clear() noexcept
{
    DestroyRange(m_Data, m_Size);
    m_Size = 0;
}

template<typename T, std::size_t N>
void SmallVector<T, N>::reserve(size_type count)
{
    if (count > m_Capacity)
    {
        Grow(count);
    }
}

template<typename T, std::size_t N>
Allocator* SmallVector<T, N>::GetAllocator() const noexcept
{ return m_Allocator; }

template<typename T, std::size_t N>
T* SmallVector<T, N>::InlineData() noexcept
{
    return reinterpret_cast<T*>(m_Inline);
}

template<typename T, std::size_t N>
bool SmallVector<T, N>::UsingInline() const noexcept
{
    return m_Data == reinterpret_cast<const T*>(m_Inline);
}

template<typename T, std::size_t N>
void SmallVector<T, N>::DestroyRange(T* data, size_type count) noexcept
{
    while (count > 0)
    {
        data[--count].~T();
    }
}

template<typename T, std::size_t N>
void SmallVector<T, N>::ReleaseOverflow() noexcept
{
    if (UsingInline() == false)
    {
        m_Allocator->Deallocate(m_Data);
    }
}

template<typename T, std::size_t N>
void SmallVector<T, N>::Grow(size_type required)
{
    if (required > (std::numeric_limits<size_type>::max)() / sizeof(T))
    {
        throw std::length_error("SmallVector capacity overflow");
    }

    const size_type maxCount = (std::numeric_limits<size_type>::max)() / sizeof(T);
    size_type next = m_Capacity;
    while (next < required)
    {
        if (next > maxCount / 2)
        {
            next = required;
            break;
        }
        next *= 2;
    }

    void* raw = m_Allocator->Allocate(next * sizeof(T), alignof(T));
    if (raw == nullptr)
    {
        throw std::bad_alloc{};
    }

    T* destination = static_cast<T*>(raw);
    size_type built = 0;
    try
    {
        for (; built < m_Size; ++built)
        {
            ::new (static_cast<void*>(destination + built))
                T(std::move_if_noexcept(m_Data[built]));
        }
    }
    catch (...)
    {
        DestroyRange(destination, built);
        m_Allocator->Deallocate(destination);
        throw;
    }

    // 全要素の移設が成功してから旧領域を破棄し、所有権を切り替えます。
    DestroyRange(m_Data, m_Size);
    ReleaseOverflow();
    m_Data = destination;
    m_Capacity = next;
}

} // namespace Raven
