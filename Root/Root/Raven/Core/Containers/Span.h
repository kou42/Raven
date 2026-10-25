#pragma once

#include <cassert>
#include <cstddef>
#include <type_traits>

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

    constexpr Span() noexcept = default;

    constexpr Span(T* data, size_type size) noexcept
        : m_Data(data), m_Size(size)
    {
        assert(data != nullptr || size == 0);
    }

    template<std::size_t N>
    constexpr Span(T (&values)[N]) noexcept
        : m_Data(values), m_Size(N)
    {
    }

    template<typename Container,
        typename Pointer = decltype(std::declval<Container&>().data()),
        typename Size = decltype(std::declval<Container&>().size()),
        std::enable_if_t<std::is_convertible_v<Pointer, T*> &&
            std::is_convertible_v<Size, size_type>, int> = 0>
    constexpr Span(Container& values) noexcept
        : Span(values.data(), static_cast<size_type>(values.size()))
    {
    }

    [[nodiscard]] constexpr T* data() const noexcept { return m_Data; }
    [[nodiscard]] constexpr size_type size() const noexcept { return m_Size; }
    [[nodiscard]] constexpr bool empty() const noexcept { return m_Size == 0; }
    [[nodiscard]] constexpr iterator begin() const noexcept { return m_Data; }
    [[nodiscard]] constexpr iterator end() const noexcept
    {
        // null pointerへの加算は空Spanでも未定義動作となるため回避します。
        return m_Size == 0 ? m_Data : m_Data + m_Size;
    }

    constexpr T& operator[](size_type index) const noexcept
    {
        assert(index < m_Size);
        return m_Data[index];
    }

    [[nodiscard]] constexpr Span subspan(size_type offset, size_type count) const noexcept
    {
        assert(offset <= m_Size);
        assert(count <= m_Size - offset);
        return Span(offset == 0 ? m_Data : m_Data + offset, count);
    }

private:
    T* m_Data = nullptr;
    size_type m_Size = 0;
};

} // namespace Raven
