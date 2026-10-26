#pragma once

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <utility>

namespace Raven
{

namespace Detail
{
// N == 0 の場合も標準C++で有効なストレージを持ち、要素は構築しません。
template<typename T, std::size_t N>
struct ArrayStorage
{
    T values[N]{};
    constexpr T* data() noexcept { return values; }
    constexpr const T* data() const noexcept { return values; }
};

template<typename T>
struct ArrayStorage<T, 0>
{
    constexpr T* data() noexcept { return nullptr; }
    constexpr const T* data() const noexcept { return nullptr; }
};
} // namespace Detail

// 固定長の連続領域をオブジェクト内に保持します。
// 容量はコンパイル時に決定し、動的メモリ確保は行いません。
template<typename T, std::size_t N>
class Array
{
public:
    using value_type = T;
    using size_type = std::size_t;
    using iterator = T*;
    using const_iterator = const T*;

    constexpr Array() = default;
    constexpr Array(std::initializer_list<T> values);

    [[nodiscard]] constexpr size_type size() const noexcept;
    [[nodiscard]] constexpr bool empty() const noexcept;
    [[nodiscard]] constexpr T* data() noexcept;
    [[nodiscard]] constexpr const T* data() const noexcept;
    [[nodiscard]] constexpr iterator begin() noexcept;
    [[nodiscard]] constexpr const_iterator begin() const noexcept;
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept;
    [[nodiscard]] constexpr iterator end() noexcept;
    [[nodiscard]] constexpr const_iterator end() const noexcept;
    [[nodiscard]] constexpr const_iterator cend() const noexcept;
    [[nodiscard]] constexpr size_type max_size() const noexcept;
    constexpr T& front() noexcept;
    constexpr const T& front() const noexcept;
    constexpr T& back() noexcept;
    constexpr const T& back() const noexcept;
    constexpr T& operator[](size_type index) noexcept;
    constexpr const T& operator[](size_type index) const noexcept;
    T& at(size_type index);
    const T& at(size_type index) const;
    constexpr void fill(const T& value);

private:
    Detail::ArrayStorage<T, N> m_Values{};
};

} // namespace Raven

#include "Raven/Core/Containers/Array.inl"
