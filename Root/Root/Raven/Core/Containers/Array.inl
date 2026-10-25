#pragma once

namespace Raven
{

template<typename T, std::size_t N>
constexpr Array<T, N>::Array(std::initializer_list<T> values)
{
    if (values.size() != N)
    {
        throw std::length_error("Array initializer size must match capacity");
    }
    size_type index = 0;
    for (const T& value : values)
    {
        m_Values[index++] = value;
    }
}

template<typename T, std::size_t N>
constexpr typename Array<T, N>::size_type Array<T, N>::size() const noexcept
{
    return N;
}

template<typename T, std::size_t N>
constexpr bool Array<T, N>::empty() const noexcept
{
    return N == 0;
}

template<typename T, std::size_t N>
constexpr T* Array<T, N>::data() noexcept
{
    return m_Values.data();
}

template<typename T, std::size_t N>
constexpr const T* Array<T, N>::data() const noexcept
{
    return m_Values.data();
}

template<typename T, std::size_t N>
constexpr typename Array<T, N>::iterator Array<T, N>::begin() noexcept
{
    return m_Values.data();
}

template<typename T, std::size_t N>
constexpr typename Array<T, N>::const_iterator Array<T, N>::begin() const noexcept
{
    return m_Values.data();
}

template<typename T, std::size_t N>
constexpr typename Array<T, N>::iterator Array<T, N>::end() noexcept
{
    return m_Values.end();
}

template<typename T, std::size_t N>
constexpr typename Array<T, N>::const_iterator Array<T, N>::end() const noexcept
{
    return m_Values.end();
}

template<typename T, std::size_t N>
constexpr T& Array<T, N>::operator[](size_type index) noexcept
{
    assert(index < N);
    return m_Values[index];
}

template<typename T, std::size_t N>
constexpr const T& Array<T, N>::operator[](size_type index) const noexcept
{
    assert(index < N);
    return m_Values[index];
}

template<typename T, std::size_t N>
T& Array<T, N>::at(size_type index)
{
    return m_Values.at(index);
}

template<typename T, std::size_t N>
const T& Array<T, N>::at(size_type index) const
{
    return m_Values.at(index);
}

template<typename T, std::size_t N>
constexpr void Array<T, N>::fill(const T& value)
{
    m_Values.fill(value);
}

} // namespace Raven
