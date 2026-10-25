#pragma once

namespace Raven
{

template<typename T>
constexpr Span<T>::Span() noexcept = default;

template<typename T>
constexpr Span<T>::Span(T* data, size_type size) noexcept
    : m_Data(data), m_Size(size)
{
    assert(data != nullptr || size == 0);
}

template<typename T>
template<std::size_t N>
constexpr Span<T>::Span(T (&values)[N]) noexcept
    : m_Data(values), m_Size(N)
{
}

template<typename T>
template<typename Container, typename Pointer, typename Size,
    std::enable_if_t<std::is_convertible_v<Pointer, T*> &&
        std::is_convertible_v<Size, typename Span<T>::size_type>, int>>
constexpr Span<T>::Span(Container& values) noexcept
    : Span(values.data(), static_cast<size_type>(values.size()))
{
}

template<typename T>
constexpr T* Span<T>::data() const noexcept
{
    return m_Data;
}

template<typename T>
constexpr typename Span<T>::size_type Span<T>::size() const noexcept
{
    return m_Size;
}

template<typename T>
constexpr bool Span<T>::empty() const noexcept
{
    return m_Size == 0;
}

template<typename T>
constexpr typename Span<T>::iterator Span<T>::begin() const noexcept
{
    return m_Data;
}

template<typename T>
constexpr typename Span<T>::iterator Span<T>::end() const noexcept
{
    // 空Spanでnullポインタに加算しないようにします。
    return m_Size == 0 ? m_Data : m_Data + m_Size;
}

template<typename T>
constexpr T& Span<T>::operator[](size_type index) const noexcept
{
    assert(index < m_Size);
    return m_Data[index];
}

template<typename T>
constexpr Span<T> Span<T>::subspan(size_type offset, size_type count) const noexcept
{
    assert(offset <= m_Size);
    assert(count <= m_Size - offset);
    return Span(offset == 0 ? m_Data : m_Data + offset, count);
}

} // namespace Raven
