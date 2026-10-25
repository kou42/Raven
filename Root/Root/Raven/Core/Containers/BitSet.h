#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace Raven
{

// 固定ビット数のBitSetです。動的確保を行わず64bit単位で保持します。
// 末尾の未使用bitは常に0に保ち、count/allの結果へ混入させません。
template<std::size_t N>
class BitSet
{
public:
    using size_type = std::size_t;
    using word_type = std::uint64_t;
    static constexpr size_type BitsPerWord = 64;
    static constexpr size_type WordCount = N / BitsPerWord + (N % BitsPerWord != 0 ? 1 : 0);

    constexpr BitSet() noexcept = default;
    [[nodiscard]] constexpr size_type size() const noexcept;
    [[nodiscard]] constexpr bool empty() const noexcept;
    [[nodiscard]] bool test(size_type index) const;
    [[nodiscard]] bool operator[](size_type index) const noexcept;
    BitSet& set(size_type index, bool value = true);
    BitSet& reset(size_type index);
    BitSet& flip(size_type index);
    BitSet& set() noexcept;
    BitSet& reset() noexcept;
    BitSet& flip() noexcept;
    [[nodiscard]] bool any() const noexcept;
    [[nodiscard]] bool none() const noexcept;
    [[nodiscard]] bool all() const noexcept;
    [[nodiscard]] size_type count() const noexcept;

private:
    static constexpr word_type LastWordMask() noexcept;
    std::array<word_type, WordCount> m_Words{};
};

} // namespace Raven

#include "Raven/Core/Containers/BitSet.inl"
