#pragma once

namespace Raven
{

template<typename T, std::size_t N>
RingBuffer<T, N>::RingBuffer(RingBuffer&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
{
    // Slot自体は移譲できないためFIFO順で要素を移動します。
    // コンストラクタが途中で例外終了しても構築済み要素を手動破棄します。
    try
    {
        for (size_type index = 0; index < other.size(); ++index)
        {
            emplace_back(std::move(other[index]));
        }
    }
    catch (...)
    {
        clear();
        throw;
    }
    other.clear();
}

template<typename T, std::size_t N>
RingBuffer<T, N>::~RingBuffer() noexcept
{
    clear();
}

template<typename T, std::size_t N>
constexpr typename RingBuffer<T, N>::size_type RingBuffer<T, N>::capacity() const noexcept
{
    return N;
}

template<typename T, std::size_t N>
typename RingBuffer<T, N>::size_type RingBuffer<T, N>::size() const noexcept
{
    return m_Size;
}

template<typename T, std::size_t N>
bool RingBuffer<T, N>::empty() const noexcept
{
    return m_Size == 0;
}

template<typename T, std::size_t N>
bool RingBuffer<T, N>::full() const noexcept
{
    return m_Size == N;
}

template<typename T, std::size_t N>
typename RingBuffer<T, N>::size_type RingBuffer<T, N>::PhysicalIndex(size_type logicalIndex) const noexcept
{
    // head + indexの加算overflowを避け、剰余演算なしでwrapします。
    const size_type remaining = N - m_Head;
    return logicalIndex < remaining ? m_Head + logicalIndex : logicalIndex - remaining;
}

template<typename T, std::size_t N>
T* RingBuffer<T, N>::ElementAt(size_type index) noexcept
{
    return std::launder(reinterpret_cast<T*>(&m_Storage[index]));
}

template<typename T, std::size_t N>
const T* RingBuffer<T, N>::ElementAt(size_type index) const noexcept
{
    return std::launder(reinterpret_cast<const T*>(&m_Storage[index]));
}

template<typename T, std::size_t N>
template<typename... Args>
T* RingBuffer<T, N>::try_emplace_back(Args&&... args)
{
    if (full())
    {
        return nullptr;
    }
    T* location = ElementAt(PhysicalIndex(m_Size));
    ::new (static_cast<void*>(location)) T(std::forward<Args>(args)...);
    ++m_Size;
    return location;
}

template<typename T, std::size_t N>
template<typename... Args>
T& RingBuffer<T, N>::emplace_back(Args&&... args)
{
    if (full())
    {
        throw std::length_error("RingBuffer capacity exceeded");
    }
    return *try_emplace_back(std::forward<Args>(args)...);
}

template<typename T, std::size_t N>
bool RingBuffer<T, N>::try_push_back(const T& value)
{
    return try_emplace_back(value) != nullptr;
}

template<typename T, std::size_t N>
bool RingBuffer<T, N>::try_push_back(T&& value)
{
    return try_emplace_back(std::move(value)) != nullptr;
}

template<typename T, std::size_t N>
void RingBuffer<T, N>::push_back(const T& value)
{
    emplace_back(value);
}

template<typename T, std::size_t N>
void RingBuffer<T, N>::push_back(T&& value)
{
    emplace_back(std::move(value));
}

template<typename T, std::size_t N>
T& RingBuffer<T, N>::front() noexcept
{
    assert(empty() == false);
    return *ElementAt(m_Head);
}

template<typename T, std::size_t N>
const T& RingBuffer<T, N>::front() const noexcept
{
    assert(empty() == false);
    return *ElementAt(m_Head);
}

template<typename T, std::size_t N>
T& RingBuffer<T, N>::back() noexcept
{
    assert(empty() == false);
    return *ElementAt(PhysicalIndex(m_Size - 1));
}

template<typename T, std::size_t N>
const T& RingBuffer<T, N>::back() const noexcept
{
    assert(empty() == false);
    return *ElementAt(PhysicalIndex(m_Size - 1));
}

template<typename T, std::size_t N>
T& RingBuffer<T, N>::operator[](size_type index) noexcept
{
    assert(index < m_Size);
    return *ElementAt(PhysicalIndex(index));
}

template<typename T, std::size_t N>
const T& RingBuffer<T, N>::operator[](size_type index) const noexcept
{
    assert(index < m_Size);
    return *ElementAt(PhysicalIndex(index));
}

template<typename T, std::size_t N>
void RingBuffer<T, N>::pop_front() noexcept
{
    assert(empty() == false);
    ElementAt(m_Head)->~T();
    m_Head = m_Head + 1 == N ? 0 : m_Head + 1;
    --m_Size;
    if (empty())
    {
        m_Head = 0;
    }
}

template<typename T, std::size_t N>
void RingBuffer<T, N>::clear() noexcept
{
    while (empty() == false)
    {
        pop_front();
    }
}

} // namespace Raven
