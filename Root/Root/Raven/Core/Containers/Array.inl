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
        m_Values.data()[index++] = value;
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
    return data();
}

template<typename T, std::size_t N>
constexpr typename Array<T, N>::const_iterator Array<T, N>::begin() const noexcept
{
    return data();
}

template<typename T, std::size_t N>
constexpr typename Array<T, N>::const_iterator Array<T, N>::cbegin() const noexcept
{
    return begin();
}

template<typename T, std::size_t N>
constexpr typename Array<T, N>::iterator Array<T, N>::end() noexcept
{
    // 空配列では nullptr + 0 も行わないようにします。
    if constexpr (N == 0)
    {
        return data();
    }
    else
    {
        return data() + N;
    }
}

template<typename T, std::size_t N>
constexpr typename Array<T, N>::const_iterator Array<T, N>::end() const noexcept
{
    if constexpr (N == 0)
    {
        return data();
    }
    else
    {
        return data() + N;
    }
}

template<typename T, std::size_t N>
constexpr typename Array<T, N>::const_iterator Array<T, N>::cend() const noexcept
{
    return end();
}

template<typename T, std::size_t N>
constexpr typename Array<T, N>::size_type Array<T, N>::max_size() const noexcept
{
    return N;
}

template<typename T, std::size_t N>
constexpr T& Array<T, N>::front() noexcept
{
    // 空配列でのアクセスは契約違反です。実行時には assert で検出します。
    static_assert(N > 0, "Array::front requires nonzero capacity");
    return (*this)[0];
}

template<typename T, std::size_t N>
constexpr const T& Array<T, N>::front() const noexcept
{
    static_assert(N > 0, "Array::front requires nonzero capacity");
    return (*this)[0];
}

template<typename T, std::size_t N>
constexpr T& Array<T, N>::back() noexcept
{
    static_assert(N > 0, "Array::back requires nonzero capacity");
    return (*this)[N - 1];
}

template<typename T, std::size_t N>
constexpr const T& Array<T, N>::back() const noexcept
{
    static_assert(N > 0, "Array::back requires nonzero capacity");
    return (*this)[N - 1];
}

template<typename T, std::size_t N>
constexpr T& Array<T, N>::operator[](size_type index) noexcept
{
    assert(index < N);
    return data()[index];
}

template<typename T, std::size_t N>
constexpr const T& Array<T, N>::operator[](size_type index) const noexcept
{
    assert(index < N);
    return data()[index];
}

template<typename T, std::size_t N>
T& Array<T, N>::at(size_type index)
{
    if (index >= N)
    {
        throw std::out_of_range("Array index out of range");
    }
    return data()[index];
}

template<typename T, std::size_t N>
const T& Array<T, N>::at(size_type index) const
{
    if (index >= N)
    {
        throw std::out_of_range("Array index out of range");
    }
    return data()[index];
}

template<typename T, std::size_t N>
constexpr void Array<T, N>::fill(const T& value)
{
    for (size_type index = 0; index < N; ++index)
    {
        data()[index] = value;
    }
}

} // namespace Raven
