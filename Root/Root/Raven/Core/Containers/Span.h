#pragma once

#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace Raven
{

// 連続領域を所有せず参照するViewです。
// 参照先の寿命と再確保による無効化は呼び出し側が管理してください。
template<typename T>
class Span
{
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = std::size_t;
    using iterator = T*;

    constexpr Span() noexcept;
    constexpr Span(T* data, size_type size) noexcept;

    template<std::size_t N>
    constexpr Span(T (&values)[N]) noexcept;

    template<typename Container,
        std::enable_if_t<
            std::is_convertible_v<decltype(std::declval<Container&>().data()), T*> &&
            std::is_convertible_v<decltype(std::declval<Container&>().size()), std::size_t>, int> = 0>
    constexpr Span(Container& values) noexcept;

    [[nodiscard]] constexpr T* data() const noexcept;
    [[nodiscard]] constexpr size_type size() const noexcept;
    [[nodiscard]] constexpr bool empty() const noexcept;
    [[nodiscard]] constexpr iterator begin() const noexcept;
    [[nodiscard]] constexpr iterator end() const noexcept;
    constexpr T& operator[](size_type index) const noexcept;
    [[nodiscard]] constexpr Span subspan(size_type offset, size_type count) const noexcept;

private:
    T* m_Data = nullptr;
    size_type m_Size = 0;
};

} // namespace Raven

#include "Raven/Core/Containers/Span.inl"
